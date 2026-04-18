#include "hex_editor_core.h"
#include "gui.h"
#include "app_state.h"
#include "updater.h"
#include "IconsFontAwesome6.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <shellapi.h>
#  include <ole2.h>
#  define GLFW_EXPOSE_NATIVE_WIN32
#  include <GLFW/glfw3native.h>
#  include "win_drop_target.h"
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

static bool FileExists(const std::string& path) {
    std::ifstream f(path.c_str(), std::ios::binary);
    return f.good();
}

/* ImGui hard-asserts inside stb_truetype on garbage input (including
 * WOFF/WOFF2 files renamed to .ttf), so reject anything whose magic bytes
 * aren't a recognized TrueType/OpenType signature. */
static bool IsValidFontFile(const std::string& path) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f.good()) return false;
    unsigned char s[4] = {0, 0, 0, 0};
    f.read(reinterpret_cast<char*>(s), 4);
    if (f.gcount() != 4) return false;
    if (s[0] == 0x00 && s[1] == 0x01 && s[2] == 0x00 && s[3] == 0x00) return true;
    if (s[0] == 't' && s[1] == 'r' && s[2] == 'u' && s[3] == 'e') return true;
    if (s[0] == 'O' && s[1] == 'T' && s[2] == 'T' && s[3] == 'O') return true;
    if (s[0] == 't' && s[1] == 't' && s[2] == 'c' && s[3] == 'f') return true;
    return false;
}

static ImFont* TryLoadFont(ImGuiIO& io,
                           const std::string& path,
                           float size_px,
                           const ImFontConfig* cfg,
                           const ImWchar* ranges = nullptr) {
    if (!FileExists(path)) return nullptr;
    if (!IsValidFontFile(path)) {
        std::fprintf(stderr,
            "[font] skipping '%s' — not a valid TTF/OTF (bad magic bytes)\n",
            path.c_str());
        return nullptr;
    }
    std::fprintf(stderr, "[font] loading '%s' at %.1fpx\n", path.c_str(), size_px);
    return io.Fonts->AddFontFromFileTTF(path.c_str(), size_px, cfg, ranges);
}

static void glfw_error_callback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

struct AppContext {
    AppState state = AppState::StartScreen;
    std::unique_ptr<HexEditorCore> core;
    std::string pending_path;
    std::string load_error;
    GLFWwindow* window = nullptr;
    /* Empty unless the Settings popup has approved an update: absolute path
     * to a downloaded-and-SHA-verified NSIS installer in %TEMP%. */
    std::string installer_to_launch;
#ifdef _WIN32
    /* Flipped by our IDropTarget; read by the render loop each frame to
     * decide whether to draw the drop-zone overlay. */
    platform::DragState drag_over = platform::DragState::None;
#endif
};

#ifdef _WIN32
/* Copy the installed updater helper to %TEMP% (NSIS can't delete a running
 * binary from $INSTDIR, so the helper must not run from its installed
 * location), then ShellExecute it with the installer path and our PID.
 * Returns true on successful spawn. */
static bool LaunchUpdaterHelper(const std::string& installer_path_utf8,
                                std::string& err_utf8) {
    wchar_t exe_path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) { err_utf8 = "GetModuleFileName failed"; return false; }

    std::wstring exe_dir(exe_path);
    size_t slash = exe_dir.find_last_of(L"\\/");
    if (slash == std::wstring::npos) { err_utf8 = "bad exe path"; return false; }
    exe_dir.resize(slash);

    std::wstring installed_helper = exe_dir + L"\\hxediter-updater-helper.exe";
    if (GetFileAttributesW(installed_helper.c_str()) == INVALID_FILE_ATTRIBUTES) {
        err_utf8 = "updater helper is missing next to hxediter.exe";
        return false;
    }

    wchar_t tmp[MAX_PATH];
    DWORD tn = GetTempPathW(MAX_PATH, tmp);
    if (tn == 0 || tn >= MAX_PATH) { err_utf8 = "GetTempPath failed"; return false; }
    wchar_t helper_name[64];
    swprintf(helper_name, 64, L"hxediter-updater-helper-%lu.exe",
             (unsigned long)GetCurrentProcessId());
    std::wstring temp_helper = std::wstring(tmp) + helper_name;

    if (!CopyFileW(installed_helper.c_str(), temp_helper.c_str(), FALSE)) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "CopyFile failed (err %lu)",
                      (unsigned long)GetLastError());
        err_utf8 = buf;
        return false;
    }

    /* Installer path may contain spaces — quote it. */
    int wide_n = MultiByteToWideChar(CP_UTF8, 0, installer_path_utf8.c_str(),
                                     (int)installer_path_utf8.size(), nullptr, 0);
    std::wstring winst(wide_n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, installer_path_utf8.c_str(),
                        (int)installer_path_utf8.size(),
                        winst.data(), wide_n);

    wchar_t args[MAX_PATH + 64];
    swprintf(args, MAX_PATH + 64, L"\"%s\" %lu",
             winst.c_str(), (unsigned long)GetCurrentProcessId());

    HINSTANCE r = ShellExecuteW(nullptr, L"open", temp_helper.c_str(),
                                 args, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "ShellExecute failed (code %lld)",
                      (long long)(INT_PTR)r);
        err_utf8 = buf;
        return false;
    }
    return true;
}
#endif

static void glfw_drop_callback(GLFWwindow* w, int count, const char** paths) {
    auto* ctx = static_cast<AppContext*>(glfwGetWindowUserPointer(w));
    if (ctx == nullptr || count <= 0) return;
    ctx->pending_path = paths[0];
    glfwFocusWindow(w);
}

int main(int argc, char* argv[]) {
    const auto startup_begin = std::chrono::steady_clock::now();

    AppContext ctx;

    if (argc >= 2) {
        try {
            ctx.core = std::make_unique<HexEditorCore>(argv[1]);
            ctx.state = AppState::HexView;
        } catch (const std::exception& e) {
            /* Fall through to the start screen with an error. */
            ctx.load_error = e.what();
            std::fprintf(stderr, "Error: %s\n", e.what());
        }
    }

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    std::string title = ctx.core ? ("hxediter — " + ctx.core->GetFilename())
                                  : std::string("hxediter");
    GLFWwindow* window = glfwCreateWindow(1280, 720, title.c_str(), nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }
    ctx.window = window;
    glfwSetWindowUserPointer(window, &ctx);
    glfwSetDropCallback(window, glfw_drop_callback);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

#ifdef _WIN32
    /* Replace GLFW's internal IDropTarget with ours so we get DragEnter /
     * DragOver / DragLeave in addition to Drop. The custom target writes
     * ctx.pending_path on drop, mirroring glfw_drop_callback's behavior,
     * so the main loop's consumer is unchanged. OleInitialize is
     * refcounted; safe even though GLFW already called it. */
    HWND hwnd = glfwGetWin32Window(window);
    OleInitialize(nullptr);
    RevokeDragDrop(hwnd);
    platform::WinDropTarget* drop_target =
        new platform::WinDropTarget(&ctx.drag_over, &ctx.pending_path);
    RegisterDragDrop(hwnd, drop_target);
#endif

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImFontConfig ui_cfg;
    ui_cfg.OversampleH = 3;
    ui_cfg.OversampleV = 2;
    ImFontConfig mono_cfg = ui_cfg;

    const std::vector<std::string> ui_font_candidates = {
        "assets/fonts/Roboto-Regular.ttf",
        "/usr/share/fonts/truetype/roboto/unhinted/RobotoTTF/Roboto-Regular.ttf",
        "/usr/share/fonts/truetype/roboto/Roboto-Regular.ttf",
        "C:\\Windows\\Fonts\\Roboto-Regular.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
    };
    const std::vector<std::string> mono_font_candidates = {
        "assets/fonts/JetBrainsMono-Regular.ttf",
        "/usr/share/fonts/truetype/jetbrains-mono/JetBrainsMono-Regular.ttf",
        "/usr/share/fonts/TTF/JetBrainsMono-Regular.ttf",
        "C:\\Windows\\Fonts\\JetBrainsMono-Regular.ttf",
        "C:\\Windows\\Fonts\\consola.ttf",
    };

    ImFont* ui_font = nullptr;
    for (const auto& path : ui_font_candidates) {
        ui_font = TryLoadFont(io, path, 17.0f, &ui_cfg);
        if (ui_font) break;
    }
    ImFont* mono_font = nullptr;
    for (const auto& path : mono_font_candidates) {
        mono_font = TryLoadFont(io, path, 16.0f, &mono_cfg);
        if (mono_font) break;
    }

    ImFont* title_font = nullptr;
    ImFontConfig title_cfg = ui_cfg;
    title_cfg.OversampleH = 2;
    title_cfg.OversampleV = 2;
    for (const auto& path : ui_font_candidates) {
        title_font = TryLoadFont(io, path, 48.0f, &title_cfg);
        if (title_font) break;
    }

    /* Narrow the FontAwesome glyph range to the codepoints we use: the full
     * FA range at 96px blows past OpenGL's max texture size on many GPUs
     * and crashes atlas construction. */
    static const ImWchar fa_ranges[] = {
        0xf15b, 0xf15b,   /* ICON_FA_FILE */
        0
    };
    ImFontConfig icon_cfg;
    icon_cfg.OversampleH = 2;
    icon_cfg.OversampleV = 2;
    icon_cfg.PixelSnapH  = true;
    ImFont* icon_font = TryLoadFont(io,
        "assets/fonts/fa-solid-900.ttf", 96.0f, &icon_cfg, fa_ranges);

    /* Second, small-size FA atlas for toolbar icons (gear, future friends).
     * Separate from the 96px atlas so each glyph renders at its correct
     * visual size without per-button font scaling. */
    static const ImWchar fa_small_ranges[] = {
        0xf013, 0xf013,   /* ICON_FA_GEAR */
        0
    };
    ImFontConfig icon_small_cfg;
    icon_small_cfg.OversampleH = 2;
    icon_small_cfg.OversampleV = 2;
    icon_small_cfg.PixelSnapH  = true;
    ImFont* icon_font_small = TryLoadFont(io,
        "assets/fonts/fa-solid-900.ttf", 18.0f, &icon_small_cfg, fa_small_ranges);

    if (!ui_font)    ui_font = io.Fonts->AddFontDefault();
    if (!mono_font)  mono_font = ui_font;
    if (!title_font) title_font = ui_font;

    io.FontDefault = ui_font;
    SetEditorFonts(ui_font, mono_font, title_font, icon_font, icon_font_small);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    /* Kick off the GitHub releases check in a background thread (debounced
     * to one real request per 6h via %LOCALAPPDATA%\HxEditer\update_state.json). */
    updater::InitAndMaybeCheck();

    ImVec4 clear_color = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    bool startup_measured = false;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        /* Reset the old core *before* constructing the new one so the
         * previous handle is closed before HexEditorCore's
         * is_file_held_by_other_process probe runs. Trade-off: a failed
         * load also closes the previously-open file. */
        if (!ctx.pending_path.empty()) {
            std::string path = ctx.pending_path;
            ctx.pending_path.clear();
            ctx.core.reset();
            try {
                ctx.core = std::make_unique<HexEditorCore>(path);
                ctx.load_error.clear();
                ctx.state = AppState::HexView;
                std::string new_title = "hxediter — " + ctx.core->GetFilename();
                glfwSetWindowTitle(window, new_title.c_str());
                /* Drop input focus so the new file's first frame has
                 * no ghost active item from the previous file. */
                ImGui::ClearActiveID();
            } catch (const std::exception& e) {
                ctx.load_error = e.what();
                ctx.state = AppState::StartScreen;
                glfwSetWindowTitle(window, "hxediter");
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

#ifdef _WIN32
        int drag_over_state = static_cast<int>(ctx.drag_over);
#else
        int drag_over_state = 0;
#endif
        RenderHexEditorUI(ctx.state, ctx.core.get(), ctx.load_error.c_str(),
                          &ctx.pending_path, &ctx.installer_to_launch,
                          drag_over_state);

#ifdef _WIN32
        /* If the user dismissed the (non-modal) Settings popup mid-download,
         * the popup won't write installer_to_launch when it completes. Pull
         * the path directly from the updater module so the handoff fires
         * regardless of popup visibility. */
        if (ctx.installer_to_launch.empty()) {
            updater::ConsumeInstallerPath(ctx.installer_to_launch);
        }

        /* Settings popup has written a verified installer path here (or
         * ConsumeInstallerPath just did). Spawn the helper (which waits
         * for us to exit then elevates). If the spawn itself fails,
         * surface the error through the updater module so the next
         * Settings open shows it — do NOT close the window. */
        if (!ctx.installer_to_launch.empty()) {
            std::string err;
            if (LaunchUpdaterHelper(ctx.installer_to_launch, err)) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            } else {
                updater::SetLaunchError("Could not start updater: " + err);
            }
            ctx.installer_to_launch.clear();
        }
#endif

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);

        if (!startup_measured) {
            auto elapsed = std::chrono::steady_clock::now() - startup_begin;
            float ms = std::chrono::duration<float, std::milli>(elapsed).count();
            SetStartupDuration(ms);
            startup_measured = true;
        }
    }

    /* Signal in-flight updater threads to drop their results instead of
     * writing back into module state that's about to be destroyed. Threads
     * are detached; we don't join. */
    updater::RequestAbandon();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

#ifdef _WIN32
    RevokeDragDrop(hwnd);
    drop_target->Release();
    OleUninitialize();
#endif

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

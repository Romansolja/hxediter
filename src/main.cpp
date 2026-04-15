/* hxediter — Dear ImGui hex editor (GLFW + OpenGL3) */

#include "hex_editor_core.h"
#include "gui.h"
#include "app_state.h"
#include "IconsFontAwesome6.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

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

/* Validate that a file looks like a real TrueType / OpenType font by reading
 * its 4-byte magic number. We do this because ImGui hard-asserts inside
 * stb_truetype when it encounters garbage (or when the file is actually
 * WOFF/WOFF2 masquerading as .ttf), and the debug build aborts before any
 * window is shown. Any file that fails this check is skipped by the loader. */
static bool IsValidFontFile(const std::string& path) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f.good()) return false;
    unsigned char s[4] = {0, 0, 0, 0};
    f.read(reinterpret_cast<char*>(s), 4);
    if (f.gcount() != 4) return false;
    /* TTF: 00 01 00 00  — standard TrueType outlines */
    if (s[0] == 0x00 && s[1] == 0x01 && s[2] == 0x00 && s[3] == 0x00) return true;
    /* 'true' — legacy Apple TrueType */
    if (s[0] == 't' && s[1] == 'r' && s[2] == 'u' && s[3] == 'e') return true;
    /* 'OTTO' — OpenType with CFF outlines */
    if (s[0] == 'O' && s[1] == 'T' && s[2] == 'T' && s[3] == 'O') return true;
    /* 'ttcf' — TrueType Collection (e.g. Windows arial.ttf on modern Win10) */
    if (s[0] == 't' && s[1] == 't' && s[2] == 'c' && s[3] == 'f') return true;
    return false;
}

/* Attempt to load a TTF into the atlas, skipping (and logging) files that
 * fail the magic-byte check. Returns nullptr on failure. */
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

/* Application context shared between main() and the GLFW drop callback.
 * The drop callback only writes pending_path; the main loop consumes it. */
struct AppContext {
    AppState state = AppState::StartScreen;   /* flipped to HexView on successful load */
    std::unique_ptr<HexEditorCore> core;
    std::string pending_path;
    std::string load_error;
    GLFWwindow* window = nullptr;
};

static void glfw_drop_callback(GLFWwindow* w, int count, const char** paths) {
    auto* ctx = static_cast<AppContext*>(glfwGetWindowUserPointer(w));
    if (ctx == nullptr || count <= 0) return;
    /* Only the first dropped file is opened. */
    ctx->pending_path = paths[0];
    /* Bring the window to the foreground so the user sees the result. */
    glfwFocusWindow(w);
}

int main(int argc, char* argv[]) {
    AppContext ctx;

    /* ---- Optional initial file from argv ---- */
    if (argc >= 2) {
        try {
            ctx.core = std::make_unique<HexEditorCore>(argv[1]);
            ctx.state = AppState::HexView;
        } catch (const std::exception& e) {
            /* Don't bail — fall through to the start screen with an error. */
            ctx.load_error = e.what();
            std::fprintf(stderr, "Error: %s\n", e.what());
        }
    }

    /* ---- GLFW init ---- */
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
    glfwSwapInterval(1); /* vsync */

    /* ---- Dear ImGui init ---- */
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

    /* Title font: same family as the UI font, rendered at 48px for the
     * "HxEditer" brand text on the start screen. */
    ImFont* title_font = nullptr;
    ImFontConfig title_cfg = ui_cfg;
    title_cfg.OversampleH = 2;
    title_cfg.OversampleV = 2;
    for (const auto& path : ui_font_candidates) {
        title_font = TryLoadFont(io, path, 48.0f, &title_cfg);
        if (title_font) break;
    }

    /* FontAwesome icon font at 96px — loaded standalone (NOT merged into the
     * UI font) so the start screen's hero file icon renders crisply at its
     * natural size without SetWindowFontScale tricks. If fa-solid-900.ttf is
     * missing, icon_font stays null and RenderStartScreen draws a placeholder
     * rectangle instead.
     *
     * The glyph range is narrowed to exactly the codepoints we use. Loading
     * the full FA range (~2000 glyphs) at 96px blows past OpenGL's max
     * texture size on many GPUs and crashes atlas construction. */
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

    if (!ui_font)    ui_font = io.Fonts->AddFontDefault();
    if (!mono_font)  mono_font = ui_font;
    if (!title_font) title_font = ui_font;
    /* icon_font may stay null — RenderStartScreen handles that. */

    io.FontDefault = ui_font;
    SetEditorFonts(ui_font, mono_font, title_font, icon_font);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    /* ---- Render loop ---- */
    ImVec4 clear_color = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        /* ---- Consume any pending file drop / dialog result ----
         * The old core is reset *before* constructing the new one. On
         * Windows, fopen("rb+") opens the file with write-share denied,
         * so reopening the same path while the old handle is still alive
         * fails with a sharing violation. The trade-off: a failed load
         * also closes the previously-open file. The user can re-drop it
         * from the start screen.
         *
         * pending_path is set by (a) the GLFW drop callback or (b) the
         * Select File button on the start screen writing the ImGuiFileDialog
         * result. Both paths funnel through here. */
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
                /* Drop any input focus held over from the previous file
                 * so the new file's first frame has no ghost active item. */
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

        RenderHexEditorUI(ctx.state, ctx.core.get(), ctx.load_error.c_str(), &ctx.pending_path);

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    /* ---- Cleanup ---- */
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

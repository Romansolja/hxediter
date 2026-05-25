#include "hex_editor_core.h"
#include "gui.h"
#include "app_state.h"
#include "path_utils.h"
#include "platform/asset_path.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

static bool FileExists(const std::string& path) {
    std::ifstream f(path.c_str(), std::ios::binary);
    return f.good();
}

// stb_truetype hard-asserts on garbage input (including WOFF/WOFF2 files
// renamed to .ttf); reject unrecognized magic up front.
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
    AppState                  state = AppState::StartScreen;
    std::vector<OpenDocument> docs;
    int                       active_doc      = -1;
    int                       last_titled_doc = -2;   // sentinel: forces first-frame title set
    std::vector<std::string>  pending_paths;
    std::vector<int>          close_indices;          // filled by gui; consumed in main
    // Folders dropped or passed on the CLI land here. Each frame the main
    // loop drains this, replaces directory_files, and updates directory_label.
    std::vector<std::string>  pending_directories;
    // Files in the most-recently-loaded folder, alphabetized. Surfaced in
    // the tab-bar dropdown so the user picks which to open as tabs —
    // folder drops no longer auto-open every file.
    std::vector<std::string>  directory_files;
    std::string               directory_label;        // basename for the dropdown header
    // Canonical paths of every doc in `docs`. Kept in sync on every
    // push_back/erase. Backs O(1) dedup during multi-file drops — pairwise
    // filesystem::equivalent was O(N*M) and froze the UI past a few hundred.
    std::unordered_set<std::string> open_canonical;
    std::string               load_error;
    GLFWwindow*               window = nullptr;
};

// Stable string key per file — weakly_canonical handles relative paths,
// case differences, and intermediate symlinks. Errors fall back to the
// original path (still better than nothing for dedup).
// TODO: this fires once per dropped path. With kMaxOpenDocs=200 the cost
// is bounded; if the cap is ever raised much higher, batch the canonical
// resolution or memoize against the input string.
static std::string CanonicalKey(const std::string& utf8_path) {
    std::error_code ec;
    auto canon = std::filesystem::weakly_canonical(PathFromUtf8(utf8_path), ec);
    if (ec) return utf8_path;
    return PathToUtf8(canon);
}

// Linear scan to recover the doc index for a canonical key. Only used
// when refocusing an already-open file (rare path).
static int FindDocByCanonical(const std::vector<OpenDocument>& docs,
                              const std::string& canonical_key) {
    for (int i = 0; i < (int)docs.size(); ++i) {
        if (!docs[i].core) continue;
        if (CanonicalKey(docs[i].core->GetFilename()) == canonical_key) return i;
    }
    return -1;
}

static void glfw_drop_callback(GLFWwindow* w, int count, const char** paths) {
    auto* ctx = static_cast<AppContext*>(glfwGetWindowUserPointer(w));
    if (ctx == nullptr || count <= 0) return;
    for (int i = 0; i < count; ++i) {
        const char* p = paths[i];
        if (!p) continue;
        std::error_code ec;
        std::filesystem::path fsp = PathFromUtf8(p);
        auto status = std::filesystem::status(fsp, ec);
        if (ec) continue;
        if (std::filesystem::is_directory(status)) {
            // Folder drops surface a directory listing in the tab-bar
            // dropdown; the user explicitly clicks files to open.
            ctx->pending_directories.push_back(p);
        } else if (std::filesystem::is_regular_file(status)) {
            ctx->pending_paths.push_back(p);
        }
    }
    glfwFocusWindow(w);
}


static std::string BuildBatchError(const std::string& first, int additional) {
    if (first.empty()) return std::string();
    if (additional <= 0) return first;
    char suffix[48];
    std::snprintf(suffix, sizeof(suffix), " (and %d more)", additional);
    return first + suffix;
}

static void UpdateWindowTitle(AppContext& ctx) {
    std::string title;
    if (ctx.state == AppState::HexView &&
        ctx.active_doc >= 0 &&
        ctx.active_doc < (int)ctx.docs.size() &&
        ctx.docs[ctx.active_doc].core) {
        title = "hxediter — " + ctx.docs[ctx.active_doc].core->GetFilename();
    } else {
        title = "hxediter";
    }
    glfwSetWindowTitle(ctx.window, title.c_str());
}

// Map AppContext state -> the index that drives the window title. -1
// stands in for any non-HexView state; only call glfwSetWindowTitle when
// this target changes.
static int TitleTargetIndex(const AppContext& ctx) {
    return (ctx.state == AppState::HexView) ? ctx.active_doc : -1;
}

int main(int argc, char* argv[]) {
    const auto startup_begin = std::chrono::steady_clock::now();

    AppContext ctx;

    // CLI args: each argument is a file or directory; directories are
    // expanded recursively. Same code path as a multi-file drag-drop.
    std::vector<std::string> utf8_args;
    for (int i = 1; i < argc; ++i) {
        if (argv[i]) utf8_args.emplace_back(argv[i]);
    }
    for (const std::string& a : utf8_args) {
        std::error_code ec;
        std::filesystem::path fsp = PathFromUtf8(a);
        auto status = std::filesystem::status(fsp, ec);
        if (ec) continue;
        if (std::filesystem::is_directory(status)) {
            // Mirror drag-drop behavior: directories populate the
            // dropdown listing rather than auto-opening every file.
            ctx.pending_directories.push_back(a);
        } else if (std::filesystem::is_regular_file(status)) {
            ctx.pending_paths.push_back(a);
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
    // Start maximized so 1280x720 doesn't look like a postage stamp on
    // 4K / ultrawide. The OS still draws normal window chrome — user can
    // un-maximize via the title-bar button.
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "hxediter",
                                          nullptr, nullptr);
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

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Launched from /Applications cwd is "/" (read-only), so the default
    // "imgui.ini" silently fails to save. Redirect into Application Support.
    // Static so the c_str() ImGui holds outlives every frame.
    if (!platform::AppSupportDir().empty()) {
        static const std::string g_imgui_ini_path =
            platform::AppSupportDir() + "imgui.ini";
        io.IniFilename = g_imgui_ini_path.c_str();
    }

    // Native Cmd-based text-input shortcuts in InputText (Cmd+A/C/V/X,
    // Option-arrow). Must be set before the first frame.
    io.ConfigMacOSXBehaviors = true;

    ImGui::StyleColorsDark();

    // HiDPI one-time bake — mid-session moves to a different-DPI display require restart.
    float content_scale;
    {
        float sx = 1.0f, sy = 1.0f;
        glfwGetWindowContentScale(window, &sx, &sy);
        content_scale = (sx > sy) ? sx : sy;
        if (content_scale < 1.0f) content_scale = 1.0f;
    }
    if (content_scale > 1.0f) {
        ImGui::GetStyle().ScaleAllSizes(content_scale);
    }
    SetContentScale(content_scale);

    ImFontConfig ui_cfg;
    ui_cfg.OversampleH = 3;
    ui_cfg.OversampleV = 2;
    ImFontConfig mono_cfg = ui_cfg;

    // platform::ResourceDir() returns the .app's Contents/Resources path
    // so a bundle launched from /Applications can find its fonts.
    // Prefixing here keeps the candidate lists uniform.
    const std::string& res_dir = platform::ResourceDir();
    std::vector<std::string> ui_font_candidates   = { res_dir + "assets/fonts/Roboto-Regular.ttf" };
    std::vector<std::string> mono_font_candidates = { res_dir + "assets/fonts/JetBrainsMono-Regular.ttf" };

    ImFont* ui_font = nullptr;
    for (const auto& path : ui_font_candidates) {
        ui_font = TryLoadFont(io, path, 17.0f * content_scale, &ui_cfg);
        if (ui_font) break;
    }
    ImFont* mono_font = nullptr;
    for (const auto& path : mono_font_candidates) {
        mono_font = TryLoadFont(io, path, 16.0f * content_scale, &mono_cfg);
        if (mono_font) break;
    }

    ImFont* title_font = nullptr;
    ImFontConfig title_cfg = ui_cfg;
    title_cfg.OversampleH = 2;
    title_cfg.OversampleV = 2;
    for (const auto& path : ui_font_candidates) {
        title_font = TryLoadFont(io, path, 48.0f * content_scale, &title_cfg);
        if (title_font) break;
    }

    // Full FA range at 96px blows past OpenGL's max texture size on many
    // GPUs, so narrow to codepoints we actually use.
    static constexpr ImWchar fa_ranges[] = {
        0xf15b, 0xf15b,   // ICON_FA_FILE
        0
    };
    ImFontConfig icon_cfg;
    icon_cfg.OversampleH = 2;
    icon_cfg.OversampleV = 2;
    icon_cfg.PixelSnapH  = true;
    ImFont* icon_font = TryLoadFont(io,
        res_dir + "assets/fonts/fa-solid-900.ttf", 96.0f * content_scale, &icon_cfg, fa_ranges);

    // Separate small-size FA atlas for toolbar icons — each glyph renders
    // at its native size without per-button font scaling.
    static constexpr ImWchar fa_small_ranges[] = {
        0xf013, 0xf013,   // ICON_FA_GEAR
        0xf078, 0xf078,   // ICON_FA_CHEVRON_DOWN
        0xf802, 0xf802,   // ICON_FA_FOLDER_TREE
        0
    };
    ImFontConfig icon_small_cfg;
    icon_small_cfg.OversampleH = 2;
    icon_small_cfg.OversampleV = 2;
    icon_small_cfg.PixelSnapH  = true;
    ImFont* icon_font_small = TryLoadFont(io,
        res_dir + "assets/fonts/fa-solid-900.ttf", 18.0f * content_scale, &icon_small_cfg, fa_small_ranges);

    if (!ui_font)    ui_font = io.Fonts->AddFontDefault();
    if (!mono_font)  mono_font = ui_font;
    if (!title_font) title_font = ui_font;

    io.FontDefault = ui_font;
    SetEditorFonts(ui_font, mono_font, title_font, icon_font, icon_font_small);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    // macOS Core Profile (set by FORWARD_COMPAT above) rejects GLSL
    // #version 130 — Apple supports 3.2+ only, mapping to GLSL 150.
    ImGui_ImplOpenGL3_Init("#version 150");

    // Restore the user's last-saved sliders / palette / toggles before the
    // first render so the first frame already reflects their preferences.
    LoadGuiPreferences();

    ImVec4 clear_color = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    bool startup_measured = false;

    while (!glfwWindowShouldClose(window)) {
        // Background throttle drops to ~15 FPS when unfocused; any queued
        // event wakes it instantly.
        if (BackgroundThrottle() &&
            !glfwGetWindowAttrib(window, GLFW_FOCUSED)) {
            glfwWaitEventsTimeout(1.0 / 15.0);
        } else {
            glfwPollEvents();
        }

        // Drain pending directories: replace the dropdown listing with
        // the latest folder's contents (only the last on a multi-drop).
        if (!ctx.pending_directories.empty()) {
            std::vector<std::string> dirs;
            dirs.swap(ctx.pending_directories);
            const std::string& chosen = dirs.back();
            std::vector<std::string> files;
            ExpandDirectoryInto(PathFromUtf8(chosen), files);
            ctx.directory_files = std::move(files);

            // PathBasename strips trailing slashes; fall back to the
            // whole path for the filesystem root (empty basename).
            std::string label = PathBasename(chosen);
            if (label.empty()) label = chosen;
            ctx.directory_label = std::move(label);

            if (ctx.state == AppState::StartScreen &&
                !ctx.directory_files.empty()) {
                ctx.state = AppState::HexView;
                ctx.load_error.clear();
            }
        }

        // Drain pending paths: each either focuses an already-open tab
        // or creates a new one. Errors are batched into ctx.load_error.
        // A hard cap keeps a careless drop-of-node_modules from
        // exhausting file handles or blowing up ImGui's tab bar.
        if (!ctx.pending_paths.empty()) {
            constexpr size_t kMaxOpenDocs = 200;

            std::vector<std::string> to_open;
            to_open.swap(ctx.pending_paths);

            std::string first_err;
            int  additional_err = 0;
            bool any_opened     = false;
            int  last_new_index = -1;
            int  skipped_cap    = 0;

            for (const std::string& path : to_open) {
                // Cheap cap check first — once full, the rest of the batch
                // is counted and skipped without per-path filesystem I/O.
                if (ctx.docs.size() >= kMaxOpenDocs) {
                    skipped_cap++;
                    continue;
                }
                std::string key = CanonicalKey(path);
                if (ctx.open_canonical.count(key) > 0) {
                    int existing = FindDocByCanonical(ctx.docs, key);
                    if (existing >= 0) {
                        ctx.active_doc = existing;
                        any_opened = true;
                    }
                    continue;
                }
                try {
                    OpenDocument od;
                    od.core = std::make_unique<HexEditorCore>(path);
                    if (ReadonlyDefault()) od.core->ForceReadOnly();
                    ctx.docs.push_back(std::move(od));
                    ctx.open_canonical.insert(key);
                    last_new_index = (int)ctx.docs.size() - 1;
                    any_opened = true;
                } catch (const std::exception& e) {
                    if (first_err.empty()) first_err = e.what();
                    else                   additional_err++;
                } catch (...) {
                    if (first_err.empty()) first_err = "unknown error opening file";
                    else                   additional_err++;
                }
            }

            if (last_new_index >= 0) ctx.active_doc = last_new_index;

            if (any_opened) {
                ctx.state = AppState::HexView;
                ctx.load_error.clear();
            } else if (ctx.docs.empty()) {
                ctx.state = AppState::StartScreen;
                ctx.load_error = BuildBatchError(first_err, additional_err);
            }

            // Single place that surfaces batch errors and the cap
            // message, regardless of which arm we landed in. The start-
            // screen path also seeds ctx.load_error for in-screen render.
            if (!first_err.empty() && ctx.state == AppState::HexView) {
                std::string msg = BuildBatchError(first_err, additional_err);
                std::fprintf(stderr, "Error opening: %s\n", msg.c_str());
                SetExternalStatus(msg, true);
            }
            if (skipped_cap > 0) {
                char buf[96];
                std::snprintf(buf, sizeof(buf),
                              "Tab limit reached (%zu); skipped %d more file%s",
                              kMaxOpenDocs, skipped_cap,
                              skipped_cap == 1 ? "" : "s");
                std::fprintf(stderr, "%s\n", buf);
                SetExternalStatus(buf, true);
            }

            ImGui::ClearActiveID();
        }

        // Title tracks the active tab. Single-source-of-truth target:
        // the active doc index when we have one, -1 otherwise.
        {
            const int target = TitleTargetIndex(ctx);
            if (target != ctx.last_titled_doc) {
                UpdateWindowTitle(ctx);
                ctx.last_titled_doc = target;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        int drag_over_state = 0;
        ctx.close_indices.clear();
        bool clear_directory = false;
        RenderHexEditorUI(ctx.state, &ctx.docs, &ctx.active_doc,
                          ctx.load_error.c_str(),
                          &ctx.pending_paths,
                          drag_over_state,
                          &ctx.close_indices,
                          &ctx.directory_files,
                          &ctx.directory_label,
                          &clear_directory,
                          &ctx.pending_directories);

        if (clear_directory) {
            ctx.directory_files.clear();
            ctx.directory_label.clear();
            if (ctx.docs.empty()) {
                ctx.state = AppState::StartScreen;
                ctx.active_doc = -1;
            }
        }

        // Consume tab close requests after render so indices still match.
        if (!ctx.close_indices.empty()) {
            std::sort(ctx.close_indices.begin(), ctx.close_indices.end(),
                      std::greater<int>());
            ctx.close_indices.erase(
                std::unique(ctx.close_indices.begin(), ctx.close_indices.end()),
                ctx.close_indices.end());
            for (int idx : ctx.close_indices) {
                if (idx < 0 || idx >= (int)ctx.docs.size()) continue;
                // Drop the canonical entry first — must read the filename
                // before the unique_ptr is destroyed.
                if (ctx.docs[idx].core) {
                    ctx.open_canonical.erase(
                        CanonicalKey(ctx.docs[idx].core->GetFilename()));
                }
                ctx.docs.erase(ctx.docs.begin() + idx);
                if (ctx.active_doc > idx) ctx.active_doc--;
            }
            if (ctx.docs.empty()) {
                ctx.active_doc = -1;
                // A loaded folder keeps the user in HexView with the
                // empty-state prompt; only revert to the start screen
                // when there's also nothing to pick from.
                if (ctx.directory_files.empty()) {
                    ctx.state = AppState::StartScreen;
                }
            } else {
                if (ctx.active_doc < 0) ctx.active_doc = 0;
                if (ctx.active_doc >= (int)ctx.docs.size())
                    ctx.active_doc = (int)ctx.docs.size() - 1;
            }
        }

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

    // Persist the user's current settings before tearing ImGui down. Done
    // here (rather than on every change) because the relevant fields all
    // mutate from inside ImGui callbacks — saving on shutdown captures the
    // final state with no per-widget plumbing.
    SaveGuiPreferences();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

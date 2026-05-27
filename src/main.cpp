#include "hex_editor_core.h"
#include "gui.h"
#include "app_state.h"
#include "path_utils.h"
#include "platform/asset_path.h"
#include "platform/macos_apple_events.h"

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

// stb_truetype hard-asserts on garbage input (incl. WOFF renamed to .ttf) — reject by magic.
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
    int                       last_titled_doc = -2;   // sentinel forces first-frame title set
    std::vector<std::string>  pending_paths;
    std::vector<int>          close_indices;
    std::vector<std::string>  pending_directories;
    // Files in the most-recently-loaded folder, alphabetized — feeds the tab-bar dropdown.
    std::vector<std::string>  directory_files;
    std::string               directory_label;
    // O(1) dedup during multi-file drops — pairwise filesystem::equivalent was O(N*M).
    std::unordered_set<std::string> open_canonical;
    std::string               load_error;
    GLFWwindow*               window = nullptr;
};

// Stable per-file key — weakly_canonical normalizes relative paths, case, and symlinks.
// On error, falls back to the original path (still better than nothing for dedup).
static std::string CanonicalKey(const std::string& utf8_path) {
    std::error_code ec;
    auto canon = std::filesystem::weakly_canonical(PathFromUtf8(utf8_path), ec);
    if (ec) return utf8_path;
    return PathToUtf8(canon);
}

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

// -1 stands in for non-HexView; call glfwSetWindowTitle only when this changes.
static int TitleTargetIndex(const AppContext& ctx) {
    return (ctx.state == AppState::HexView) ? ctx.active_doc : -1;
}

int main(int argc, char* argv[]) {
    const auto startup_begin = std::chrono::steady_clock::now();

    AppContext ctx;

    // CLI args take the same path as a multi-file drag-drop.
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
            ctx.pending_directories.push_back(a);
        } else if (std::filesystem::is_regular_file(status)) {
            ctx.pending_paths.push_back(a);
        }
    }

    // Must precede glfwInit(): glfwInit() calls [NSApp finishLaunching], which is
    // exactly when macOS dispatches kAEOpenDocuments events queued during launch
    // (the cold-launch "Open With → hxediter" path). Registering after that point
    // loses the very first event to NSApplication's default no-op handler.
    platform::RegisterAppleEventHandlers();

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    // Start maximized — 1280x720 is a postage stamp on 4K / ultrawide.
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

    // Launched from /Applications, cwd is "/" (read-only) — the default "imgui.ini" silently
    // fails to save. Redirect into Application Support. Static so io.IniFilename outlives frames.
    if (!platform::AppSupportDir().empty()) {
        static const std::string g_imgui_ini_path =
            platform::AppSupportDir() + "imgui.ini";
        io.IniFilename = g_imgui_ini_path.c_str();
    }

    // Cmd-based InputText shortcuts (Cmd+A/C/V/X, Option-arrow). Must be set before first frame.
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

    // Full FA range at 96px exceeds many GPUs' max texture size — narrow to used codepoints.
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

    // Small FA atlas for toolbar icons — native size, no per-button scaling.
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
    // macOS Core Profile rejects GLSL <150 — Apple supports 3.2+ only, mapping to GLSL 150.
    ImGui_ImplOpenGL3_Init("#version 150");

    LoadGuiPreferences();

    ImVec4 clear_color = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    bool startup_measured = false;

    while (!glfwWindowShouldClose(window)) {
        // Was glfwPollEvents in the focused branch — a tight loop where the
        // CPU never slept between frames, just queued ImGui work until vsync.
        // Fanless MacBook Air thermals notice. WaitEventsTimeout lets the
        // process sleep up to `timeout` seconds and wakes on any input, so an
        // idle editor settles to ~30 FPS instead of vsync's 60 while a
        // mouse-move / keypress still feels immediate.
        double timeout = 1.0 / 60.0;
        if (BackgroundThrottle() &&
            !glfwGetWindowAttrib(window, GLFW_FOCUSED)) {
            timeout = 1.0 / 15.0;  // unfocused throttle
        }
        glfwWaitEventsTimeout(timeout);

        // GLFW's macOS backend pumps NSApplication events inside glfwWaitEventsTimeout,
        // so any kAEOpenDocuments handler invocations have already populated the queue.
        platform::DrainPendingOpenPaths(ctx.pending_paths, ctx.pending_directories);

        // Drain pending directories — on a multi-drop, only the last one's listing is kept.
        if (!ctx.pending_directories.empty()) {
            std::vector<std::string> dirs;
            dirs.swap(ctx.pending_directories);
            const std::string& chosen = dirs.back();
            std::vector<std::string> files;
            DirectoryWalkResult walk =
                ExpandDirectoryInto(PathFromUtf8(chosen), files);
            ctx.directory_files = std::move(files);

            // Fall back to the whole path when basename is empty (filesystem root).
            std::string label = PathBasename(chosen);
            if (label.empty()) label = chosen;
            ctx.directory_label = std::move(label);

            if (ctx.state == AppState::StartScreen &&
                !ctx.directory_files.empty()) {
                ctx.state = AppState::HexView;
                ctx.load_error.clear();
            }

            if (walk.truncated_by_count || walk.truncated_by_time ||
                walk.step_errors > 0) {
                const char* why =
                    walk.truncated_by_count ? "file cap reached" :
                    walk.truncated_by_time  ? "time cap reached" :
                                              "errors during walk";
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                              "Directory listing partial (%s); showing %zu file%s",
                              why,
                              ctx.directory_files.size(),
                              ctx.directory_files.size() == 1 ? "" : "s");
                std::fprintf(stderr, "%s\n", buf);
                SetExternalStatus(buf, true);
            } else if (!walk.ok) {
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                              "Could not open folder: %s",
                              chosen.c_str());
                std::fprintf(stderr, "%s\n", buf);
                SetExternalStatus(buf, true);
            }
        }

        // Drain pending paths — focus existing tabs or open new ones. Cap keeps a careless
        // drop-of-node_modules from exhausting file handles.
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
                // Cap check first — skip the rest without per-path filesystem I/O.
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

        ctx.close_indices.clear();
        bool clear_directory = false;
        RenderHexEditorUI(ctx.state, &ctx.docs, &ctx.active_doc,
                          ctx.load_error.c_str(),
                          &ctx.pending_paths,
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

        // Process close requests after render so indices still match.
        if (!ctx.close_indices.empty()) {
            std::sort(ctx.close_indices.begin(), ctx.close_indices.end(),
                      std::greater<int>());
            ctx.close_indices.erase(
                std::unique(ctx.close_indices.begin(), ctx.close_indices.end()),
                ctx.close_indices.end());
            for (int idx : ctx.close_indices) {
                if (idx < 0 || idx >= (int)ctx.docs.size()) continue;
                // Read the filename before the unique_ptr is destroyed.
                if (ctx.docs[idx].core) {
                    ctx.open_canonical.erase(
                        CanonicalKey(ctx.docs[idx].core->GetFilename()));
                }
                ctx.docs.erase(ctx.docs.begin() + idx);
                if (ctx.active_doc > idx) ctx.active_doc--;
            }
            if (ctx.docs.empty()) {
                ctx.active_doc = -1;
                // Stay in HexView with the empty-state prompt as long as a folder is loaded.
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

    // Save on shutdown — captures final state without per-widget plumbing in ImGui callbacks.
    SaveGuiPreferences();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

#include "ui/settings_panel.h"
#include "ui/layout.h"
#include "ui/theme.h"
#include "updater.h"

#include "imgui.h"

#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <functional>

#ifndef APP_VERSION
#  define APP_VERSION "0.0.0"
#endif

namespace ui {

namespace {

/* Animated accordion: a button-toggled section whose body height eases
 * between 0 and the body's natural height with framerate-independent
 * exponential smoothing. State (open / current anim phase / measured
 * natural height) is owned by the caller's statics so each section can
 * persist independently across frames; the helper just consumes them.
 *
 * The body callback is invoked only at fully-open (anim==1) so the
 * height measurement doesn't see partially-rendered widgets; while
 * animating, a Dummy of the interpolated height drives the popup's
 * AlwaysAutoResize. Caller is responsible for any font Push/Pop around
 * the body (Appearance and Performance pop the surrounding mono font;
 * Updates renders in mono). */
void RenderAccordionSection(const char* label,
                            bool* open,
                            float* anim,
                            float* content_h,
                            const std::function<void()>& body) {
    const float dt     = ImGui::GetIO().DeltaTime;
    const float target = *open ? 1.0f : 0.0f;
    const float rate   = 8.0f;  /* ~200ms feel, framerate-independent */
    *anim += (target - *anim) * (1.0f - std::pow(0.1f, dt * rate));
    if (std::fabs(*anim - target) < 0.002f) *anim = target;

    /* Zero ItemSpacing BEFORE the button — ImGui bakes the spacing at
     * the end of each item using the style active at that moment.
     * Pushing after the button is too late (the row's bottom margin is
     * already committed). */
    const ImVec2 natural_item_spacing = ImGui::GetStyle().ItemSpacing;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
    if (ImGui::Button(label, ImVec2(-FLT_MIN, 0))) {
        *open = !*open;
    }
    ImGui::PopStyleVar();

    /* While animating, render only a Dummy of cur_h so the popup's
     * auto-size sees a smooth interpolation. At anim==1 we render the
     * real content and capture content_h from the natural height so the
     * Dummy → content handoff is sub-pixel invisible. */
    const float cur_h = (*content_h) * (*anim);
    if (cur_h > 0.5f) {
        if (*anim >= 1.0f) {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, natural_item_spacing);
            const float start_y = ImGui::GetCursorPosY();
            ImGui::Indent(4.0f);
            body();
            ImGui::Unindent(4.0f);
            const float end_y = ImGui::GetCursorPosY();
            *content_h = end_y - start_y;
            ImGui::PopStyleVar();
        } else {
            ImGui::Dummy(ImVec2(0, cur_h));
        }
    }

    ImGui::PopStyleVar(); /* ItemSpacing */
}

#ifdef _WIN32
void RenderUpdatesSection(GuiState& s) {
    (void)s;
    auto snap = updater::GetSnapshot();

    ImGui::TextDisabled("Current version:");
    ImGui::SameLine();
    ImGui::TextUnformatted(APP_VERSION);

    ImGui::Spacing();

    switch (snap.check) {
        case updater::CheckState::Idle:
            ImGui::TextUnformatted("Not checked yet.");
            break;
        case updater::CheckState::InProgress:
            ImGui::TextUnformatted("Checking GitHub...");
            break;
        case updater::CheckState::UpToDate:
            ImGui::Text("On latest (%s).", snap.latest_version.c_str());
            break;
        case updater::CheckState::UpdateAvailable:
            ImGui::TextDisabled("Latest:");
            ImGui::SameLine();
            ImGui::Text("%s", snap.latest_version.c_str());
            break;
        case updater::CheckState::NetworkError:
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.5f, 0.5f, 1.0f));
            ImGui::TextWrapped("Could not check: %s", snap.error_message.c_str());
            ImGui::PopStyleColor();
            break;
        case updater::CheckState::ParseError:
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.5f, 0.5f, 1.0f));
            ImGui::TextWrapped("Bad response: %s", snap.error_message.c_str());
            ImGui::PopStyleColor();
            break;
    }

    ImGui::Spacing();

    const bool checking    = (snap.check    == updater::CheckState::InProgress);
    const bool downloading = (snap.download == updater::DownloadState::InProgress);
    const bool download_done = (snap.download == updater::DownloadState::Complete);
    const bool busy        = checking || downloading;

    if (ImGui::Button("Check for updates", ImVec2(-FLT_MIN, 0))) {
        if (!busy) updater::StartCheck();
    }

    if (snap.check == updater::CheckState::UpdateAvailable && !download_done) {
        if (downloading) {
            ImGui::BeginDisabled();
            ImGui::Button("Downloading...", ImVec2(-FLT_MIN, 0));
            ImGui::EndDisabled();
        } else {
            if (ImGui::Button("Install and restart", ImVec2(-FLT_MIN, 0))) {
                updater::StartDownload();
            }
        }
    }

    if (downloading) {
        ImGui::Spacing();
        float frac = 0.0f;
        if (snap.bytes_total > 0) {
            frac = (float)snap.bytes_received / (float)snap.bytes_total;
            if (frac < 0.0f) frac = 0.0f;
            if (frac > 1.0f) frac = 1.0f;
        }
        char label[64];
        if (snap.bytes_total > 0) {
            std::snprintf(label, sizeof(label), "%.1f / %.1f MB",
                snap.bytes_received / (1024.0 * 1024.0),
                snap.bytes_total    / (1024.0 * 1024.0));
        } else {
            std::snprintf(label, sizeof(label), "%.1f MB",
                snap.bytes_received / (1024.0 * 1024.0));
        }
        ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0), label);
    } else if (snap.download == updater::DownloadState::Failed) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.5f, 0.5f, 1.0f));
        const char* msg = !snap.launch_error.empty()
            ? snap.launch_error.c_str()
            : snap.error_message.c_str();
        ImGui::TextWrapped("%s", msg);
        ImGui::PopStyleColor();
    }

    /* Don't write the installer path through any out-param here. The
     * main loop polls updater::ConsumeInstallerPath() once per frame
     * and is the single owner of the launch handoff — duplicating
     * that write here would mean two paths to the same data and a
     * frame-ordering accident if they ever diverge. We just close
     * the popup so the user knows the click took, and let main pick
     * up the path on the same frame (it polls regardless of popup
     * visibility). */
    if (download_done && !snap.installer_path.empty()) {
        ImGui::CloseCurrentPopup();
    }
}
#endif /* _WIN32 */

void RenderAppearanceSection(GuiState& s) {
    ImGui::TextDisabled("Theme:");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##theme", theme::Name(s.palette))) {
        for (int i = 0; i < (int)GuiState::PAL_COUNT; ++i) {
            auto p = (GuiState::Palette)i;
            const bool selected = (s.palette == p);
            if (ImGui::Selectable(theme::Name(p), selected)) {
                s.palette = p;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();

    ImGui::TextDisabled("Font size:");
    const float reset_w  = ImGui::CalcTextSize("Reset").x +
                           ImGui::GetStyle().FramePadding.x * 2.0f;
    const float inner    = ImGui::GetStyle().ItemInnerSpacing.x;
    const float slider_w = ImGui::GetContentRegionAvail().x - reset_w - inner;

    float pct = s.font_scale * 100.0f;
    ImGui::SetNextItemWidth(slider_w);
    if (ImGui::SliderFloat("##fontscale", &pct,
                           layout::kFontScaleMin * 100.0f,
                           layout::kFontScaleMax * 100.0f,
                           "%.0f%%")) {
        float v = pct / 100.0f;
        if (v < layout::kFontScaleMin) v = layout::kFontScaleMin;
        if (v > layout::kFontScaleMax) v = layout::kFontScaleMax;
        s.font_scale = v;
    }
    ImGui::SameLine(0.0f, inner);
    if (ImGui::Button("Reset")) {
        s.font_scale = 1.0f;
    }
}

void RenderPerformanceSection(GuiState& s) {
    ImGui::Checkbox("Throttle when in background", &s.background_throttle);
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Drops to ~15 FPS when the window loses focus; "
                       "any event wakes it instantly.");
    ImGui::PopStyleColor();
}

} /* anonymous namespace */

void RenderSettingsPopup(GuiState& s) {
#ifdef _WIN32
    /* Per-frame ring buffer of the Updates animation state, surfaced via a
     * separate log window when the Debug checkbox is on. Only the Windows
     * build has an Updates accordion to instrument, so the entire ring
     * buffer + diagnostics window is gated out elsewhere. */
    struct AnimMetric {
        int   frame;
        float dt;
        float anim;
        float cur_h;
        float popup_h;
        char  shape;
    };
    static AnimMetric s_metrics[96] = {};
    static int        s_metrics_head = 0;
    static bool       s_show_debug   = false;

    if (s_show_debug) {
    /* Size cap + post-Begin position clamp so the window (and its close-X)
     * stay reachable regardless of where the user drags it. */
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSizeConstraints(ImVec2(240, 160), vp->WorkSize);
    ImGui::SetNextWindowSize(ImVec2(560, 560), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Updates Animation Diagnostics", &s_show_debug)) {
        ImVec2 wpos  = ImGui::GetWindowPos();
        ImVec2 wsize = ImGui::GetWindowSize();
        ImVec2 lo    = vp->WorkPos;
        ImVec2 hi    = ImVec2(lo.x + vp->WorkSize.x - wsize.x,
                              lo.y + vp->WorkSize.y - wsize.y);
        ImVec2 clamped(
            wpos.x < lo.x ? lo.x : (wpos.x > hi.x ? hi.x : wpos.x),
            wpos.y < lo.y ? lo.y : (wpos.y > hi.y ? hi.y : wpos.y));
        if (clamped.x != wpos.x || clamped.y != wpos.y) {
            ImGui::SetWindowPos(clamped);
        }
        /* Read-only InputTextMultiline gives us free selection/copy. */
        static char s_log_text[8192];
        int pos = 0;
        for (int i = 0; i < 96; i++) {
            int idx = (s_metrics_head + i) % 96;
            const AnimMetric& m = s_metrics[idx];
            if (m.frame == 0) continue;
            int n = std::snprintf(s_log_text + pos,
                                  (int)sizeof(s_log_text) - pos,
                                  "f=%d dt=%.4f anim=%.5f cur_h=%.3f total_h=%.2f shape=%c\n",
                                  m.frame, m.dt, m.anim, m.cur_h,
                                  m.popup_h, m.shape);
            if (n <= 0 || pos + n >= (int)sizeof(s_log_text)) break;
            pos += n;
        }
        s_log_text[pos < (int)sizeof(s_log_text) ? pos
                                                  : (int)sizeof(s_log_text) - 1] = '\0';

        if (ImGui::Button("Clear")) {
            for (auto& m : s_metrics) m.frame = 0;
            s_metrics_head = 0;
            s_log_text[0] = '\0';
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy all")) {
            ImGui::SetClipboardText(s_log_text);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("A=body rendered, B=gate off");
        ImGui::Separator();

        ImGui::InputTextMultiline("##log", s_log_text, sizeof(s_log_text),
                                  ImVec2(-1.0f, -1.0f),
                                  ImGuiInputTextFlags_ReadOnly);
    }
    ImGui::End();
    }  /* if (s_show_debug) */
#endif /* _WIN32 */

    /* Floating tray below the gear. Non-modal so the editor stays
     * interactive; all literals scale with content_scale for HiDPI. */
    const float kPanelW   = 260.0f * s.content_scale;
    const float kTopPad   = 34.0f  * s.content_scale;
    const float kRightPad = 8.0f   * s.content_scale;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 anchor(vp->Pos.x + vp->Size.x - kPanelW - kRightPad,
                  vp->Pos.y + kTopPad);
    ImGui::SetNextWindowPos(anchor, ImGuiCond_Always);
    /* Width locked, height auto-grows with the Updates accordion. */
    ImGui::SetNextWindowSizeConstraints(ImVec2(kPanelW, 0.0f),
                                        ImVec2(kPanelW, FLT_MAX));

    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f * s.content_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(10.0f * s.content_scale, 10.0f * s.content_scale));

    if (!ImGui::BeginPopup("Settings##settings",
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoMove)) {
        ImGui::PopStyleVar(2);
        return;
    }

    if (s.mono_font) ImGui::PushFont(s.mono_font);

    /* Three accordions, identical animation/measurement scaffold; the
     * helper owns the math and renders the body via a callback. Body
     * font is the caller's call: Appearance and Performance read better
     * in the UI font, so they pop+push the surrounding mono_font;
     * Updates stays in mono. */
    {
        static bool  open      = false;
        static float anim      = 0.0f;
        static float content_h = 80.0f;
        RenderAccordionSection("Appearance##header", &open, &anim, &content_h, [&] {
            if (s.mono_font) ImGui::PopFont();
            RenderAppearanceSection(s);
            if (s.mono_font) ImGui::PushFont(s.mono_font);
        });
    }

    ImGui::Spacing();

    {
        static bool  open      = false;
        static float anim      = 0.0f;
        static float content_h = 60.0f;
        RenderAccordionSection("Performance##header", &open, &anim, &content_h, [&] {
            if (s.mono_font) ImGui::PopFont();
            RenderPerformanceSection(s);
            if (s.mono_font) ImGui::PushFont(s.mono_font);
        });
    }

#ifdef _WIN32
    /* The auto-updater is WinHTTP + ShellExecute("runas") and only
     * compiles on Windows. macOS / Linux ship without auto-update in v1
     * — hide the entire Updates section and the animation-debug toggle
     * here so the popup doesn't expose UI for a feature that wouldn't
     * work. The non-Windows updater_stub provides no-op symbols so
     * RenderUpdatesSection itself would compile, but rendering a
     * permanently-idle "Check for updates" button confuses users. */
    ImGui::Spacing();

    /* Updates needs anim/content_h visible to the diagnostics block
     * below, so the statics live at this scope rather than inside an
     * inner block like the other two. */
    static bool  updates_open      = false;
    static float updates_anim      = 0.0f;
    static float updates_content_h = 140.0f;
    RenderAccordionSection("Updates##header",
                           &updates_open, &updates_anim, &updates_content_h, [&] {
        RenderUpdatesSection(s);
    });

    ImGui::Spacing();
    ImGui::Checkbox("Debug", &s_show_debug);

    /* Record only on change so steady-state frames don't flood the log. */
    {
        static float s_last_anim = -1.0f;
        if (updates_anim != s_last_anim) {
            AnimMetric& m = s_metrics[s_metrics_head];
            m.frame   = ImGui::GetFrameCount();
            m.dt      = ImGui::GetIO().DeltaTime;
            m.anim    = updates_anim;
            m.cur_h   = updates_content_h * updates_anim;
            m.popup_h = ImGui::GetWindowHeight();
            m.shape   = (m.cur_h > 0.5f) ? 'A' : 'B';
            s_metrics_head = (s_metrics_head + 1) % 96;
            s_last_anim = updates_anim;
        }
    }
#endif

    if (s.mono_font) ImGui::PopFont();
    ImGui::EndPopup();
    ImGui::PopStyleVar(2);
}

} /* namespace ui */

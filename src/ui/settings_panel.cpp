#include "ui/settings_panel.h"
#include "ui/layout.h"
#include "ui/theme.h"
#include "updater.h"

#include "imgui.h"

#include <cfloat>
#include <cmath>
#include <cstdlib>

#ifndef APP_VERSION
#  define APP_VERSION "0.0.0"
#endif

namespace ui {

namespace {

void RenderUpdatesSection(GuiState& s, std::string* out_installer_to_launch) {
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
        ImGui::TextWrapped("%s", snap.error_message.c_str());
        ImGui::PopStyleColor();
    }

    /* Download finished and verified — write the path back for the main
     * loop to pick up and close the popup. */
    if (download_done && !snap.installer_path.empty() && out_installer_to_launch) {
        *out_installer_to_launch = snap.installer_path;
        ImGui::CloseCurrentPopup();
    }
}

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

void RenderSettingsPopup(GuiState& s, std::string* out_installer_to_launch) {
    /* Debug-mode instrumentation: records per-frame state of the Updates
     * animation into a ring buffer and renders a scrolling, selectable log
     * in a separate window. Gated on s_show_debug, toggled via the Debug
     * checkbox at the bottom of this popup. Recording is always on (the
     * write is cheap) so there's always fresh history when you open the
     * window. */
    struct ScuffMetric {
        int   frame;
        float dt;
        float anim;
        float cur_h;
        float popup_h;
        char  shape;
    };
    static ScuffMetric s_metrics[96] = {};
    static int         s_metrics_head = 0;
    static bool        s_show_debug   = false;

    if (s_show_debug) {
    /* Cap the max size to the main viewport so the window can never grow
     * larger than what's visible, then clamp its position after Begin so
     * dragging can't push it past the edges. Together these keep the X
     * close button reachable regardless of where the user puts it. */
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSizeConstraints(ImVec2(240, 160), vp->WorkSize);
    ImGui::SetNextWindowSize(ImVec2(560, 560), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Updates Scuff Debug", &s_show_debug)) {
        /* Clamp inside the main viewport. Must happen after Begin so we
         * can read the current window size. */
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
        /* Build a plain-text rendering of the ring buffer every frame. Using
         * an InputTextMultiline (read-only) instead of a Text loop makes the
         * log natively selectable and copyable. */
        static char s_log_text[8192];
        int pos = 0;
        for (int i = 0; i < 96; i++) {
            int idx = (s_metrics_head + i) % 96;
            const ScuffMetric& m = s_metrics[idx];
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

    /* Frameless floating tray pinned to the top-right, just below the
     * toolbar row where the gear icon lives. BeginPopup (non-modal) means
     * no backdrop dim and auto-close on any outside click — the rest of
     * the editor stays fully interactive while the panel is open. */
    constexpr float kPanelW   = 260.0f;
    constexpr float kTopPad   = 34.0f;   /* clears toolbar + separator */
    constexpr float kRightPad = 8.0f;    /* matches gear button inset */

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 anchor(vp->Pos.x + vp->Size.x - kPanelW - kRightPad,
                  vp->Pos.y + kTopPad);
    ImGui::SetNextWindowPos(anchor, ImGuiCond_Always);
    /* Width locked at kPanelW; height auto-grows with the Updates accordion.
     * Locking both min and max width breaks the AlwaysAutoResize chicken-
     * and-egg with the child BeginChild (whose width is derived from the
     * window's). */
    ImGui::SetNextWindowSizeConstraints(ImVec2(kPanelW, 0.0f),
                                        ImVec2(kPanelW, FLT_MAX));

    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));

    if (!ImGui::BeginPopup("Settings##settings",
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoMove)) {
        ImGui::PopStyleVar(2);
        return;
    }

    if (s.mono_font) ImGui::PushFont(s.mono_font);

    /* Animated "Appearance" section. Mirrors the Updates accordion below;
     * its own statics calibrate a different natural height. Mono font is
     * temporarily popped inside the body so prose labels and widget text
     * render in Roboto. */
    {
        static bool  appearance_open      = false;
        static float appearance_anim      = 0.0f;
        static float appearance_content_h = 80.0f;

        const float dt     = ImGui::GetIO().DeltaTime;
        const float target = appearance_open ? 1.0f : 0.0f;
        const float rate   = 8.0f;
        appearance_anim += (target - appearance_anim) * (1.0f - std::pow(0.1f, dt * rate));
        if (std::fabs(appearance_anim - target) < 0.002f) appearance_anim = target;

        const ImVec2 natural_item_spacing = ImGui::GetStyle().ItemSpacing;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
        if (ImGui::Button("Appearance##header", ImVec2(-FLT_MIN, 0))) {
            appearance_open = !appearance_open;
        }
        ImGui::PopStyleVar();

        const float cur_h = appearance_content_h * appearance_anim;
        if (cur_h > 0.5f) {
            if (appearance_anim >= 1.0f) {
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, natural_item_spacing);
                const float start_y = ImGui::GetCursorPosY();
                ImGui::Indent(4.0f);
                if (s.mono_font) ImGui::PopFont();
                RenderAppearanceSection(s);
                if (s.mono_font) ImGui::PushFont(s.mono_font);
                ImGui::Unindent(4.0f);
                const float end_y = ImGui::GetCursorPosY();
                appearance_content_h = end_y - start_y;
                ImGui::PopStyleVar();
            } else {
                ImGui::Dummy(ImVec2(0, cur_h));
            }
        }

        ImGui::PopStyleVar(); /* ItemSpacing */
    }

    ImGui::Spacing();

    /* Animated "Performance" section. Mirrors the Appearance accordion
     * pattern; currently hosts the single background-throttle toggle. */
    {
        static bool  perf_open      = false;
        static float perf_anim      = 0.0f;
        static float perf_content_h = 60.0f;

        const float dt     = ImGui::GetIO().DeltaTime;
        const float target = perf_open ? 1.0f : 0.0f;
        const float rate   = 8.0f;
        perf_anim += (target - perf_anim) * (1.0f - std::pow(0.1f, dt * rate));
        if (std::fabs(perf_anim - target) < 0.002f) perf_anim = target;

        const ImVec2 natural_item_spacing = ImGui::GetStyle().ItemSpacing;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
        if (ImGui::Button("Performance##header", ImVec2(-FLT_MIN, 0))) {
            perf_open = !perf_open;
        }
        ImGui::PopStyleVar();

        const float cur_h = perf_content_h * perf_anim;
        if (cur_h > 0.5f) {
            if (perf_anim >= 1.0f) {
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, natural_item_spacing);
                const float start_y = ImGui::GetCursorPosY();
                ImGui::Indent(4.0f);
                if (s.mono_font) ImGui::PopFont();
                RenderPerformanceSection(s);
                if (s.mono_font) ImGui::PushFont(s.mono_font);
                ImGui::Unindent(4.0f);
                const float end_y = ImGui::GetCursorPosY();
                perf_content_h = end_y - start_y;
                ImGui::PopStyleVar();
            } else {
                ImGui::Dummy(ImVec2(0, cur_h));
            }
        }

        ImGui::PopStyleVar(); /* ItemSpacing */
    }

    ImGui::Spacing();

    /* Animated "Updates" section. Starts collapsed; state persists across
     * popup open/close within a session. The anim float drives both
     * alpha and child-height so the body slides in while fading, and the
     * outer popup (AlwaysAutoResize) grows with it. */
    static bool  updates_open = false;
    static float updates_anim = 0.0f;
    static float content_h    = 140.0f;   /* calibrated on first full open */

    const float dt     = ImGui::GetIO().DeltaTime;
    const float target = updates_open ? 1.0f : 0.0f;
    /* Exponential smoothing, framerate-independent. rate=8 → ~200ms feel. */
    const float rate = 8.0f;
    updates_anim += (target - updates_anim) * (1.0f - std::pow(0.1f, dt * rate));
    if (std::fabs(updates_anim - target) < 0.002f) updates_anim = target;

    /* Zero ItemSpacing BEFORE the button. ImGui's ItemSize bakes
     * ItemSpacing.y into the cursor advance at the END of each item using
     * the style active at that moment — so the default ~4 px between
     * button and the next item is reserved when the BUTTON finishes, not
     * when the body starts. Pushing after the button was too late. */
    const ImVec2 natural_item_spacing = ImGui::GetStyle().ItemSpacing;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
    if (ImGui::Button("Updates##header", ImVec2(-FLT_MIN, 0))) {
        updates_open = !updates_open;
    }
    ImGui::PopStyleVar();

    /* Body layout: two modes.
     *
     *   Animating  (cur_h > 0.5 && anim < 1.0): plain Dummy of cur_h.
     *       Nobody can read or click the content during the ~200 ms
     *       transition, so we don't pay the layout-isolation cost that
     *       clipping/BeginChild would demand. Popup auto-sizes to
     *       padding + button + cur_h + padding.
     *
     *   Fully open (anim == 1.0): real content at natural height. Popup
     *       auto-sizes to padding + button + content_natural + padding.
     *       content_h is calibrated from the measured natural height so
     *       that cur_h at anim=0.998 is within sub-pixel of content_h —
     *       the transition between Dummy(cur_h) and content(natural) is
     *       invisible at the snap-to-1.0 boundary.
     *
     * The gate on cur_h > 0.5 still guards the Shape A ↔ Shape B handoff
     * at the end of collapse — that scuff fix is untouched by this. */
    const float cur_h = content_h * updates_anim;
    if (cur_h > 0.5f) {
        if (updates_anim >= 1.0f) {
            /* Restore natural ItemSpacing for the updates section's own
             * widgets so they keep their normal vertical rhythm. */
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, natural_item_spacing);
            const float start_y = ImGui::GetCursorPosY();
            ImGui::Indent(4.0f);
            RenderUpdatesSection(s, out_installer_to_launch);
            ImGui::Unindent(4.0f);
            const float end_y = ImGui::GetCursorPosY();
            /* Calibrate to the exact measured height so cur_h = content_h
             * at full open matches the content's natural bottom. */
            content_h = end_y - start_y;
            ImGui::PopStyleVar();
        } else {
            ImGui::Dummy(ImVec2(0, cur_h));
        }
    }

    ImGui::PopStyleVar(); /* ItemSpacing */

    /* Debug mode toggle. When on, a separate selectable-log window appears
     * (see top of this function) showing per-frame animation metrics. A
     * small Spacing() first so the checkbox doesn't butt directly against
     * the Updates body, which has zero trailing item spacing. */
    ImGui::Spacing();
    ImGui::Checkbox("Debug", &s_show_debug);

    /* === DEBUG: record metric for this frame, but only when updates_anim
     * actually changed — so steady-state frames don't drown the log and the
     * collapse shows up as a clean ~20-line transcript. popup_h is queried
     * via GetWindowHeight inside the popup so it reflects what ImGui has
     * committed as this popup's outer height for the current frame. */
    {
        static float s_last_anim = -1.0f;
        if (updates_anim != s_last_anim) {
            ScuffMetric& m = s_metrics[s_metrics_head];
            m.frame   = ImGui::GetFrameCount();
            m.dt      = ImGui::GetIO().DeltaTime;
            m.anim    = updates_anim;
            m.cur_h   = content_h * updates_anim;
            m.popup_h = ImGui::GetWindowHeight();
            m.shape   = (m.cur_h > 0.5f) ? 'A' : 'B';
            s_metrics_head = (s_metrics_head + 1) % 96;
            s_last_anim = updates_anim;
        }
    }
    /* === END DEBUG === */

    if (s.mono_font) ImGui::PopFont();
    ImGui::EndPopup();
    ImGui::PopStyleVar(2);
}

} /* namespace ui */

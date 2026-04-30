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
        const char* msg = !snap.launch_error.empty()
            ? snap.launch_error.c_str()
            : snap.error_message.c_str();
        ImGui::TextWrapped("%s", msg);
        ImGui::PopStyleColor();
    }

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
    /* Per-frame ring buffer of the Updates animation state, surfaced via a
     * separate log window when the Debug checkbox is on. */
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
    /* Size cap + post-Begin position clamp so the window (and its close-X)
     * stay reachable regardless of where the user drags it. */
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSizeConstraints(ImVec2(240, 160), vp->WorkSize);
    ImGui::SetNextWindowSize(ImVec2(560, 560), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Updates Scuff Debug", &s_show_debug)) {
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

    /* Accordion pattern shared with Performance/Updates below. */
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

    static bool  updates_open = false;
    static float updates_anim = 0.0f;
    static float content_h    = 140.0f;

    const float dt     = ImGui::GetIO().DeltaTime;
    const float target = updates_open ? 1.0f : 0.0f;
    /* rate=8 → ~200 ms feel, framerate-independent. */
    const float rate = 8.0f;
    updates_anim += (target - updates_anim) * (1.0f - std::pow(0.1f, dt * rate));
    if (std::fabs(updates_anim - target) < 0.002f) updates_anim = target;

    /* Zero ItemSpacing BEFORE the button — ImGui bakes the spacing at the
     * end of each item using the style active at that moment. Pushing
     * after the button was too late. */
    const ImVec2 natural_item_spacing = ImGui::GetStyle().ItemSpacing;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
    if (ImGui::Button("Updates##header", ImVec2(-FLT_MIN, 0))) {
        updates_open = !updates_open;
    }
    ImGui::PopStyleVar();

    /* While animating, a plain Dummy of cur_h drives the popup's auto-size;
     * at anim==1 we render the real content and calibrate content_h from
     * the measured natural height so the Dummy→content handoff is
     * sub-pixel invisible. */
    const float cur_h = content_h * updates_anim;
    if (cur_h > 0.5f) {
        if (updates_anim >= 1.0f) {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, natural_item_spacing);
            const float start_y = ImGui::GetCursorPosY();
            ImGui::Indent(4.0f);
            RenderUpdatesSection(s, out_installer_to_launch);
            ImGui::Unindent(4.0f);
            const float end_y = ImGui::GetCursorPosY();
            content_h = end_y - start_y;
            ImGui::PopStyleVar();
        } else {
            ImGui::Dummy(ImVec2(0, cur_h));
        }
    }

    ImGui::PopStyleVar(); /* ItemSpacing */

    ImGui::Spacing();
    ImGui::Checkbox("Debug", &s_show_debug);

    /* Record only on change so steady-state frames don't flood the log. */
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

    if (s.mono_font) ImGui::PopFont();
    ImGui::EndPopup();
    ImGui::PopStyleVar(2);
}

} /* namespace ui */

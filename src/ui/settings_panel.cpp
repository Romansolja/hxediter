#include "ui/settings_panel.h"
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
            ImGui::TextUnformatted("Checking GitHub for updates...");
            break;
        case updater::CheckState::UpToDate:
            ImGui::Text("You're on the latest version (%s).", snap.latest_version.c_str());
            break;
        case updater::CheckState::UpdateAvailable:
            ImGui::TextDisabled("Latest version:");
            ImGui::SameLine();
            ImGui::Text("%s  (update available)", snap.latest_version.c_str());
            break;
        case updater::CheckState::NetworkError:
            ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.5f, 1.0f),
                "Could not check: %s", snap.error_message.c_str());
            break;
        case updater::CheckState::ParseError:
            ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.5f, 1.0f),
                "Unexpected response from GitHub: %s", snap.error_message.c_str());
            break;
    }

    ImGui::Spacing();

    const bool checking    = (snap.check    == updater::CheckState::InProgress);
    const bool downloading = (snap.download == updater::DownloadState::InProgress);
    const bool download_done = (snap.download == updater::DownloadState::Complete);
    const bool busy        = checking || downloading;

    /* Check-now button — always available unless already checking. */
    if (ImGui::Button("Check for updates", ImVec2(180, 0))) {
        if (!busy) updater::StartCheck();
    }
    if (checking) {
        ImGui::SameLine();
        ImGui::TextDisabled("...");
    }

    if (snap.check == updater::CheckState::UpdateAvailable && !download_done) {
        ImGui::SameLine();
        if (downloading) {
            ImGui::BeginDisabled();
            ImGui::Button("Downloading...", ImVec2(180, 0));
            ImGui::EndDisabled();
        } else {
            if (ImGui::Button("Install and restart", ImVec2(180, 0))) {
                updater::StartDownload();
            }
        }
    }

    /* Download progress and errors. */
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
        ImGui::ProgressBar(frac, ImVec2(370, 0), label);
    } else if (snap.download == updater::DownloadState::Failed) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.5f, 1.0f),
            "%s", snap.error_message.c_str());
    }

    /* Download finished and verified — write the path back for the main
     * loop to pick up and close the popup. */
    if (download_done && !snap.installer_path.empty() && out_installer_to_launch) {
        *out_installer_to_launch = snap.installer_path;
        ImGui::CloseCurrentPopup();
    }
}

} /* anonymous namespace */

void RenderSettingsPopup(GuiState& s, std::string* out_installer_to_launch) {
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    /* Pin a min width so content inside the animated BeginChild doesn't
     * clip. AlwaysAutoResize alone can't resolve this because the child's
     * width is derived from the popup's width, which is derived from
     * content width — chicken-and-egg. A floor breaks the cycle. */
    ImGui::SetNextWindowSizeConstraints(ImVec2(420.0f, 0.0f),
                                        ImVec2(FLT_MAX, FLT_MAX));

    if (!ImGui::BeginPopupModal("Settings##settings", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }

    /* Distinct font for the Settings panel — monospaced leans into the
     * hex-editor aesthetic. Falls back to default if mono isn't loaded. */
    if (s.mono_font) ImGui::PushFont(s.mono_font);

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

    /* Header button: full-width, centered label, no triangle (it's a
     * plain Button, not a CollapsingHeader). */
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
    if (ImGui::Button("Updates##header", ImVec2(-FLT_MIN, 0))) {
        updates_open = !updates_open;
    }
    ImGui::PopStyleVar();

    if (updates_anim > 0.002f) {
        float cur_h = content_h * updates_anim;
        if (cur_h < 1.0f) cur_h = 1.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, updates_anim);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("##updates_body",
                          ImVec2(-FLT_MIN, cur_h),
                          false,
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
        const float start_y = ImGui::GetCursorPosY();
        ImGui::Indent(6.0f);
        RenderUpdatesSection(s, out_installer_to_launch);
        ImGui::Unindent(6.0f);
        const float end_y = ImGui::GetCursorPosY();
        if (updates_anim > 0.98f) {
            content_h = (end_y - start_y) + 6.0f;
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Close", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
    }

    if (s.mono_font) ImGui::PopFont();
    ImGui::EndPopup();
}

} /* namespace ui */

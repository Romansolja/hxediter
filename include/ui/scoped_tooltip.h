#pragma once

#include "imgui.h"

namespace ui {

// RAII guard for the editor's standard rounded/padded tooltip frame. Caller writes the body.
struct ScopedStyledTooltip {
    ScopedStyledTooltip() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
        ImGui::BeginTooltip();
    }
    ~ScopedStyledTooltip() {
        ImGui::EndTooltip();
        ImGui::PopStyleVar(2);
    }
    ScopedStyledTooltip(const ScopedStyledTooltip&)            = delete;
    ScopedStyledTooltip& operator=(const ScopedStyledTooltip&) = delete;
};

}

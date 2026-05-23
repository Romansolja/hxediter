#pragma once

#include "imgui.h"

namespace ui {

// Begin a tooltip with the editor's standard rounded/padded frame, scoped
// to the enclosing block. The caller writes the body between construction
// and end of scope (with or without PushFont, with or without printf-style
// formatting) — the guard handles only the outer chrome so each site can
// keep its own content rendering.
//
//   if (ImGui::IsItemHovered()) {
//       ui::ScopedStyledTooltip tip;
//       ImGui::TextUnformatted("Hello");
//   }
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

} // namespace ui

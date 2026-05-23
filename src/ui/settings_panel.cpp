#include "ui/settings_panel.h"
#include "ui/layout.h"
#include "ui/theme.h"

#include "imgui.h"

#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <functional>

namespace ui {

namespace {

// Animated accordion section. Body is invoked only at fully-open
// (anim == 1) so height measurement doesn't see partially-rendered
// widgets; while animating, a Dummy of the interpolated height drives the
// popup's AlwaysAutoResize. State (open/anim phase/measured natural
// height) is owned by the caller's statics — the helper just consumes them.
void RenderAccordionSection(const char* label,
                            bool* open,
                            float* anim,
                            float* content_h,
                            const std::function<void()>& body) {
    const float dt     = ImGui::GetIO().DeltaTime;
    const float target = *open ? 1.0f : 0.0f;
    const float rate   = 8.0f;  // ~200ms feel, framerate-independent
    *anim += (target - *anim) * (1.0f - std::pow(0.1f, dt * rate));
    if (std::fabs(*anim - target) < 0.002f) *anim = target;

    // Zero ItemSpacing BEFORE the button. ImGui bakes spacing at item
    // end using the style active at that moment; pushing after the
    // button is too late (the row's bottom margin is already committed).
    const ImVec2 natural_item_spacing = ImGui::GetStyle().ItemSpacing;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
    if (ImGui::Button(label, ImVec2(-FLT_MIN, 0))) {
        *open = !*open;
    }
    ImGui::PopStyleVar();

    // While animating, render only a Dummy of cur_h so the popup's
    // auto-size sees a smooth interpolation. At anim==1 we render the
    // real content and recapture content_h, so the Dummy → content
    // handoff is sub-pixel invisible.
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

    ImGui::PopStyleVar(); // ItemSpacing
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

} // anonymous namespace

void RenderSettingsPopup(GuiState& s) {

    // Floating tray below the gear. Non-modal so the editor stays
    // interactive; all literals scale with content_scale for HiDPI.
    const float kPanelW   = 260.0f * s.content_scale;
    const float kTopPad   = 34.0f  * s.content_scale;
    const float kRightPad = 8.0f   * s.content_scale;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 anchor(vp->Pos.x + vp->Size.x - kPanelW - kRightPad,
                  vp->Pos.y + kTopPad);
    ImGui::SetNextWindowPos(anchor, ImGuiCond_Always);
    // Width locked, height auto-grows with the open accordions.
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

    // Two accordions on the same animation scaffold. Body font is the
    // caller's call: Appearance and Performance read better in the UI
    // font, so each callback pops+pushes the surrounding mono_font.
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


    if (s.mono_font) ImGui::PopFont();
    ImGui::EndPopup();
    ImGui::PopStyleVar(2);
}

} // namespace ui

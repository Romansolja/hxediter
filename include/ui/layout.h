/* layout.h — Spacing, padding, rounding, and timing constants for the
 * editor UI. Values moved out of gui.cpp so renderers can reference
 * named constants instead of scattered magic numbers. */

#pragma once

namespace ui::layout {

/* --- Hex grid spacing (expressed as multiples of character width) */
constexpr float kGapByteMul     = 0.7f;   /* gap between every byte */
constexpr float kGapQuadMul     = 0.7f;   /* extra gap every 4 bytes */
constexpr float kGapOctetMul    = 1.2f;   /* extra gap every 8 bytes */
constexpr float kOffsetGapMul   = 2.0f;   /* gap after offset column */
constexpr float kAsciiGapMul    = 2.0f;   /* gap before ASCII column */

/* --- Header & grid body */
constexpr float kHeaderExtraH   = 8.0f;
constexpr float kCaretRounding  = 3.0f;
constexpr float kCaretPulseHz   = 5.0f;

/* --- Toolbar */
constexpr float kToolbarGroupGap  = 28.0f;
constexpr float kGotoFieldWidth   = 120.0f;
constexpr float kSearchFieldWidth = 200.0f;

/* --- Status bar badges */
constexpr float kBadgePadX     = 7.0f;
constexpr float kBadgePadY     = 2.0f;
constexpr float kBadgeRounding = 4.0f;

/* --- Help panel */
constexpr float kHelpPanelWidth    = 460.0f;
constexpr float kHelpPanelPadX     = 16.0f;
constexpr float kHelpPanelPadY     = 12.0f;
constexpr float kHelpCloseSize     = 18.0f;
constexpr float kHelpPanelRounding = 6.0f;

/* --- Start screen gaps (between stacked elements) */
constexpr float kStartIconToTitle  = 24.0f;
constexpr float kStartTitleToDrop  = 16.0f;
constexpr float kStartDropToButton = 28.0f;
constexpr float kStartButtonToErr  = 20.0f;

/* --- Global ImGui style pushed at frame entry */
constexpr float kFrameRounding = 3.0f;
constexpr float kItemSpacingX  = 8.0f;
constexpr float kItemSpacingY  = 4.0f;

/* --- Timing */
constexpr float kStatusMsgSeconds = 3.5f;
constexpr float kHelpAnimSpeed    = 10.0f;

/* --- Font scale bounds */
constexpr float kFontScaleMin  = 0.75f;
constexpr float kFontScaleMax  = 2.00f;
constexpr float kFontScaleStep = 0.10f;

} /* namespace ui::layout */

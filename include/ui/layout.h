#pragma once

namespace ui::layout {

/* Hex grid spacing (multiples of character width). */
constexpr float kGapByteMul     = 0.7f;
constexpr float kGapQuadMul     = 0.7f;   /* extra gap every 4 bytes */
constexpr float kGapOctetMul    = 1.2f;   /* extra gap every 8 bytes */
constexpr float kOffsetGapMul   = 2.0f;
constexpr float kAsciiGapMul    = 2.0f;

constexpr float kHeaderExtraH   = 8.0f;
constexpr float kCaretRounding  = 3.0f;
constexpr float kCaretPulseHz   = 5.0f;

constexpr float kToolbarGroupGap  = 28.0f;
constexpr float kGotoFieldWidth   = 120.0f;
constexpr float kSearchFieldWidth = 200.0f;

constexpr float kBadgePadX     = 7.0f;
constexpr float kBadgePadY     = 2.0f;
constexpr float kBadgeRounding = 4.0f;

constexpr float kHelpPanelWidth    = 460.0f;
constexpr float kHelpPanelPadX     = 16.0f;
constexpr float kHelpPanelPadY     = 12.0f;
constexpr float kHelpCloseSize     = 18.0f;
constexpr float kHelpPanelRounding = 6.0f;

constexpr float kStartIconToTitle  = 24.0f;
constexpr float kStartTitleToDrop  = 16.0f;
constexpr float kStartDropToButton = 28.0f;
constexpr float kStartButtonToErr  = 20.0f;

constexpr float kFrameRounding = 3.0f;
constexpr float kItemSpacingX  = 8.0f;
constexpr float kItemSpacingY  = 4.0f;

constexpr float kStatusMsgSeconds = 3.5f;
constexpr float kHelpAnimSpeed    = 10.0f;

constexpr float kFontScaleMin  = 0.75f;
constexpr float kFontScaleMax  = 2.00f;
constexpr float kFontScaleStep = 0.10f;

} /* namespace ui::layout */

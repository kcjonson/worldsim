#pragma once

// Shared typography and color constants for colonist details tabs

#include <graphics/Color.h>

namespace world_sim::tabs {

// Font sizes matching NeedBar labels
constexpr float kTitleSize = 14.0F;
constexpr float kLabelSize = 12.0F;  // Matches NeedBar normal font
constexpr float kBodySize = 12.0F;   // Same as labels for consistency
constexpr float kSmallSize = 10.0F;  // Matches NeedBar compact font

// Colors - bright to match NeedBar white labels
inline Foundation::Color titleColor() { return Foundation::Color::white(); }
inline Foundation::Color labelColor() { return Foundation::Color{0.85F, 0.85F, 0.90F, 1.0F}; }
inline Foundation::Color bodyColor() { return Foundation::Color{0.80F, 0.80F, 0.85F, 1.0F}; }
inline Foundation::Color mutedColor() { return Foundation::Color{0.55F, 0.55F, 0.60F, 1.0F}; }

} // namespace world_sim::tabs

#pragma once

#include <cstdint>

// Layout system types and enums for LayoutContainer.
// See: /docs/technical/ui-framework/layout-system.md

namespace UI {

// Direction for LayoutContainer stacking
enum class Direction : uint8_t {
	Vertical,  // Stack children top to bottom
	Horizontal // Stack children left to right
};

// Per-axis sizing mode for any IComponent placed in a LayoutContainer.
// - Fixed: the element's explicit size is authoritative; layout never resizes it.
// - Hug: the element sizes to its content (Text measures itself, containers
//   size to their children). Stretched by CrossAlign::Stretch.
// - Fill: the parent assigns the size. On the main axis, Fill children share
//   the leftover space by fillWeight; on the cross axis, Fill behaves like
//   Stretch for that child alone.
enum class SizeMode : uint8_t { Fixed, Hug, Fill };

// Main-axis distribution of children within the content box. Space* modes
// distribute leftover space and stack on top of the fixed gap. With no
// leftover (Hug main axis, Fill children, or overflow) all modes degrade to
// Start.
enum class Distribution : uint8_t { Start, Center, End, SpaceBetween, SpaceAround, SpaceEvenly };

// Cross-axis alignment of children. Stretch resizes Hug/Fill children to the
// content box; Fixed children keep their size and align at Start.
enum class CrossAlign : uint8_t { Start, Center, End, Stretch };

// Per-side container insets (CSS padding order: top, right, bottom, left).
struct Insets {
	float top{0.0F};
	float right{0.0F};
	float bottom{0.0F};
	float left{0.0F};

	constexpr Insets() = default;
	constexpr Insets(float uniform) : top(uniform), right(uniform), bottom(uniform), left(uniform) {}
	constexpr Insets(float top, float right, float bottom, float left)
		: top(top), right(right), bottom(bottom), left(left) {}

	[[nodiscard]] constexpr float horizontal() const { return left + right; }
	[[nodiscard]] constexpr float vertical() const { return top + bottom; }
};

// Sentinel for IComponent::setLayoutSize: leave that axis untouched.
inline constexpr float kSizeKeep = -1.0F;

} // namespace UI

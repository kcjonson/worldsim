#pragma once

#include <graphics/Rect.h>

namespace UI {

	// A clip rect in the space of the current transform, as the screen-space rect
	// Primitives::pushClip takes: the bounds of its four corners through
	// Primitives::getCurrentTransform(). A rect with a negative or NaN extent is the empty
	// clip, not flipped round.
	[[nodiscard]] Foundation::Rect screenClip(const Foundation::Rect& local);

} // namespace UI

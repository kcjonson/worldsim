#pragma once

// OverlapGrid - which of a frame's instanced runs last drew over each patch of the screen, so
// InstancedEntityRenderer can tell whether an entity may join an earlier run without changing what
// overlaps what. Cells are a few screen pixels, so the test follows each entity's real footprint at
// every zoom: far out, where a tree is a couple of pixels across, trees that don't touch don't
// collide just because they share a cell sized for the whole view.

#include <math/Types.h>

#include <cstdint>
#include <vector>

namespace worldview {

class OverlapGrid {
  public:
	// Side of a cell in screen pixels.
	static constexpr float kCellPixels = 4.0F;

	// A footprint is widened by this much on every side, so antialiased or rounded edges that
	// spill past the mesh's bounds still count as overlap.
	static constexpr float kPadPixels = 1.0F;

	// The cells a footprint covers (x0 > x1: none, for one wholly off the screen).
	struct Cells {
		uint32_t x0 = 1;
		uint32_t y0 = 1;
		uint32_t x1 = 0;
		uint32_t y1 = 0;
	};

	/// Start a frame over a viewport of this many logical pixels; every cell reads empty.
	void beginFrame(int viewportWidth, int viewportHeight);

	/// The cells under a screen-space rect, clipped to the viewport.
	[[nodiscard]] Cells cellsOf(Foundation::Vec2 minPixels, Foundation::Vec2 maxPixels) const;

	/// The latest run key marked on any of these cells this frame (0: none).
	[[nodiscard]] uint32_t latest(const Cells& cells) const;

	/// Mark these cells as last drawn by `runKey` (1 + run index), which is at least latest(cells).
	void mark(const Cells& cells, uint32_t runKey);

  private:
	uint32_t width = 0;
	uint32_t height = 0;

	// Per cell, frameBase + run key; a value at or below frameBase is from an earlier frame, so a
	// new frame clears nothing. frameBase moves past the last frame's largest key.
	std::vector<uint32_t> stamps;
	uint32_t			  frameBase = 0;
	uint32_t			  frameMaxKey = 0;
};

} // namespace worldview

#pragma once

// RunPlanner - which instanced draw each entity of the depth-sorted upright stream goes in, so that
// any two entities whose footprints may overlap draw in depth order and everything else shares as few
// draws as it can. GL-free; InstancedEntityRenderer owns the draws.
//
// The frame's draws form a timeline of layers; in each layer every mesh has one place, in the order
// meshes first appeared this frame. An entity takes the earliest place of its mesh at or after the
// latest place any entity it may overlap took: the same layer when that place's mesh comes earlier in
// the layer or is its own, the next layer otherwise. An entity that overlaps nothing goes in the first
// layer, however late it sorts. Within a draw, instances keep the stream's order.

#include "world/rendering/OverlapGrid.h"

#include <cstdint>
#include <span>
#include <vector>

namespace worldview {

class RunPlanner {
  public:
	/// Start a frame over a viewport of this many logical pixels, for meshes numbered below meshCount.
	void beginFrame(int viewportWidth, int viewportHeight, uint32_t meshCount);

	/// The screen cells a footprint covers (OverlapGrid::cellsOf).
	[[nodiscard]] OverlapGrid::Cells cellsOf(Foundation::Vec2 minPixels, Foundation::Vec2 maxPixels) const { return all.cellsOf(minPixels, maxPixels); }

	/// The draw (an index from 0, the order draws were opened in) an entity of `mesh` over `cells`
	/// joins. Called in depth order. A small entity is ordered against large ones only.
	[[nodiscard]] uint32_t place(uint32_t mesh, const OverlapGrid::Cells& cells, bool small);

	/// The draws opened since the last close, in the order they must be drawn. Everything placed
	/// after this draws after all of them.
	[[nodiscard]] std::span<const uint32_t> close();

	/// Draws opened for a mesh that already had an open one, because of an overlap.
	[[nodiscard]] uint32_t splits() const { return splitCount; }

  private:
	// A draw's place on the timeline as a grid key: 1 + layer * meshBound + the mesh's order, meshBound
	// the power of two at or above the mesh count so a key splits by shift and mask. Keys grow with
	// depth order, so a cell's largest key is the latest place drawn over it. Layers stop at maxLayer,
	// so a key is at most OverlapGrid::kMaxRunKey.
	[[nodiscard]] uint32_t keyOf(uint32_t layer, uint32_t order) const { return 1 + (layer << meshShift) + order; }

	// A layer past maxLayer is placed in it instead (logged once): entities there may draw out of
	// depth order, where a wrapped key would scramble the whole frame. A frame reaches it only after
	// kMaxRunKey / meshBound layers, 262,144 at 4,096 mesh slots.
	[[nodiscard]] uint32_t capLayer(uint32_t layer);

	// Every entity marks `all`; only those not small mark `large`. A large entity reads `all`, a small one `large`.
	OverlapGrid all;
	OverlapGrid large;

	uint32_t meshBound = 1;
	uint32_t meshShift = 0;
	uint32_t maxLayer = 0;
	bool	 cappedLogged = false;

	// Per mesh, its order this frame (kNone before it first appears); per order, the mesh, its draws
	// by layer (kNone: none), and the close count when it last opened a draw.
	static constexpr uint32_t kNone = UINT32_MAX;
	std::vector<uint32_t>				orderOfMesh;
	std::vector<uint32_t>				meshOfOrder;
	std::vector<std::vector<uint32_t>> drawOfLayer;
	std::vector<uint32_t>				openedAt;

	struct Draw {
		uint32_t key = 0;
	};
	std::vector<Draw>	  draws; // this frame's, by index
	std::vector<uint32_t> closing;
	uint32_t			  firstOpen = 0; // draws from here on are open
	uint32_t			  floorLayer = 0; // the first layer open draws may use
	uint32_t			  topLayer = 0;	  // the highest layer an open draw uses
	uint32_t			  closes = 0;
	uint32_t			  splitCount = 0;
};

} // namespace worldview

#pragma once

// The seen area's feather (sight-softening.md 7.2): the fade inside the record and its pending shapes,
// drawn on their union's boundary and nowhere else. The record's part is kept in cells of
// kSeenFeatherCellMm, one set of uploaded meshes a cell, rebuilt where the record changed; near the
// pending shapes the record's edge and theirs are cut against each other on the CPU, exactly and in
// integers, a piece kept where the ground just off its outer side is seen by nothing else.

#include "CoverPieces.h"

#include <vision/SeenArea.h>

#include <core/Vec2i64.h>

#include <primitives/InstanceData.h>
#include <vector/Types.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace worldview {

	// The record feather's cells (sight-softening.md 9): smaller rebuilds less at an epoch and draws more meshes.
	constexpr std::int64_t kSeenFeatherCellMm = 32000;

	// What one cut did.
	struct SeenCutCounts {
		std::size_t shapes		= 0;
		std::size_t shapeSides	= 0;
		std::size_t recordEdges = 0;
		std::size_t kept		= 0;
	};

	// The boundary of the union of the record and `shapes` near the shapes, as edges with seen ground on
	// the left: `recordEdges` (the record's edge there, seen ground on the left) and the shapes' sides,
	// each cut where it meets the others, a piece kept when the point just off its middle on its outer
	// side lies in no shape (for the record's pieces) or in neither the record (`inRecord`) nor another
	// shape (for a shape's). A point on a shape's side counts as in it. Each shape must be simple; either
	// winding is taken.
	void cutSeenBoundary(
		std::span<const std::vector<geometry::Vec2i64>* const> shapes,
		std::span<const DirectedEdge>						   recordEdges,
		const std::function<bool(const geometry::Vec2i64&)>&   inRecord,
		std::vector<DirectedEdge>&							   out,
		SeenCutCounts&										   counts
	);

	// The shape grown outward by `featherMm`, as polygons whose union it is to within a fan's chord: a
	// convex shape as one polygon, any other as its outline, a rectangle off each side, and a fan round
	// each convex corner.
	void appendGrownByFeather(const std::vector<geometry::Vec2i64>& shape, double featherMm, std::vector<std::vector<glm::dvec2>>& out);

	class SeenFeather {
	  public:
		// One cell's uploaded feather, its pieces in world meters with each vertex's value its alpha.
		struct Cell {
			std::vector<Renderer::InstancedMeshHandle> meshes;
			std::vector<ValueTriangle>				   pieces;
			std::size_t								   edges = 0;
			geometry::Vec2i64						   low{0, 0}; // the cell grown by the feather, which its pieces lie within a feather of
			geometry::Vec2i64						   high{0, 0};
			std::uint64_t							   frame = 0;
		};
		struct Counts {
			std::size_t cellsDrawn	   = 0;
			std::size_t cellsRebuilt   = 0;
			std::size_t cellEdgesRead  = 0; // record edges read for the cells this frame
			std::size_t epochCellsLast = 0; // cells the last epoch rebuilt in view, and the edges they read
			std::size_t epochEdgesLast = 0;
			std::size_t changedBoxes   = 0;
			std::size_t pendingShapes  = 0;
			std::size_t clusters	   = 0;
			std::size_t clustersCut	   = 0; // pending cuts made this frame, the rest kept from earlier frames
			SeenCutCounts cut;			   // what this frame's cuts read and kept
			std::size_t pendingPieces  = 0;
		};

		// The frame over the view from `viewLow` to `viewHigh` in millimetres: drains the record's changed
		// boxes (`changed`), drops the cells and cuts they reach, builds and uploads the cells in view that
		// are missing, and cuts the pending shapes near the view. `seen` gives the record's edge and its
		// pending shapes.
		void update(
			const vision::SeenArea&				  seen,
			std::span<const vision::SeenArea::Box> changed,
			const geometry::Vec2i64&			  viewLow,
			const geometry::Vec2i64&			  viewHigh,
			std::int64_t						  featherMm
		);

		// The cells to draw this frame.
		[[nodiscard]] const std::vector<const Cell*>& cellsInView() const { return drawnCells; }
		// The meshes of the cut near the pending shapes in view, and their pieces in world millimetres.
		[[nodiscard]] const std::vector<const Renderer::InstancedMeshHandle*>& pendingMeshes() const { return pendingDrawn; }
		void appendPendingPieces(std::vector<ValueTriangle>& out) const;
		// The pending shapes in view grown by the feather: where the cells' pieces give way to the cut's.
		[[nodiscard]] const std::vector<std::vector<glm::dvec2>>& pendingGrown() const { return grown; }
		[[nodiscard]] const Counts&							   counts() const { return frameCounts; }
		// The pending shapes in view, as the record handed them out this frame.
		[[nodiscard]] const std::vector<const std::vector<geometry::Vec2i64>*>& pendingInView() const { return pendingNear; }

	  private:
		struct Cluster {
			geometry::Vec2i64		  low;
			geometry::Vec2i64		  high;
			std::vector<ValueTriangle> pieces;
			std::vector<Renderer::InstancedMeshHandle> meshes;
			std::uint64_t			  frame = 0;
		};

		void buildCell(const vision::SeenArea& seen, std::int64_t cellX, std::int64_t cellY, Cell& cell) const;

		std::int64_t												 feather = 0;
		std::uint64_t												 frameNumber = 0;
		std::map<std::pair<std::int64_t, std::int64_t>, Cell>		 cells;
		std::unordered_map<std::uint64_t, Cluster>					 clusters;
		std::vector<const Cell*>									 drawnCells;
		std::vector<const Renderer::InstancedMeshHandle*>			 pendingDrawn;
		std::vector<std::vector<glm::dvec2>>						 grown;
		std::vector<const std::vector<geometry::Vec2i64>*>			 pendingNear;
		Counts														 frameCounts;
	};

} // namespace worldview

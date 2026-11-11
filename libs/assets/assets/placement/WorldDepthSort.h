#pragma once

// World-space 2.5D depth sorting (Visual Layering epic, Story A).
//
// The world camera is a pure scaled top-down ortho: screen-Y is a linear
// function of world-Y (a larger world-Y draws lower on screen, toward the
// viewer). So one world-Y key is enough to order 2.5D occlusion. Every
// renderable gets a canonical ground-contact anchorY; the tall upright occluders
// (trees, bushes, rocks) plus every dynamic ECS entity (colonists, dropped /
// packaged items) are merged and stable-sorted ascending by anchorY, then
// submitted in that order so submission order == depth order (larger anchorY
// submitted last = drawn in front). Terrain, groundcover, and baked SHORT flora
// stay an unsorted background below this stream. GL_DEPTH_TEST stays disabled;
// this is a pure CPU painter's sort.

#include "assets/placement/SpatialIndex.h"

#include <cstdint>
#include <vector>

namespace renderer {
struct TessellatedMesh;
}

namespace assets {

	// RenderContext is only referenced by-reference in WorldDepthGather::gather's
	// signature; forward-declaring it keeps RenderContext.h (which transitively pulls in
	// PlacementExecutor and other heavy deps) out of this widely-included header. The
	// include lives in WorldDepthSort.cpp, where gather() is defined.
	struct RenderContext;
	struct AssetDefinition;

	// Canonical anchorY rule: the world-Y of a renderable's ground-contact line. One
	// formula reconciles the two position conventions (static: position == mesh
	// origin; dynamic: position re-centered to the mesh bbox): the origin's world-Y
	// plus the contact line's local-Y (anchorLocalY) scaled by the entity scale.
	// Larger draws later / in front.
	[[nodiscard]] inline float computeAnchorY(float originWorldY, float anchorLocalY, float scale) {
		return originWorldY + anchorLocalY * scale;
	}

	// The local-Y of a renderable's ground-contact line: the lower edge of its def's
	// footprint for that age class (a tree's trunk foot) when it has one, else the
	// mesh's bottom-most local-Y. A footprint wins so what a tree draws below its foot
	// (contact shadow, root flare, grass) does not pull it forward in the sort.
	[[nodiscard]] float anchorLocalY(const AssetDefinition* def, uint8_t ageClass, float meshMaxLocalY);

	// Local-space Y extent (min, max) of a template mesh; {0, 0} for null / empty.
	struct MeshYExtent {
		float minY = 0.0F;
		float maxY = 0.0F;
	};
	[[nodiscard]] MeshYExtent meshYExtent(const renderer::TessellatedMesh* mesh);

	// One sortable reference into the merged upright-occluder + actor stream.
	struct DepthSortItem {
		float						anchorY = 0.0F;
		const assets::PlacedEntity* entity = nullptr;
		bool						isAnimated = false; // per-part deformed colonist -> CPU path, breaks instanced runs
	};

	// Stable ascending sort by anchorY. Submission order == depth order; stable so
	// equal keys keep insertion order (e.g. a packaged crate stays behind its item).
	void sortByAnchorY(std::vector<DepthSortItem>& items);

	// Gathers one frame's frustum-culled, Y-sorted upright stream: visible static
	// occluders from the placement index merged with the dynamic ECS entities,
	// stable-sorted by anchorY. Both anchorY and the tall/short classification are
	// precomputed (statics at placement time in PlacementExecutor, dynamics in
	// DynamicEntityRenderSystem), so this does no per-entity string/mesh lookups.
	//
	// backgroundOnFastPath == true (instanced path): short flora and groundcover
	// are drawn on their own fast paths (baked mesh + GroundcoverRenderer) and so
	// are excluded from the sorted stream, leaving only tall occluders + actors.
	// backgroundOnFastPath == false (batched fallback, which has no separate
	// background): every static is included so nothing is dropped.
	class WorldDepthGather {
	  public:
		void gather(const RenderContext& ctx, std::vector<DepthSortItem>& out, bool backgroundOnFastPath);
	};

} // namespace assets

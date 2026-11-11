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
#include "world/chunk/ChunkCoordinate.h"

#include <cstdint>
#include <unordered_map>
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

	// One upright of the stream, with what culling, sorting and drawing read copied out of its entity,
	// so a frame walks these instead of the entities scattered over the spatial index's cells.
	struct Upright {
		float				anchorY = 0.0F;
		glm::vec2			position{0.0F, 0.0F};
		float				rotation = 0.0F;
		float				scale = 1.0F;
		glm::vec4			colorTint{1.0F, 1.0F, 1.0F, 1.0F};
		uint32_t			templateId = 0;
		bool				isAnimated = false; // per-part deformed colonist -> CPU path, breaks instanced runs
		const PlacedEntity* entity = nullptr;
	};

	/// The upright an entity draws as; animated when it carries part transforms.
	[[nodiscard]] Upright uprightOf(const PlacedEntity& entity);

	// One sortable reference into the merged upright-occluder + actor stream.
	struct DepthSortItem {
		float		   anchorY = 0.0F;
		const Upright* upright = nullptr;
	};

	// Gathers one frame's frustum-culled, Y-sorted upright stream: visible tall static
	// occluders from the placement index merged with the dynamic ECS entities,
	// stable-sorted by anchorY, so equal keys keep insertion order (a packaged crate
	// stays behind its item). Short flora and groundcover draw on their own paths (baked
	// mesh, GroundcoverRenderer) and stay out of the stream. Both anchorY and the
	// tall/short classification are precomputed (statics at placement time in
	// PlacementExecutor, dynamics in DynamicEntityRenderSystem), so this does no
	// per-entity string/mesh lookups.
	//
	// Each chunk's statics are kept as Uprights sorted between frames, rebuilt when its index
	// changes, so a frame culls the stretch of each list the view's rows can reach and merges
	// them instead of querying and sorting every static in view. The result is the same sequence
	// a stable sort of the chunks' queries, in (y, x) chunk order, then the dynamic entities,
	// would give. Its items point into the gather, valid until the next gather.
	class WorldDepthGather {
	  public:
		void gather(const RenderContext& ctx, std::vector<DepthSortItem>& out);

	  private:
		// A chunk's statics as of an index version, stable-sorted by anchorY from the index's
		// query order, the box their positions span, and the farthest any anchorY is from its
		// position's y.
		struct ChunkStatics {
			const SpatialIndex* index = nullptr;
			uint64_t			version = 0;
			float				anchorReach = 0.0F;
			uint64_t			usedFrame = 0;
			glm::vec2			min{0.0F, 0.0F};
			glm::vec2			max{0.0F, 0.0F};
			std::vector<Upright> statics;
		};
		std::unordered_map<world::ChunkCoordinate, ChunkStatics> chunks;
		std::vector<world::ChunkCoordinate>						 orderedChunks;
		std::vector<size_t>										 segmentEnds; // per chunk, where its items end in out
		std::vector<Upright>									 dynamics;	  // this frame's visible dynamic entities
		uint64_t												 frame = 0;

		/// The chunk's sorted statics, rebuilt when its index is new or has changed.
		const ChunkStatics& staticsOf(world::ChunkCoordinate coord, const SpatialIndex& index);
	};

} // namespace assets

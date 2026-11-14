#pragma once

// World-space geometry + state-derived styling for COMMITTED construction
// (foundation fills, wall bands/junctions, opening footprints), cached against
// ConstructionWorld::version(). The interim renderer used to re-run
// resolveWallBands over the whole wall graph and re-triangulate every ring
// every frame with no visibility culling, so committed construction cost grew
// linearly with the total built world (~24 us per building per frame; a few
// hundred buildings broke the frame budget). Every mutation that can change
// this geometry or its state-derived styling bumps the topology version
// (commit, edit, demolish, state/entity wiring), so refresh() rebuilds only
// then. Build PROGRESS is the one per-frame styling input; the renderer reads
// it from the cached ECS entity handle. Material palette colors resolve at
// rebuild, so a (dev-only) registry hot-reload recolors on the next topology
// change, not instantly.
// Still interim: C6's baked element-emitter replaces this whole render path.

#include <construction/ConstructionWorld.h>

#include <ecs/EntityID.h>
#include <graphics/Color.h>
#include <graphics/Rect.h>
#include <math/Types.h>
#include <polygon/Polygon.h>

#include <array>
#include <cstdint>
#include <set>
#include <vector>

namespace world_sim {

	/// Longest oriented-footprint edge in world meters (the opening's clear-width
	/// axis). Shared by the cached committed render and the live opening ghost.
	[[nodiscard]] float footprintWidthMeters(const geometry::Ring& footprint);

	struct FoundationGeom {
		std::vector<Foundation::Vec2> ring; // world meters, CCW (the outline)
		// Tessellation of `ring` (world meters; may add vertices). Merges and notches
		// make concave foundations the normal case, so a vertex fan won't do.
		std::vector<Foundation::Vec2> fillVertices;
		std::vector<uint16_t>		  fillIndices;
		Foundation::Rect			  aabb; // world meters
		Foundation::Color			  matColor;
		bool						  built = false;
		ecs::EntityID				  entity = ecs::kInvalidEntity; // per-frame progress source
	};

	struct WallBandGeom {
		// The segment's trimmed band cut at its opening gaps, plus the junction
		// polygons it owns. They tile with the other walls' rings, no overlap.
		std::vector<std::vector<Foundation::Vec2>> rings;
		// Its share of the joined wall shape's outline; seams are not in it.
		std::vector<std::array<Foundation::Vec2, 2>> outline;
		Foundation::Rect							 aabb; // world meters, rings and outline together
		Foundation::Color							 matColor;
		bool										 built = false;
		ecs::EntityID								 entity = ecs::kInvalidEntity;
		construction::SegmentId						 segment = construction::kInvalidSegment;
	};

	struct OpeningGeom {
		std::vector<Foundation::Vec2> footprint; // oriented rect, 4 world corners
		Foundation::Rect			  aabb;
		Foundation::Color			  matColor;
		float						  widthMeters = 0.0F;
		bool						  window = false;
		bool						  built = false;
		ecs::EntityID				  entity = ecs::kInvalidEntity;
	};

	/// See file header. refresh() is a no-op while the world's version is unchanged.
	class CommittedGeometryCache {
	  public:
		void refresh(const construction::ConstructionWorld& world);

		[[nodiscard]] const std::vector<FoundationGeom>& foundations() const { return foundations_; }
		[[nodiscard]] const std::vector<WallBandGeom>&	 walls() const { return walls_; }
		[[nodiscard]] const std::vector<OpeningGeom>&	 openings() const { return openings_; }

		/// Centerlines of the wall graphs resolveWallBands rejected; the renderer
		/// draws these instead (reject-don't-repair, no garbage bands).
		[[nodiscard]] const std::vector<std::array<Foundation::Vec2, 2>>& fallbackCenterlines() const {
			return fallbackCenterlines_;
		}

	  private:
		void rebuild(const construction::ConstructionWorld& world);

		std::vector<FoundationGeom>					 foundations_;
		std::vector<WallBandGeom>					 walls_;
		std::vector<OpeningGeom>					 openings_;
		std::vector<std::array<Foundation::Vec2, 2>> fallbackCenterlines_;
		// Each rejected graph (its segment ids, endpoints, and half thicknesses)
		// as of the last rebuild, so its warning logs once until it changes.
		std::set<std::vector<std::int64_t>> loggedRejectedGraphs_;
		// Never matches a real version (they start at 0), so the first refresh builds.
		std::uint64_t builtVersion_ = ~0ULL;
	};

} // namespace world_sim

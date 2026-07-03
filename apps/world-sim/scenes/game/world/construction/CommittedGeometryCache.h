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
#include <vector>

namespace world_sim {

	/// Longest oriented-footprint edge in world meters (the opening's clear-width
	/// axis). Shared by the cached committed render and the live opening ghost.
	[[nodiscard]] float footprintWidthMeters(const geometry::Ring& footprint);

	struct FoundationGeom {
		std::vector<Foundation::Vec2> ring; // world meters, CCW
		std::vector<uint16_t>		  fan;	// fan triangulation of `ring`
		Foundation::Rect			  aabb; // world meters
		Foundation::Color			  matColor;
		bool						  built = false;
		ecs::EntityID				  entity = ecs::kInvalidEntity; // per-frame progress source
	};

	struct WallBandGeom {
		// One trimmed band, or several solid sub-bands around opening gaps.
		std::vector<std::vector<Foundation::Vec2>> rings;
		Foundation::Rect						   aabb;
		Foundation::Color						   matColor;
		bool									   built = false;
		ecs::EntityID							   entity = ecs::kInvalidEntity;
	};

	struct JunctionGeom {
		std::vector<Foundation::Vec2> ring;
		Foundation::Rect			  aabb;
		Foundation::Color			  matColor;
		bool						  built = false; // every incident segment Built
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
		void refresh(const engine::construction::ConstructionWorld& world);

		[[nodiscard]] const std::vector<FoundationGeom>& foundations() const { return foundations_; }
		[[nodiscard]] const std::vector<WallBandGeom>&	 walls() const { return walls_; }
		[[nodiscard]] const std::vector<JunctionGeom>&	 junctions() const { return junctions_; }
		[[nodiscard]] const std::vector<OpeningGeom>&	 openings() const { return openings_; }

		/// True when resolveWallBands rejected the graph; the renderer falls back
		/// to bare centerlines (reject-don't-repair, no garbage bands).
		[[nodiscard]] bool bandsFailed() const { return bandsFailed_; }
		[[nodiscard]] const std::vector<std::array<Foundation::Vec2, 2>>& fallbackCenterlines() const {
			return fallbackCenterlines_;
		}

	  private:
		void rebuild(const engine::construction::ConstructionWorld& world);

		std::vector<FoundationGeom>					 foundations_;
		std::vector<WallBandGeom>					 walls_;
		std::vector<JunctionGeom>					 junctions_;
		std::vector<OpeningGeom>					 openings_;
		std::vector<std::array<Foundation::Vec2, 2>> fallbackCenterlines_;
		bool										 bandsFailed_ = false;
		// Never matches a real version (they start at 0), so the first refresh builds.
		std::uint64_t builtVersion_ = ~0ULL;
	};

} // namespace world_sim

#pragma once

// Vision System for Colonist Observation
// Updates colonist Memory components by observing nearby world entities.
// Also discovers shore points (land beside water) that can fulfill needs.
// See /docs/design/game-systems/colonists/memory.md for design details.

#include "../ISystem.h"
#include "../EntityID.h"

#include <vision/GeometryIndex.h>

#include <world/chunk/ChunkCoordinate.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace assets {
	class PlacementExecutor;
}

namespace world {
	class ChunkManager;
}

namespace ecs {

struct Knowledge;
struct Memory;

/// Updates colonist Memory by observing nearby world entities and terrain features.
/// Queries PlacementExecutor for PlacedEntities within each colonist's sight radius.
/// Also scans chunks for shore points (land beside water) with Drinkable capability.
/// Priority: 45 (runs early, before needs decay and AI decisions)
///
/// Performance: Throttled to run every N frames (default 5) since colonists don't
/// move fast enough to need per-frame vision updates.
class VisionSystem : public ISystem {
  public:
	void update(float deltaTime) override;

	[[nodiscard]] int priority() const override { return 45; }
	[[nodiscard]] const char* name() const override { return "Vision"; }

	/// Set how often vision updates run (default: every 5 frames)
	/// At 60fps, 5 frames = 12 vision updates/second, which is plenty
	void setUpdateInterval(uint32_t frames) { m_updateInterval = frames; }

	/// Set the placement executor and processed chunks for entity queries
	/// Must be called before update() can function
	void setPlacementData(
		assets::PlacementExecutor*						  executor,
		const std::unordered_set<world::ChunkCoordinate>* processedChunks) {
		m_placementExecutor = executor;
		m_processedChunks = processedChunks;
	}

	/// Set the chunk manager for shore discovery
	void setChunkManager(world::ChunkManager* chunkManager) { m_chunkManager = chunkManager; }

	/// Set callback for recipe discovery notifications ("Aha!" moments)
	/// Called with recipe label when colonist learns something that unlocks a new recipe
	using RecipeDiscoveryCallback = std::function<void(const std::string& recipeLabel)>;
	void setRecipeDiscoveryCallback(RecipeDiscoveryCallback callback) { m_onRecipeDiscovery = std::move(callback); }

	/// Wire the construction topology that walls block sight from. Forwards to the
	/// owned GeometryIndex. Null leaves the index inert (zero occluders), so every
	/// observer takes the outdoor fast path -- vision then behaves exactly as before
	/// walls existed.
	void setConstructionWorld(const construction::ConstructionWorld* world) {
		m_geometry.setConstructionWorld(world);
	}

	/// Test-only: how many visibility polygons were actually rebuilt so far. A
	/// stationary indoor observer should not bump this every tick (cache reuse).
	[[nodiscard]] uint64_t polygonBuildCount() const { return m_polygonBuildCount; }

	/// An observer's sight as vision last built it. With no polygon the shape is the plain
	/// circle; with one it is the wall-clipped star-shaped polygon (mm, same frame as the
	/// occluders) inside that circle. polygonRevision changes whenever the observer's polygon is
	/// rebuilt or dropped, so a consumer can tell a new polygon from the one it already used.
	/// The polygon pointer is valid until the next update().
	struct SightShape {
		glm::vec2			   center{0.0F, 0.0F}; // world position (meters) the shape was built at
		float				   radius = 0.0F;	   // meters
		const geometry::Ring* polygon = nullptr;  // null when no wall is in sight
		std::uint64_t		   polygonRevision = 0;
	};

	/// The sight shape vision used for `observer` on its last pass, outdoors or indoors. Empty
	/// for an entity vision has not observed.
	[[nodiscard]] std::optional<SightShape> sightShape(EntityID observer) const {
		auto it = m_visibilityCache.find(observer);
		if (it == m_visibilityCache.end()) {
			return std::nullopt;
		}
		const VisibilityCache& entry = it->second;
		return SightShape{
			.center = entry.builtPos,
			.radius = entry.radius,
			.polygon = entry.hadOccluders ? &entry.polygon : nullptr,
			.polygonRevision = entry.polygonRevision,
		};
	}

	/// Read-only access to the occluder geometry (walls) for overlay drawing.
	[[nodiscard]] const GeometryIndex& geometry() const { return m_geometry; }

	/// Give `observer` what a look over the disc of `radiusMeters` round `center` would give him,
	/// through the same passes as his sight each tick. The landing crew's memory of the landing area
	/// (player-visibility.md 3.8). Leaves his sight shape alone.
	void rememberArea(EntityID observer, glm::vec2 center, float radiusMeters);

  private:
	/// Ensure synthetic terrain definitions are registered (called once on first update)
	void ensureTerrainDefinitionsRegistered();

	/// The walls and openings that bound or cross `polygon`, into memory (vision-architecture D4).
	void rememberStructures(geometry::Vec2i64 centerMm, std::int64_t radiusMm, const geometry::Ring& polygon, Memory& memory);

	/// What the disc of `radiusMeters` round `center` shows, inside `polygon` when walls clip it
	/// (null outdoors), into memory: reconciliation, then placed entities, ECS entities, and shore.
	void lookOver(
		EntityID observer, glm::vec2 center, float radiusMeters, const geometry::Ring* polygon, Memory& memory, Knowledge* knowledge
	);

	assets::PlacementExecutor*							  m_placementExecutor = nullptr;
	const std::unordered_set<world::ChunkCoordinate>*	  m_processedChunks = nullptr;
	world::ChunkManager*								  m_chunkManager = nullptr;

	// Cached defNameId for shore points (registered on first update)
	uint32_t m_shoreDefNameId = 0;
	uint16_t m_shoreCapabilityMask = 0;
	bool	 m_terrainDefsRegistered = false;

	// Callback for recipe discovery notifications
	RecipeDiscoveryCallback m_onRecipeDiscovery = nullptr;

	// --- Occlusion gate ---

	// Source of opaque wall occluders (built from the construction graph). Inert
	// until setConstructionWorld() wires a world; rebuilt (version-gated) at the
	// top of each throttled tick.
	GeometryIndex m_geometry;

	// Per-observer sight cache, one entry per observer. A stationary colonist indoors builds
	// its star-shaped sight polygon once, then reuses it every tick. The polygon is
	// in integer mm (same frame as the occluders); builtPos is the observer's
	// meters position the polygon was built from (the current position outdoors).
	struct VisibilityCache {
		glm::vec2	   builtPos{0.0F, 0.0F};
		float		   radius = 0.0F;		// meters, the sight radius of the last pass or build
		std::uint64_t  polygonRevision = 0; // bumps when the polygon is rebuilt or dropped
		std::uint64_t  builtVersion = 0; // GeometryIndex generation the polygon was built against
		geometry::Ring polygon;			 // empty when hadOccluders == false (outdoors)
		bool		   hadOccluders = false;
		bool		   seenThisTick = false; // mark-and-sweep prune flag
	};
	std::unordered_map<EntityID, VisibilityCache> m_visibilityCache;

	// A remembered entity in view that the placement index doesn't hold: forgotten unless pass 2
	// sees it in the ECS. Sorted by key for pass 2's lookup; reused across observers.
	struct StaleCandidate {
		uint64_t  key = 0;
		glm::vec2 position{0.0F, 0.0F};
		uint32_t  defNameId = 0;
		bool	  seen = false;
	};
	std::vector<StaleCandidate> m_staleScratch;

	// Test-only diagnostic: incremented on each actual polygon (re)build.
	uint64_t m_polygonBuildCount = 0;

	// Throttling: only update every N frames to reduce CPU overhead
	// Initialize to interval so first update() call executes immediately
	uint32_t m_frameCounter = 5;
	uint32_t m_updateInterval = 5; // Every 5 frames: 12 passes a second at 60 fps, 24 at 120
};

} // namespace ecs

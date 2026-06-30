#pragma once

// Vision System for Colonist Observation
// Updates colonist Memory components by observing nearby world entities.
// Also discovers shore tiles (land adjacent to water) that can fulfill needs.
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

namespace engine::assets {
	class PlacementExecutor;
}

namespace engine::world {
	class ChunkManager;
}

namespace ecs {

/// Updates colonist Memory by observing nearby world entities and terrain features.
/// Queries PlacementExecutor for PlacedEntities within each colonist's sight radius.
/// Also scans chunks for shore tiles (land adjacent to water) with Drinkable capability.
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
		engine::assets::PlacementExecutor*						  executor,
		const std::unordered_set<engine::world::ChunkCoordinate>* processedChunks) {
		m_placementExecutor = executor;
		m_processedChunks = processedChunks;
	}

	/// Set the chunk manager for terrain tile queries (shore discovery)
	void setChunkManager(engine::world::ChunkManager* chunkManager) { m_chunkManager = chunkManager; }

	/// Set callback for recipe discovery notifications ("Aha!" moments)
	/// Called with recipe label when colonist learns something that unlocks a new recipe
	using RecipeDiscoveryCallback = std::function<void(const std::string& recipeLabel)>;
	void setRecipeDiscoveryCallback(RecipeDiscoveryCallback callback) { m_onRecipeDiscovery = std::move(callback); }

	/// Wire the construction topology that walls block sight from. Forwards to the
	/// owned GeometryIndex. Null leaves the index inert (zero occluders), so every
	/// observer takes the outdoor fast path -- vision then behaves exactly as before
	/// walls existed.
	void setConstructionWorld(const engine::construction::ConstructionWorld* world) {
		m_geometry.setConstructionWorld(world);
	}

	/// Test-only: how many visibility polygons were actually rebuilt so far. A
	/// stationary indoor observer should not bump this every tick (cache reuse).
	[[nodiscard]] uint64_t polygonBuildCount() const { return m_polygonBuildCount; }

	/// Returns the cached visibility polygon for `observer`, or nullptr if the
	/// entity has no cache entry or took the outdoor (no-occluder) fast path.
	[[nodiscard]] const geometry::Ring* visibilityPolygon(EntityID observer) const {
		auto it = m_visibilityCache.find(observer);
		if (it == m_visibilityCache.end() || !it->second.hadOccluders) {
			return nullptr;
		}
		return &it->second.polygon;
	}

	/// Read-only access to the occluder geometry (walls) for overlay drawing.
	[[nodiscard]] const GeometryIndex& geometry() const { return m_geometry; }

  private:
	/// Ensure synthetic terrain definitions are registered (called once on first update)
	void ensureTerrainDefinitionsRegistered();

	engine::assets::PlacementExecutor*							  m_placementExecutor = nullptr;
	const std::unordered_set<engine::world::ChunkCoordinate>*	  m_processedChunks = nullptr;
	engine::world::ChunkManager*								  m_chunkManager = nullptr;

	// Cached defNameId for shore tiles (registered on first update)
	uint32_t m_shoreTileDefNameId = 0;
	uint16_t m_shoreTileCapabilityMask = 0;
	bool	 m_terrainDefsRegistered = false;

	// Callback for recipe discovery notifications
	RecipeDiscoveryCallback m_onRecipeDiscovery = nullptr;

	// --- Occlusion gate ---

	// Source of opaque wall occluders (built from the construction graph). Inert
	// until setConstructionWorld() wires a world; rebuilt (version-gated) at the
	// top of each throttled tick.
	GeometryIndex m_geometry;

	// Per-observer visibility polygon cache. A stationary colonist indoors builds
	// its star-shaped sight polygon once, then reuses it every tick. The polygon is
	// in integer mm (same frame as the occluders); builtPos is the observer's
	// meters position the polygon was built from.
	struct VisibilityCache {
		glm::vec2	   builtPos{0.0F, 0.0F};
		std::uint64_t  builtVersion = 0; // GeometryIndex generation the polygon was built against
		geometry::Ring polygon;			 // empty when hadOccluders == false (fast path)
		bool		   hadOccluders = false;
		bool		   seenThisTick = false; // mark-and-sweep prune flag
	};
	std::unordered_map<EntityID, VisibilityCache> m_visibilityCache;

	// Test-only diagnostic: incremented on each actual polygon (re)build.
	uint64_t m_polygonBuildCount = 0;

	// Throttling: only update every N frames to reduce CPU overhead
	// Initialize to interval so first update() call executes immediately
	uint32_t m_frameCounter = 5;
	uint32_t m_updateInterval = 5; // Default: update every 5 frames (12x/sec at 60fps)
};

} // namespace ecs

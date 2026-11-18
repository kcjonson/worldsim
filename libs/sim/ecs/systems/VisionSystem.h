#pragma once

// Vision System for Colonist Observation
// Updates colonist Memory components by observing nearby world entities.
// Also discovers shore points (land beside water) that can fulfill needs.
// See /docs/design/game-systems/colonists/memory.md for design details.

#include "../ISystem.h"
#include "../EntityID.h"

#include <vision/GeometryIndex.h>
#include <vision/PlacedOccluderIndex.h>
#include <vision/SightBody.h>
#include <vision/SightTunables.h>
#include <visibility/ForestShape.h>

#include <world/chunk/ChunkCoordinate.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <functional>
#include <optional>
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
/// Also scans chunks for shore points (land beside water) with the Drinkable and Washable capabilities.
/// Priority: 45 (runs early, before needs decay and AI decisions)
///
/// Needs a TimeSystem in the same world: a mover's sighting is stamped with the game clock, and a world
/// without one aborts in development builds the first time a mover is seen (ecs::simulationSeconds).
///
/// Performance: Throttled to run every N frames (default 5) since colonists don't
/// move fast enough to need per-frame vision updates.
///
/// Sight reads bodies (sight-softening.md 2): wall bodies from the GeometryIndex it owns, placed rings
/// from NavigationSystem's placed occluder index, never the nav mesh. Opaque bodies occlude; soft
/// ones (a tree, a wall stub) do not, and a mover, water, a ford, or the mesh's bounds never do. Every
/// placed ring in sight, opaque or soft, shortens his range as forest depth (sight-softening.md 4).
class VisionSystem : public ISystem {
  public:
	/// How far an observer may move from where his sight shape was built before it is rebuilt.
	/// The shape is built to `reach`, sight plus `lead`, so it holds his sight from anywhere he can be
	/// before the next pass (sight-softening.md 2.4); what vision decides with it is held to his sight.
	static constexpr float		  kSightRebuildStepMeters = 0.5F;
	static constexpr std::int64_t kSightRebuildStepMm	   = 500;
	/// A colonist's walk until the next pass is taken up to whole steps of this in `lead`.
	static constexpr std::int64_t kLeadWalkStepMm = 100;

	void update(float deltaTime) override;

	/// The sight tunables (sight-softening.md 9). The setter clamps every field to its range; a change
	/// re-decides every body's kind where a threshold moved and rebuilds every observer's shape at the
	/// next pass.
	[[nodiscard]] const vision::SightTunables& sightTunables() const { return tunables; }
	void									   setSightTunables(const vision::SightTunables& next);

	/// The bodies within `reach` of an observer's last build (sight-softening.md 2.4), each with its
	/// outline, kind, serial or wall body, and arrival fraction; the build's shadow edges, indexing
	/// into `bodies`; and the `lead` that pass computed. Valid until vision's next pass. Empty for an
	/// entity vision has not observed.
	struct SightBodies {
		const std::vector<vision::SightBody>*  bodies	   = nullptr;
		const std::vector<vision::ShadowEdge>* shadowEdges = nullptr;
		std::int64_t						   leadMm	   = 0;
		std::int64_t						   reachMm	   = 0;
		geometry::Vec2i64					   eyeCentre{0, 0};
	};
	[[nodiscard]] std::optional<SightBodies> sightBodies(EntityID observer) const;

	/// The visibility polygon round `centre` out to `reachMm` that the opaque bodies there leave:
	/// the walls' facing occluders and the opaque placed rings in reach. What the sweep builds.
	[[nodiscard]] geometry::Ring hardPolygon(const geometry::Vec2i64& centre, std::int64_t reachMm) const;

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

	enum class DiscoveryKind : uint8_t { NewKind, Recipe };

	/// A colonist saw a kind of thing for the first time, or learned a recipe by it ("Aha!").
	struct Discovery {
		EntityID	  colonist = kInvalidEntity;
		DiscoveryKind kind = DiscoveryKind::NewKind;
		/// NewKind: the AssetRegistry id of the kind seen. Recipe: of the recipe's first output.
		uint32_t definition = 0;
		/// Recipe: its label.
		std::string recipeLabel;
	};
	using DiscoveryObserver = std::function<void(const Discovery& discovery)>;

	/// Hears the discoveries each pass of update() makes, once the pass is over. rememberArea's look is silent.
	void addDiscoveryObserver(DiscoveryObserver observer) { m_discoveryObservers.push_back(std::move(observer)); }

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
	/// Test-only: how many sight builds, plain disc or polygon, were made so far.
	[[nodiscard]] uint64_t buildCount() const { return builds; }

	/// The three paths a build takes (sight-softening.md 5.1): the plain sight polygon with no placed
	/// ring in sight and no body in reach; the forest shape of his sight with placed rings in sight, all
	/// soft, and no wall in reach; and the polygon path with an opaque body or any wall in reach.
	enum class SightPath : std::uint8_t { Open, Leg, Polygon };

	/// An observer's sight as vision last built it (sight-softening.md 5.2). On the open path it is the
	/// sight polygon of `radius` round where he stands, the forest shape of equal ranges. On a leg it is
	/// the forest shape of `ranges` placed where he stands. On the polygon path it is `polygon`, the
	/// star-shaped shape round `center` out to his reach that the opaque bodies leave cut by the forest
	/// shape of his reach, within `radius` of where he stands and inside the forest shape of `ranges`
	/// placed there. polygonRevision changes whenever
	/// the observer's polygon is rebuilt or dropped, so a consumer can tell a new polygon from the one
	/// it already used. The pointers are valid until the next update().
	struct SightShape {
		glm::vec2			   center{0.0F, 0.0F}; // world position (meters) the shape was built at
		float				   radius = 0.0F;	   // meters
		SightPath			   path	  = SightPath::Open;
		const geometry::Ring* polygon = nullptr;  // set on the polygon path
		std::uint64_t		   polygonRevision = 0;
		std::uint64_t		   buildRevision   = 0; // changes at every build, on every path
		// The 64 ranges of the build's forest shape of `radius`, in mm from where it is placed; every one
		// `radius` on the open path.
		const geometry::SightSideValues* ranges = nullptr;
		// Their half-planes; null only for a sight too long for forest depth (none a colonist has), whose
		// shape is then the circle of `radius` cut by the polygon.
		const geometry::ForestHalfPlanes* forest = nullptr;
		// The built walls and openings the polygon bounds or crosses within `radius` of `center`, as the
		// structure pass found them when it was built (vision-architecture D4). Empty without a polygon.
		const std::vector<construction::SegmentId>* segmentsSeen = nullptr;
		const std::vector<construction::OpeningId>* openingsSeen = nullptr;

		/// Whether `point` is in his sight while he stands at `standing`: within `radius` of it, inside
		/// the polygon on the polygon path, and inside the forest shape of `ranges` placed there.
		[[nodiscard]] bool holds(glm::vec2 standing, glm::vec2 point) const;
	};

	/// The sight shape vision used for `observer` on its last pass. Empty for an entity vision has not
	/// observed.
	[[nodiscard]] std::optional<SightShape> sightShape(EntityID observer) const;

	/// Read-only access to the occluder geometry (walls) for overlay drawing.
	[[nodiscard]] const GeometryIndex& geometry() const { return m_geometry; }

	/// Give `observer` the things in the plain disc of `radiusMeters` round `center`, silently: what he
	/// learns there is told to no discovery observer. The landing crew's memory of the landing area
	/// (player-visibility.md 3.8): no occluder is read and no shape built, so it agrees with the plain
	/// disc the seen record commits (sight-softening.md 5.3). Leaves his sight shape alone.
	void rememberArea(EntityID observer, glm::vec2 center, float radiusMeters);

  private:
	/// Ensure synthetic terrain definitions are registered (called once on first update)
	void ensureTerrainDefinitionsRegistered();

	/// The walls and openings that bound or cross the part of `builtPolygon` within `radiusMm` of `centerMm`, into memory
	/// (vision-architecture D4) and into `segments` and `openings` (cleared first). `backFacesHidden` is what the query
	/// the polygon was built from returned.
	void rememberStructures(
		geometry::Vec2i64						 centerMm,
		std::int64_t							 radiusMm,
		const geometry::Ring&					 builtPolygon,
		bool									 backFacesHidden,
		Memory&									 memory,
		std::vector<construction::SegmentId>&	 segments,
		std::vector<construction::OpeningId>&	 openings
	);

	/// What the disc of `radiusMeters` round `center` shows, inside `shape` held from `center` (null for
	/// the plain disc), into memory: reconciliation, then placed entities, ECS entities (movers among
	/// them, stamped with the game time), and shore.
	void lookOver(
		EntityID observer, glm::vec2 center, float radiusMeters, const SightShape* shape, Memory& memory, Knowledge* knowledge
	);

	assets::PlacementExecutor*							  m_placementExecutor = nullptr;
	const std::unordered_set<world::ChunkCoordinate>*	  m_processedChunks = nullptr;
	world::ChunkManager*								  m_chunkManager = nullptr;

	// Cached defNameId for shore points (registered on first update)
	uint32_t m_shoreDefNameId = 0;
	uint16_t m_shoreCapabilityMask = 0;
	bool	 m_terrainDefsRegistered = false;

	/// The observer learns `defNameId`; when it is new, it and the recipe it unlocks join m_learnedScratch.
	void learn(EntityID observer, Knowledge& knowledge, uint32_t defNameId);

	std::vector<DiscoveryObserver> m_discoveryObservers;
	// What lookOver learned, until update() tells the observers or rememberArea drops it.
	std::vector<Discovery> m_learnedScratch;

	// --- Occlusion gate ---

	// Source of wall bodies (built from the construction graph). Inert
	// until setConstructionWorld() wires a world; rebuilt (version-gated) at the
	// top of each throttled tick.
	GeometryIndex m_geometry;

	vision::SightTunables tunables;
	// Bumped by every change of the tunables: a shape built before it is rebuilt.
	std::uint64_t tunablesVersion = 0;

	// Per-observer sight cache, one entry per observer. A stationary colonist indoors builds
	// its star-shaped sight polygon once, then reuses it every tick. The polygon is
	// in integer mm (same frame as the occluders); builtPos is the observer's
	// meters position the shape was built from (the current position outdoors).
	struct VisibilityCache {
		glm::vec2		  builtPos{0.0F, 0.0F};
		geometry::Vec2i64 builtMm{0, 0};
		float			  radius = 0.0F;		// meters, the sight radius of the last pass or build
		std::int64_t	  leadMm = 0;			// the lead the shape was built with
		std::uint64_t	  buildRevision = 0;	// bumps at every build
		std::int64_t	  reachMm = 0;			// sight plus that lead
		std::uint64_t	  polygonRevision = 0; // bumps when the polygon is rebuilt or dropped
		std::uint64_t	  builtVersion = 0; // GeometryIndex generation the shape was built against
		std::uint64_t	  tunablesVersion = 0;
		// The sorted serials of the placed rings within reach of the build.
		std::vector<std::uint64_t> placedSerials;
		SightPath				   path = SightPath::Open;
		geometry::SightSideValues  ranges{};	// the forest shape of his sight round the build
		geometry::ForestHalfPlanes forest;		// its half-planes, off the open path
		geometry::Ring	  polygon;			 // empty when hasPolygon == false (outdoors)
		std::vector<construction::SegmentId> segmentsSeen; // what the structure pass found for the polygon
		std::vector<construction::OpeningId> openingsSeen;
		std::vector<vision::SightBody>		 bodies;	  // within reach of the build
		std::vector<vision::ShadowEdge>		 shadowEdges; // of the polygon, into bodies
		bool		   built = false;
		bool		   hasPolygon = false;
		bool		   hasForest = false; // `forest` holds the half-planes of `ranges`
		bool		   seenThisTick = false; // mark-and-sweep prune flag
	};

	// The opaque wall bodies' facing occluders within `reachMm` of `eye`, then the edges of the
	// opaque rings among `placed`, into `out` (cleared first). Returns what queryFacingOccluders did.
	bool opaqueOccluders(
		const geometry::Vec2i64&							eye,
		std::int64_t										reachMm,
		const std::vector<const vision::PlacedOccluder*>& placed,
		std::vector<geometry::OccluderSegment>&				out
	) const;

	// The placed occluder index of the world's NavigationSystem, or null without one.
	[[nodiscard]] const vision::PlacedOccluderIndex* placedIndex() const;
	[[nodiscard]] static SightShape shapeOf(const VisibilityCache& entry);
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

	// The game time movers seen this pass are stamped with, read from the clock at the first such sighting.
	std::optional<double> m_sightingTime;

	// The forest clip's failure is logged once.
	bool clipFailureLogged = false;

	// Test-only diagnostic: incremented on each actual polygon (re)build.
	uint64_t m_polygonBuildCount = 0;
	uint64_t builds = 0;

	// Throttling: only update every N frames to reduce CPU overhead
	// Initialize to interval so first update() call executes immediately
	uint32_t m_frameCounter = 5;
	uint32_t m_updateInterval = 5; // Every 5 frames: 12 passes a second at 60 fps, 24 at 120
};

} // namespace ecs

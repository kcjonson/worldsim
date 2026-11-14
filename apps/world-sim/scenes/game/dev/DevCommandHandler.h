#pragma once

// DevCommandHandler - the dev/test verbs (/api/dev/<verb>) and world-state views
// (/api/state?what=) against the live game. Dev-only. Split out of GameScene so the scene
// class stays focused on the frame loop, not the (large) cheat + inspection surface.
//
// The dev server (salal's app) stays domain-agnostic: GameScene constructs one of these after
// its systems exist and registers its routes and providers (registerWith), which the server
// answers on the game thread, before the frame's update and after its render respectively.

#include <DevServer/DevServer.h>
#include <ecs/EntityID.h>
#include <math/Types.h> // Foundation::Vec2

#include <glm/vec2.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ecs {
	class World;
	class NavigationSystem;
	struct Inventory;
	enum class NeedType : std::uint8_t;
}
namespace world {
	class ChunkManager;
	class WorldCamera;
}
namespace assets {
	class PlacementExecutor;
}
namespace construction {
	using FoundationId = std::uint64_t;
}
namespace worldgen {
	struct GeneratedWorld;
}

namespace world_sim {

	class DrawingSystem;
	class PlacementSystem;
	class SelectionSystem;
	class GameUI;

	/// Non-owning pointers to the game systems the dev verbs act on, plus the colonist-spawn
	/// helper that lives on GameScene (it is also used at world init). GameScene outlives the
	/// handler, so the raw pointers are safe for the handler's lifetime.
	struct DevCommandContext {
		ecs::World*					 world = nullptr;
		world_sim::DrawingSystem*	 drawing = nullptr;
		world_sim::PlacementSystem*	 placement = nullptr;
		world_sim::SelectionSystem*	 selection = nullptr;
		world_sim::GameUI*			 ui = nullptr;
		world::ChunkManager* chunks = nullptr;
		ecs::NavigationSystem*		 navigation = nullptr; // world-position validity (isValidPosition)
		const assets::PlacementExecutor* placementIndex = nullptr; // placed statics, for the flora view
		const world::WorldCamera*		 camera = nullptr;		   // the flora view's default rect
		float							 pixelsPerMeter = 0.0F;	   // world-to-screen at zoom 1

		// Planet used for the current session; populated from GameStartConfig. Used by
		// serializeLanding (/api/state?what=landing) for biome + water readback. May be
		// null for test/fallback sessions that have no planet (readback degrades gracefully).
		std::shared_ptr<const worldgen::GeneratedWorld> planet;
		double landingLatDeg = 0.0;
		double landingLonDeg = 0.0;

		std::function<ecs::EntityID(glm::vec2, const std::string&)> spawnColonist;
		// GameScene::dropResourcePiles: the one loose-ground drop path (merge, then stack-capped piles).
		std::function<void(const std::string& defName, float x, float y, uint32_t quantity)> dropLoose;
	};

	/// The dev verbs and world-state views. Read+write surface for /api/dev/<verb> and
	/// /api/state. Dev builds only.
	class DevCommandHandler {
	  public:
		explicit DevCommandHandler(DevCommandContext context) : m_ctx(std::move(context)) {}

		/// Registers every verb as a route and every view as a state provider, for as long as the
		/// returned registrations live. Most verbs answer {"status":"ok","verb":...}; spawn, foundation
		/// and walls answer a 400 with {"error":...} when they create nothing, ground they refuse
		/// included; verbs with an outcome to report (foundation-edit) answer with it, and flora answers
		/// with its measurement. The views are colonists, construction, stations, storage, time,
		/// landing, tunables and summary.
		std::vector<app::DevServer::Registration> registerWith(app::DevServer& server);

	  private:
		// --- write verbs ---
		void devFreeBuild(const app::DevRequest& cmd);
		// reveal?on=1|0: player visibility off or on (player-visibility.md 3.10)
		void devReveal(const app::DevRequest& cmd);
		void devGive(const app::DevRequest& cmd);
		// spawn?def=&at=x,y&n=&scatter=: the placement tool's ground gate (PlacementSystem::footprintClearToBuild)
		// at the centre and at each scattered spot, which is skipped when refused.
		std::string devSpawn(const app::DevRequest& cmd);
		void devColonist(const app::DevRequest& cmd);
		void devNeed(const app::DevRequest& cmd);
		void devTime(const app::DevRequest& cmd);
		void devTeleport(const app::DevRequest& cmd);
		void devSelect(const app::DevRequest& cmd);
		void devKill(const app::DevRequest& cmd);
		void devComplete(const app::DevRequest& cmd);
		std::string devFoundation(const app::DevRequest& cmd);
		// foundation-edit?id=<foundationId>&mode=add|subtract&pts=x0,y0;x1,y1;... runs the
		// Add / Subtract tool's own validate + apply path; answers status ok|rejected,
		// reason, target, extension (0 unless an Add onto a built foundation), entity, area.
		std::string devFoundationEdit(const app::DevRequest& cmd);
		// flora?rect=x0,y0,x1,y1 (world metres, default the camera's view): the trees in the rect
		// per def and age class, canopy cover, and expected wood (assets::measureFlora). With
		// age=<name>&near=x,y (and optionally def=) it lists the nearest trees of that age class
		// instead (assets::nearestTreesOfAge). A route, not a state view, because a state
		// provider is given no query parameters.
		app::DevResponse devFlora(const app::DevRequest& cmd);
		// ground?pts=x0,y0;x1,y1;... (world metres): what each point is to the nav mesh (onMesh, valid:
		// clear to stand on, buildable) beside what its chunk's terrain polygons hold there, and the
		// mesh's live triangle count. The terrain side reads the chunk's unclipped rings: inWater,
		// toWaterM (null when no shore is within 32 m), and every ring the point is in or on, with its
		// kind, hole (a clockwise ring, an island in its water body), blocksMovement and mean half
		// width. chunk is false, with no terrain fields, when the point's chunk is not ready.
		// Read-only. No pts, or a point that is not finite, is a 400.
		app::DevResponse devGround(const app::DevRequest& cmd);
		std::string devWalls(const app::DevRequest& cmd);
		void		devOpening(const app::DevRequest& cmd);
		void		devCraft(const app::DevRequest& cmd);
		// storage?at=x,y&item=<def>|*&category=&priority=&min=&max=: adds a rule to the nearest box. A
		// named item's category comes from its definition (category= may only repeat it); an unknown
		// item is a 400. category= picks the wildcard's category, RawMaterial by default.
		app::DevResponse devStorage(const app::DevRequest& cmd);
		void		devTunable(const app::DevRequest& cmd);

		// --- world-position validity (the gates for every placing/moving verb) ---
		// True when `at` is on an active walkable nav face. On false, logs+toasts the
		// refusal, saying whether `at` is off the meshed ground or on water or a placed
		// obstacle, and the caller creates/moves NOTHING.
		bool requireValidPosition(Foundation::Vec2 at, const char* verb);

		// Where a mover may be put: ground no mover has meshed yet (his own limit lands
		// there at once, outside every budget), else requireValidPosition.
		bool requireMoverPosition(Foundation::Vec2 at, const char* verb);

		// Logs and toasts a verb's refusal, and returns it, when it is not empty.
		std::string refuse(const std::string& refusal);

		// --- entity lookup helpers ---
		ecs::EntityID	nearestColonist(Foundation::Vec2 at);
		ecs::Inventory* nearestColonistInventory(Foundation::Vec2 at);
		ecs::Inventory* nearestStorageInventory(Foundation::Vec2 at);
		ecs::EntityID	nearestStorageEntity(Foundation::Vec2 at);

		// --- state serialization ---
		void serializeColonists(std::ostringstream& out);
		void serializeConstruction(std::ostringstream& out);
		void serializeStations(std::ostringstream& out);
		void serializeStorage(std::ostringstream& out);
		void serializeTime(std::ostringstream& out);
		void serializeLanding(std::ostringstream& out);
		void serializeSummary(std::ostringstream& out);
		// The seen record's totals, and with x and y given, whether that point is seen and visible
		// (/api/state?what=visibility takes no parameters; /api/dev/visibility?x=&y= answers the point).
		std::string visibilityJson(const app::DevRequest& cmd);

		// --- pure parsing helpers ---
		static std::string					 param(const app::DevRequest& cmd, const std::string& key, const std::string& fallback = "");
		static bool							 hasParam(const app::DevRequest& cmd, const std::string& key);
		static ecs::EntityID				 parseEntity(const std::string& spec);
		static bool							 parseNeedType(const std::string& name, ecs::NeedType& out);
		static bool							 equalsIgnoreCase(const std::string& a, const char* b);
		static float						 parseClock(const std::string& spec);
		static float						 parseDuration(const std::string& spec);
		static Foundation::Vec2				 parsePoint(const std::string& spec);
		static Foundation::Vec2				 spreadOffset(long i, long n, float radius);
		static std::string					 jsonEscape(const std::string& s);

		DevCommandContext m_ctx;
	};

} // namespace world_sim

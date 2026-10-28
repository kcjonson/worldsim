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
namespace engine::world {
	class ChunkManager;
}
namespace engine::construction {
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
		engine::world::ChunkManager* chunks = nullptr;
		ecs::NavigationSystem*		 navigation = nullptr; // world-position validity (isValidPosition)

		// Planet used for the current session; populated from GameStartConfig. Used by
		// serializeLanding (/api/state?what=landing) for biome + water readback. May be
		// null for test/fallback sessions that have no planet (readback degrades gracefully).
		std::shared_ptr<const worldgen::GeneratedWorld> planet;
		double landingLatDeg = 0.0;
		double landingLonDeg = 0.0;

		std::function<ecs::EntityID(glm::vec2, const std::string&)> spawnColonist;
	};

	/// The dev verbs and world-state views. Read+write surface for /api/dev/<verb> and
	/// /api/state. Dev builds only.
	class DevCommandHandler {
	  public:
		explicit DevCommandHandler(DevCommandContext context) : m_ctx(std::move(context)) {}

		/// Registers every verb as a route and every view as a state provider, for as long as the
		/// returned registrations live. Most verbs answer {"status":"ok","verb":...}; verbs with an
		/// outcome to report (foundation-edit) answer with it. The views are colonists,
		/// construction, stations, storage, time, landing, tunables and summary.
		std::vector<app::DevServer::Registration> registerWith(app::DevServer& server);

	  private:
		// --- write verbs ---
		void devFreeBuild(const app::DevRequest& cmd);
		void devGive(const app::DevRequest& cmd);
		void devSpawn(const app::DevRequest& cmd);
		void devColonist(const app::DevRequest& cmd);
		void devNeed(const app::DevRequest& cmd);
		void devTime(const app::DevRequest& cmd);
		void devTeleport(const app::DevRequest& cmd);
		void devSelect(const app::DevRequest& cmd);
		void devKill(const app::DevRequest& cmd);
		void devComplete(const app::DevRequest& cmd);
		void devFoundation(const app::DevRequest& cmd);
		// foundation-edit?id=<foundationId>&mode=add|subtract&pts=x0,y0;x1,y1;... runs the
		// Add / Subtract tool's own validate + apply path; answers status ok|rejected,
		// reason, target, extension (0 unless an Add onto a built foundation), entity, area.
		std::string devFoundationEdit(const app::DevRequest& cmd);
		void		devWalls(const app::DevRequest& cmd);
		void		devOpening(const app::DevRequest& cmd);
		void		devCraft(const app::DevRequest& cmd);
		void		devStorage(const app::DevRequest& cmd);
		void		devTunable(const app::DevRequest& cmd);

		// --- world-position validity (single gate for every placing/moving verb) ---
		// True when `at` is on an active walkable nav face. On false, logs+toasts the
		// refusal in the standard rejection style and the caller creates/moves NOTHING.
		bool requireValidPosition(Foundation::Vec2 at, const char* verb);

		// True when the WHOLE foundation footprint (the closed polygon `pts`: vertices, edges, AND
		// interior) is BUILDABLE -- NavigationSystem::isAreaBuildable, the same terrain-only predicate the
		// UI foundation tool uses: water and off-mesh gaps block, clearable entities (trees/rocks) do not.
		// Wall conflicts are the validator's job. A footprint that spans a river or clips a water hole is
		// refused so no partial-on-water structure is stamped. On rejection logs+toasts and returns false;
		// the caller creates NOTHING.
		bool requireBuildableArea(const std::vector<Foundation::Vec2>& pts, const char* verb);

		// True when the WHOLE wall chain `pts` lies on walkable nav mesh -- every consecutive
		// segment's endpoints AND its centerline samples (NavigationSystem::isPolylineWalkable,
		// the shared linear predicate). A segment that bridges water between two on-land points
		// is refused. On rejection logs+toasts and returns false; the caller creates NOTHING.
		bool requireWalkableChain(const std::vector<Foundation::Vec2>& pts, const char* verb);

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
		static std::vector<Foundation::Vec2> parsePointList(const std::string& spec);
		static std::string					 jsonEscape(const std::string& s);

		DevCommandContext m_ctx;
	};

} // namespace world_sim

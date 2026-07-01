#pragma once

// DevCommandHandler - interprets dev/test commands (/api/dev/<verb>) and serializes world
// state (/api/state) against the live game. Dev-only. Split out of GameScene so the scene
// class stays focused on the frame loop, not the (large) cheat + inspection surface.
//
// The debug server (libs/foundation) stays domain-agnostic and hands the app generic
// DevCommands; this is where the game context (ECS world, construction, placement, selection,
// UI) interprets them. GameScene constructs one of these after its systems exist and delegates
// the per-frame DevCommand drain and the /api/state readback to it.

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

namespace Foundation {
	struct DevCommand;
}
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

	/// Interprets queued DevCommands and serializes world-state views. Read+write surface for
	/// /api/dev/<verb> and /api/state. Dev builds only.
	class DevCommandHandler {
	  public:
		explicit DevCommandHandler(DevCommandContext context) : m_ctx(std::move(context)) {}

		/// Interpret one queued DevCommand. Unknown verbs are logged and ignored.
		void handle(const Foundation::DevCommand& cmd);

		/// Produce the JSON for /api/state?what=. Unknown views fall back to the summary.
		std::string serializeState(const std::string& what);

	  private:
		// --- write verbs ---
		void devFreeBuild(const Foundation::DevCommand& cmd);
		void devGive(const Foundation::DevCommand& cmd);
		void devSpawn(const Foundation::DevCommand& cmd);
		void devColonist(const Foundation::DevCommand& cmd);
		void devNeed(const Foundation::DevCommand& cmd);
		void devTime(const Foundation::DevCommand& cmd);
		void devTeleport(const Foundation::DevCommand& cmd);
		void devSelect(const Foundation::DevCommand& cmd);
		void devKill(const Foundation::DevCommand& cmd);
		void devComplete(const Foundation::DevCommand& cmd);
		void devFoundation(const Foundation::DevCommand& cmd);
		void devWalls(const Foundation::DevCommand& cmd);
		void devOpening(const Foundation::DevCommand& cmd);
		void devCraft(const Foundation::DevCommand& cmd);
		void devStorage(const Foundation::DevCommand& cmd);

		// --- world-position validity (single gate for every placing/moving verb) ---
		// True when `at` is on an active walkable nav face. On false, logs+toasts the
		// refusal in the standard rejection style and the caller creates/moves NOTHING.
		bool requireValidPosition(Foundation::Vec2 at, const char* verb);

		// True when the WHOLE foundation footprint (the closed polygon `pts`: vertices, edges, AND
		// interior) is BUILDABLE -- NavigationSystem::isAreaBuildable, the same terrain-only predicate the
		// UI foundation tool uses: geography (water) and built walls block, clearable entities (trees/
		// rocks) do not. A footprint that spans a river or clips a water hole is refused so no
		// partial-on-water structure is stamped. On rejection logs+toasts and returns false; the caller
		// creates NOTHING.
		bool requireBuildableArea(const std::vector<Foundation::Vec2>& pts, const char* verb);

		// True when the WHOLE wall chain `pts` lies on walkable nav mesh -- every consecutive
		// segment's endpoints AND its centerline samples (NavigationSystem::isPolylineWalkable,
		// the shared linear predicate). A segment that bridges water between two on-land points
		// is refused. On rejection logs+toasts and returns false; the caller creates NOTHING.
		bool requireWalkableChain(const std::vector<Foundation::Vec2>& pts, const char* verb);

		// --- entity lookup / spawn helpers ---
		ecs::EntityID	nearestColonist(Foundation::Vec2 at);
		ecs::Inventory* nearestColonistInventory(Foundation::Vec2 at);
		ecs::Inventory* nearestStorageInventory(Foundation::Vec2 at);
		ecs::EntityID	nearestStorageEntity(Foundation::Vec2 at);
		ecs::EntityID	spawnFoundationBlueprintEntity(
			  engine::construction::FoundationId id, const std::vector<Foundation::Vec2>& pts, const std::string& material
		  );

		// --- state serialization ---
		void serializeColonists(std::ostringstream& out);
		void serializeConstruction(std::ostringstream& out);
		void serializeStations(std::ostringstream& out);
		void serializeStorage(std::ostringstream& out);
		void serializeTime(std::ostringstream& out);
		void serializeLanding(std::ostringstream& out);
		void serializeSummary(std::ostringstream& out);

		// --- pure parsing helpers ---
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

#pragma once

// Player visibility (player-visibility.md 3.3): feeds the seen record from every player colonist's
// sight each frame and answers what the player may see. A colonist is an entity with the Colonist
// tag and Memory; his sight is what VisionSystem used on its last pass. In the open he holds a leg,
// the straight segment from where it began to where he stands, pending until it ends. With walls in
// sight each new visibility polygon goes into the record, swept back along his walk to the one
// before where it opens ground; nothing else of his is pending. Shapes go into the record
// kSeenRingsPerTick a tick.

#include "../EntityID.h"
#include "../ISystem.h"
#include "VisionSystem.h"

#include <construction/ConstructionWorld.h>
#include <vision/SeenArea.h>

#include <core/Vec2i64.h>

#include <glm/vec2.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

namespace ecs {

	class World;

	class PlayerVisibilitySystem : public ISystem {
	  public:
		void update(float deltaTime) override;

		// After VisionSystem, whose sight shapes it reads, and before anything that asks it.
		[[nodiscard]] int		  priority() const override { return 46; }
		[[nodiscard]] const char* name() const override { return "PlayerVisibility"; }

		// The landing area, seen before the crew exists: the sight polygon of `radiusMeters` round
		// `drop`, inscribed in the circle nav's landing limit circumscribes, committed now.
		void revealLanding(glm::vec2 drop, float radiusMeters);

		// In the seen record or a pending shape.
		[[nodiscard]] bool isSeen(glm::vec2 meters) const;
		// In a player colonist's sight now, by vision's own test: within his sight radius of where
		// vision last looked from, or within it of where he stands and in his visibility polygon.
		[[nodiscard]] bool isVisible(glm::vec2 meters) const;
		// The ground drawn bright for `colonist` this frame (player-visibility.md 3.4), in millimetres:
		// the sight polygon of vision::drawnVisibleRadiusMm round where he stands, and with walls in
		// sight vision's polygon cut to it. Outdoors it lies in his open leg; with walls in sight it can
		// reach up to a rebuild step past seen ground ahead of him, where the cover's unseen fill hides
		// it. None when his sight is too small to hold a leg, or walls are in sight and a jump dropped
		// his polygon (until vision builds the next).
		[[nodiscard]] std::optional<std::vector<geometry::Vec2i64>> drawnVisible(EntityID colonist) const;
		// The whole outline is seen.
		[[nodiscard]] bool covers(const std::vector<glm::vec2>& outlineMeters) const;
		// The seen point on the segment from `from` to `to` nearest `to`; `to` when it is seen. None
		// when nothing on the segment is.
		[[nodiscard]] std::optional<glm::vec2> lastSeenToward(glm::vec2 from, glm::vec2 to) const;
		// Moves when the committed record does.
		[[nodiscard]] std::uint64_t epoch() const { return seen.epoch(); }

		[[nodiscard]] const vision::SeenArea& seenArea() const { return seen; }

		// The dev toggle that turns player visibility off (player-visibility.md rule 18): with it on
		// the cover is not drawn. The record keeps growing either way.
		void			   setRevealed(bool on) { revealedFlag = on; }
		[[nodiscard]] bool revealed() const { return revealedFlag; }

		// The system when the player's input and sight rules apply: null when none is registered or reveal
		// is on, so a caller gates exactly when it is handed one.
		[[nodiscard]] static const PlayerVisibilitySystem* gate(World& world);

		// The walls and openings whose footprints join the record once a colonist has seen them
		// (player-visibility.md 3.3, rule 22). Null: no structures. Does not take ownership.
		void setConstructionWorld(const construction::ConstructionWorld* world) { constructionWorld = world; }

		// Wall and opening footprints queued so far, each once.
		[[nodiscard]] std::size_t footprintsEntered() const { return enteredFootprints.size(); }

	  private:
		// A leg's held positions, kept as what bounds the leg's end: the directions from its start
		// within kSeenSlackMm of every held position's line (a wedge, once one lies farther out than
		// that), and the farthest any lay from the start.
		struct Leg {
			bool					open = false;
			geometry::Vec2i64		start;
			geometry::Vec2i64		end;
			std::int64_t			radiusMm = 0;
			bool					hasWedge = false;
			std::array<double, 2>	wedgeLow{};
			std::array<double, 2>	wedgeHigh{};
			double					farthestMm = 0.0;
		};

		// `leg` is his open leg in the open. With walls in sight `walk` is his path since the last
		// polygon that went in, for the sweep: its build, then each turn, where `stretch` (a leg, not
		// pending) ended. `wallSight` holds while vision's polygon of `polygonRevision` stands for
		// him: a jump drops it until the next.
		struct Observer {
			Leg							   leg;
			Leg							   stretch;
			std::vector<geometry::Vec2i64> walk;
			std::int64_t				   sightMm		   = 0; // his sight when the last polygon was built
			bool						   wallSight	   = false;
			std::uint64_t				   polygonRevision = 0; // the last visibility polygon's revision that was handled
			bool						   present		   = false;
		};

		static void beginLeg(Leg& leg, const geometry::Vec2i64& at, std::int64_t radiusMm);
		void		followLeg(EntityID colonist, Leg& leg, const geometry::Vec2i64& at, std::int64_t radiusMm);
		void		closeLeg(EntityID colonist, Leg& leg);
		// With walls in sight: a turn adds to his walk; a jump drops his polygon and walk.
		static void followStretch(Observer& observer, const geometry::Vec2i64& at);
		static void dropWallSight(Observer& observer);
		// With walls in sight, where he rests or his walk ends away from vision's last build, a polygon
		// built there goes in, so the ground he sees from there is not left to the next build.
		void		enterPolygonAt(Observer& observer, const geometry::Vec2i64& at);
		// The one place vision's polygon enters the record (player-visibility.md 3.3).
		void		registerPolygon(Observer& observer, const VisionSystem::SightShape& shape);
		static bool holds(const Leg& leg, const geometry::Vec2i64& at);
		static void hold(Leg& leg, const geometry::Vec2i64& at);
		// Queues the footprint of every structure the polygon of `shape` touched that has not gone in.
		void enterStructures(const VisionSystem::SightShape& shape);
		void enterFootprint(const std::vector<geometry::Vec2i64>& ring);
		// Rebuilds the footprints by structure when the construction world has changed.
		void refreshFootprints();

		vision::SeenArea			 seen;
		const GeometryIndex*		 sweepWalls = nullptr; // the walls the record's sweeps build their polygons against
		std::map<EntityID, Observer> observers;
		bool						 revealedFlag = false;

		const construction::ConstructionWorld*								 constructionWorld = nullptr;
		std::uint64_t														 footprintVersion  = ~std::uint64_t{0};
		std::vector<std::vector<geometry::Vec2i64>>							 footprints; // nav's wall and opening rings, as built
		std::unordered_map<construction::SegmentId, std::vector<std::size_t>> footprintsBySegment;
		std::unordered_map<construction::OpeningId, std::vector<std::size_t>> footprintsByOpening;
		std::set<std::vector<geometry::Vec2i64>>							 enteredFootprints;
	};

} // namespace ecs

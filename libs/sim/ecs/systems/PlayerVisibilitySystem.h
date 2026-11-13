#pragma once

// Player visibility (player-visibility.md 3.3): feeds the seen record from every player colonist's
// sight each frame and answers what the player may see. A colonist is an entity with the Colonist
// tag and Memory; his sight is what VisionSystem used on its last pass. Outdoors he holds a leg, the
// straight segment from where it began to where he stands, pending until it ends; with walls in
// sight each new visibility polygon is queued. Shapes go into the record kSeenRingsPerTick a tick.

#include "../EntityID.h"
#include "../ISystem.h"

#include <vision/SeenArea.h>

#include <core/Vec2i64.h>

#include <glm/vec2.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace ecs {

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
		// vision last looked from, or in his visibility polygon.
		[[nodiscard]] bool isVisible(glm::vec2 meters) const;
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

		struct Observer {
			Leg			  leg;
			std::uint64_t polygonRevision = 0; // the last visibility polygon's revision that was handled
			bool		  present		  = false;
		};

		void beginLeg(Leg& leg, const geometry::Vec2i64& at, std::int64_t radiusMm);
		void followLeg(EntityID colonist, Leg& leg, const geometry::Vec2i64& at, std::int64_t radiusMm);
		void closeLeg(EntityID colonist, Leg& leg);
		static bool holds(const Leg& leg, const geometry::Vec2i64& at);
		static void hold(Leg& leg, const geometry::Vec2i64& at);
		// The one place a visibility polygon enters the record (player-visibility.md 3.3, 6.2).
		void registerPolygon(const std::vector<geometry::Vec2i64>& polygon, const geometry::Vec2i64& observer);

		vision::SeenArea			 seen;
		std::map<EntityID, Observer> observers;
		bool						 revealedFlag = false;
	};

} // namespace ecs

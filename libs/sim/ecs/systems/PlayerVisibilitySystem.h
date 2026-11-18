#pragma once

// Player visibility (player-visibility.md 3.3, sight-softening.md 6): feeds the seen record from every
// player colonist's sight each frame and answers what the player may see. A colonist is an entity
// with the Colonist tag and Memory; his sight is what VisionSystem used on its last pass. Off the
// polygon path he holds a leg, the hull of the forest shapes he walked with, pending until it ends.
// On the polygon path each new shape goes into the record cut to its rim, swept back along his walk
// to the one before where it opens ground near the rim and an opaque wall was in reach; nothing else
// of his is pending.

#include "../EntityID.h"
#include "../ISystem.h"
#include "VisionSystem.h"

#include <construction/ConstructionWorld.h>
#include <vision/SeenArea.h>
#include <vision/SightFeed.h>

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
		// In a player colonist's sight now, by vision's own test (VisionSystem::SightShape::holds):
		// within his sight radius of where he stands, in his polygon on the polygon path, and in the
		// forest shape of his sight placed where he stands (sight-softening.md 5.2).
		[[nodiscard]] bool isVisible(glm::vec2 meters) const;
		// The ground drawn bright for `colonist` this frame (player-visibility.md 3.4, sight-softening.md
		// 6.3), in millimetres: off the polygon path the eased forest shape placed where he stands, whose
		// side offsets ease over rimEaseMm of his walk from those drawn when his newest build arrived to
		// that build's at vision::drawnVisibleRadiusMm less rimEaseMm; on the polygon path vision's
		// polygon cut to the sight polygon of vision::drawnVisibleRadiusMm round where he stands and then
		// to that eased shape. Off the polygon path it lies in his leg's ring; on it, it can reach up to
		// `lead` past seen ground ahead of him, where the cover's unseen fill hides it. None when his
		// sight is too small to hold a leg, or on the polygon path a jump dropped his polygon (until
		// vision builds the next).
		[[nodiscard]] std::optional<std::vector<geometry::Vec2i64>> drawnVisible(EntityID colonist) const;
		// The whole outline is seen.
		[[nodiscard]] bool covers(const std::vector<glm::vec2>& outlineMeters) const;
		// The seen point on the segment from `from` to `to` nearest `to`; `to` when it is seen. None
		// when nothing on the segment is.
		[[nodiscard]] std::optional<glm::vec2> lastSeenToward(glm::vec2 from, glm::vec2 to) const;
		// Moves when the committed record does.
		[[nodiscard]] std::uint64_t epoch() const { return seen.epoch(); }

		// A box of ground in meters.
		struct SeenBox {
			glm::vec2 low;
			glm::vec2 high;
		};
		// Boxes that together hold every point between `low` and `high` the cover leaves showing
		// (player-visibility.md 3.4): SeenArea::appendSeenBoxes, the record's and the pending shapes'.
		// The unseen fill leaves only those showing, so a colonist's bright shape needs no box of its
		// own: any part of it past them is under the fill. Drawing and the camera's chunk loads are
		// bounded by them.
		void appendSeenBoxes(glm::vec2 low, glm::vec2 high, std::vector<SeenBox>& out) const;
		// Whether appendSeenBoxes would give any box, without building them.
		[[nodiscard]] bool anySeenBox(glm::vec2 low, glm::vec2 high) const;

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
		// Test-only: how many eased forest shapes drawnVisible has built, one a colonist a frame of an ease.
		[[nodiscard]] std::uint64_t drawnForestBuildCount() const { return drawnForestBuilds; }
		// Test-only: the feed of a colonist the system follows, his legs and eased rim.
		[[nodiscard]] const vision::SightFeed* feedOf(EntityID colonist) const;

	  private:
		struct Observer {
			vision::SightFeed feed;
			bool			  present = false;
		};

		// Queues the footprint of every structure the polygon of `shape` touched that has not gone in.
		void enterStructures(const VisionSystem::SightShape& shape);
		void enterFootprint(const std::vector<geometry::Vec2i64>& ring);
		// Rebuilds the footprints by structure when the construction world has changed.
		void refreshFootprints();

		// One colonist's drawn forest shape round the origin for the side offsets it was built from. It
		// is moved to where he stands each frame, so a frame whose offsets are those of the last builds
		// nothing: every frame once the ease has ended.
		struct DrawnForest {
			vision::SightFeed::SideOffsets offsets{};
			geometry::Ring				   forest;
		};
		const geometry::Ring& drawnForestFor(EntityID colonist, const vision::SightFeed::SideOffsets& offsets) const;

		vision::SeenArea			 seen;
		mutable std::map<EntityID, DrawnForest> drawnForests;
		mutable std::uint64_t					drawnForestBuilds = 0;
		const VisionSystem*			 sweepVision = nullptr; // the vision the record's sweeps build their polygons with
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

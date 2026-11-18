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
		// Test-only: how many legs have been opened off the polygon path.
		[[nodiscard]] std::uint64_t legsOpenedCount() const { return legsOpened; }

		// How far a side's greatest range over a leg may exceed its least at the game's speed
		// `timeScale` (sight-softening.md 6.6): `toleranceMm` scaled by the distance between builds,
		// a vision interval's walk of `intervalWalkMm` at 1x times the intervals it takes to pass the
		// rebuild step, against that at 1x. `toleranceMm` itself when he does not walk.
		[[nodiscard]] static std::int64_t legToleranceMm(std::int64_t toleranceMm, double intervalWalkMm, double timeScale);

	  private:
		using SideOffsets = std::array<std::int64_t, geometry::kSightSides>;

		// A leg's held positions, kept as what bounds the leg's end: the directions from its start
		// within kSeenSlackMm of every held position's line (a wedge, once one lies farther out than
		// that), and the farthest any lay from the start. A leg in the record also keeps each side's
		// least and greatest range over its builds and the ranges it last took.
		struct Leg {
			bool					open = false;
			geometry::Vec2i64		start;
			geometry::Vec2i64		end;
			bool					hasWedge = false;
			std::array<double, 2>	wedgeLow{};
			std::array<double, 2>	wedgeHigh{};
			double					farthestMm = 0.0;
			vision::SightRanges		least{};
			vision::SightRanges		most{};
			vision::SightRanges		ranges{};
		};

		// The eased rim (sight-softening.md 6.3): the 64 side offsets drawn when the ease started, in
		// millimetres, rounded down and 1 mm less; his position then; his walk since, each frame's
		// rounded up; and the newest build's offsets at the drawn radius, each the support of its
		// forest shape along the side's normal.
		struct Ease {
			bool			  valid = false;
			std::uint64_t	  buildRevision = 0;
			SideOffsets		  start{};
			geometry::Vec2i64 startPosition;
			std::int64_t	  walkedMm	= 0;
			std::int64_t	  rimEaseMm = 1; // the tunable when the ease started
			SideOffsets		  newest{};
		};

		// `leg` is his open leg off the polygon path. On the polygon path `walk` is his path since the
		// last shape that went in, for the sweep: its build, then each turn, where `stretch` (a leg, not
		// pending) ended; `walkStartRanges` is that build's rim, and `sweepWanted` whether an opaque wall
		// was in its reach. `wallSight` holds while vision's polygon of `polygonRevision` stands for him:
		// a jump drops it until the next.
		struct Observer {
			Leg							   leg;
			Leg							   stretch;
			std::vector<geometry::Vec2i64> walk;
			vision::SightRanges			   walkStartRanges{};
			bool						   sweepWanted	   = false;
			std::int64_t				   sightMm		   = 0; // his sight when the last polygon was built
			bool						   wallSight	   = false;
			std::uint64_t				   polygonRevision = 0; // the last visibility polygon's revision that was handled
			bool						   present		   = false;
			Ease						   ease;
			bool						   placed = false; // `lastPosition` holds where he stood last frame
			geometry::Vec2i64			   lastPosition;
		};

		static void beginLeg(Leg& leg, const geometry::Vec2i64& at, const vision::SightRanges& ranges);
		// His leg off the polygon path, ended by a turn, a jump, the spread past `toleranceMm` at a new
		// build, or kSoftLegMostVertices, the next holding the shape drawn this frame but after a jump
		// or the polygon path (sight-softening.md 6.2).
		void followLeg(EntityID colonist, Observer& observer, const geometry::Vec2i64& at, const vision::SightRanges& ranges, bool newBuild, std::int64_t toleranceMm);
		// Joins his position and the leg's ranges to its ring; a ring past kSoftLegMostVertices ends the
		// leg there and opens the next.
		void joinLeg(EntityID colonist, Observer& observer, const geometry::Vec2i64& at, const std::optional<vision::SeenArea::InheritedShape>& inherited);
		void closeLeg(EntityID colonist, Leg& leg);
		// On the polygon path: a turn adds to his walk; a jump drops his polygon and walk.
		static void followStretch(Observer& observer, const geometry::Vec2i64& at);
		static void dropWallSight(Observer& observer);
		// On the polygon path, where he rests or his walk ends away from vision's last build, a shape
		// built there goes in, so the ground he sees from there is not left to the next build.
		void		enterPolygonAt(Observer& observer, const geometry::Vec2i64& at);
		// The one place vision's polygon enters the record (player-visibility.md 3.3), cut to its rim,
		// the forest shape of his sight round its build (sight-softening.md 6.4).
		void		registerPolygon(EntityID colonist, Observer& observer, const VisionSystem& vision, const VisionSystem::SightShape& shape);
		static bool holds(const Leg& leg, const geometry::Vec2i64& at);
		static void hold(Leg& leg, const geometry::Vec2i64& at);
		// The ease's walk this frame and, at a new build, its restart from the shape drawn now over
		// `rimEaseMm` of walk.
		static void followEase(Ease& ease, const VisionSystem::SightShape& shape, std::int64_t sightMm, std::int64_t rimEaseMm, std::int64_t walkedMm, const geometry::Vec2i64& at);
		// The side offsets drawn now: the ease's blend of its start and the newest build's.
		[[nodiscard]] static SideOffsets drawnOffsets(const Ease& ease);
		// The ranges whose forest shape has the given side offsets, each rounded so the shape lies inside.
		[[nodiscard]] static vision::SightRanges rangesOfOffsets(const SideOffsets& offsets);
		// Queues the footprint of every structure the polygon of `shape` touched that has not gone in.
		void enterStructures(const VisionSystem::SightShape& shape);
		void enterFootprint(const std::vector<geometry::Vec2i64>& ring);
		// Rebuilds the footprints by structure when the construction world has changed.
		void refreshFootprints();

		// One colonist's drawn forest shape round the origin for the side offsets it was built from. It
		// is moved to where he stands each frame, so a frame whose offsets are those of the last builds
		// nothing: every frame once the ease has ended.
		struct DrawnForest {
			SideOffsets	   offsets{};
			geometry::Ring forest;
		};
		const geometry::Ring& drawnForestFor(EntityID colonist, const SideOffsets& offsets) const;

		vision::SeenArea			 seen;
		mutable std::map<EntityID, DrawnForest> drawnForests;
		mutable std::uint64_t					drawnForestBuilds = 0;
		std::uint64_t							legsOpened		  = 0;
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

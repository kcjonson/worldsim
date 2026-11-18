#pragma once

// One observer's sight fed into the seen record each frame (sight-softening.md 6.2 to 6.4): off the
// polygon path his leg, the hull of the forest shapes he walked with; on it each new shape of vision's
// cut to its rim, with his walk for the sweep; and the eased rim the bright shape is drawn with.
// PlayerVisibilitySystem holds one a colonist and the forest bench one a walker, so both feed the
// record by the same rules.

#include "SeenArea.h"

#include <core/Vec2i64.h>
#include <visibility/SightPolygon.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace vision {

	// What one of vision's builds hands the feed.
	struct SightBuild {
		std::uint64_t		 buildRevision	 = 0;
		std::uint64_t		 polygonRevision = 0;
		geometry::Vec2i64	 eye{0, 0};
		std::int64_t		 sightMm = 0;
		// The forest shape of his sight round the eye, every range `sightMm` on the open path.
		const SightRanges* ranges = nullptr;
		// The ranges take a forest shape: false only for a sight too long for forest depth.
		bool forest = true;
		// The polygon path's shape round the eye, or null off it.
		const std::vector<geometry::Vec2i64>* polygon = nullptr;
		// An opaque wall body lies within the build's reach: only then is the polygon path swept.
		bool opaqueWallInReach = false;
	};

	// Why a leg ended.
	enum class LegEnd : std::uint8_t { Turn, Jump, Spread, Vertices, Path, Count };

	class SightFeed {
	  public:
		using SideOffsets = std::array<std::int64_t, geometry::kSightSides>;

		// How far a side's greatest range over a leg may exceed its least at the game's speed
		// `timeScale` (sight-softening.md 6.6): `toleranceMm` scaled by the distance between builds, a
		// vision interval's walk at that speed times the intervals it takes to pass the rebuild step,
		// against that at 1x, for a walker of 2 m/s at 60 frames a second, the game's speed alone
		// deciding it: 500 mm at 1x and 1,667 mm at 10x for 500 mm. `toleranceMm` itself when paused.
		[[nodiscard]] static std::int64_t legToleranceMm(std::int64_t toleranceMm, double timeScale);

		// The ranges whose forest shape has the given side offsets, each rounded so the shape lies inside.
		[[nodiscard]] static SightRanges rangesOfOffsets(const SideOffsets& offsets);

		// One frame with him at `at` and his last build `build`, the leg tolerance `toleranceMm` and the
		// rim's ease over `rimEaseMm` of walk. True when it entered a new polygon-path shape.
		bool follow(SeenArea& seen, SeenArea::LegId id, const geometry::Vec2i64& at, const SightBuild& build, std::int64_t toleranceMm, std::int64_t rimEaseMm);
		// His sight is too small to hold a leg: his leg and walk end.
		void stop(SeenArea& seen, SeenArea::LegId id);
		// He is gone: on the polygon path the shape where his walk ends goes in, and his leg ends.
		void leave(SeenArea& seen, SeenArea::LegId id);

		// The side offsets drawn this frame, the eased rim's: none before his first build with ranges.
		[[nodiscard]] std::optional<SideOffsets> drawnOffsets() const;
		// The polygon path's shape of `polygonRevision` stands for him: a jump drops it until the next.
		[[nodiscard]] bool holdsPolygon(std::uint64_t polygonRevision) const { return wallSight && polygonRevision == polygonSeen; }

		[[nodiscard]] std::size_t legsOpened() const { return opened; }
		[[nodiscard]] std::size_t legsEndedBy(LegEnd reason) const { return endedBy[static_cast<std::size_t>(reason)]; }

	  private:
		// A leg's held positions, kept as what bounds the leg's end: the directions from its start within
		// kSeenSlackMm of every held position's line (a wedge, once one lies farther out than that), and
		// the farthest any lay from the start. A leg in the record also keeps each side's least and
		// greatest range over its builds and the ranges it last took.
		struct Leg {
			bool				  open = false;
			geometry::Vec2i64	  start;
			geometry::Vec2i64	  end;
			bool				  hasWedge = false;
			std::array<double, 2> wedgeLow{};
			std::array<double, 2> wedgeHigh{};
			double				  farthestMm = 0.0;
			SightRanges			  least{};
			SightRanges			  most{};
			SightRanges			  ranges{};
		};

		// The eased rim (sight-softening.md 6.3): the 64 side offsets drawn when the ease started, in
		// millimetres; his walk since, each frame's rounded up; and the newest build's offsets at the
		// drawn radius, each the support of its forest shape along the side's normal.
		struct Ease {
			bool		  valid			= false;
			std::uint64_t buildRevision = 0;
			SideOffsets	  start{};
			std::int64_t  walkedMm	= 0;
			std::int64_t  rimEaseMm = 1; // the tunable when the ease started
			SideOffsets	  newest{};
		};

		static void beginLeg(Leg& leg, const geometry::Vec2i64& at, const SightRanges& ranges);
		static bool holds(const Leg& leg, const geometry::Vec2i64& at);
		static void hold(Leg& leg, const geometry::Vec2i64& at);
		void		followLeg(SeenArea& seen, SeenArea::LegId id, const geometry::Vec2i64& at, const SightRanges& ranges, bool newBuild, std::int64_t toleranceMm);
		// Opens his next leg's ring: begun at the leg's start, his position, and `inherited`.
		void		openLeg(SeenArea& seen, SeenArea::LegId id, const geometry::Vec2i64& at, const std::optional<SeenArea::InheritedShape>& inherited);
		void		closeLeg(SeenArea& seen, SeenArea::LegId id, LegEnd reason);
		void		followStretch(const geometry::Vec2i64& at);
		void		dropWallSight();
		void		enterPolygonAt(SeenArea& seen, const geometry::Vec2i64& at);
		void		registerPolygon(SeenArea& seen, const SightBuild& build);
		void		followEase(const SightBuild& build, std::int64_t rimEaseMm, std::int64_t walkedMm);
		[[nodiscard]] SideOffsets eased() const;

		Leg							   leg;
		Leg							   stretch; // on the polygon path, a leg not pending: where his walk turns
		std::vector<geometry::Vec2i64> walk;	// his path since the last shape that went in, for the sweep
		SightRanges					   walkStartRanges{};
		bool						   sweepWanted = false;
		std::int64_t				   sightMm	   = 0;
		bool						   wallSight   = false;
		std::uint64_t				   polygonSeen = 0; // the last polygon-path shape's revision taken
		Ease						   ease;
		bool						   placed = false; // `lastPosition` holds where he stood last frame
		geometry::Vec2i64			   lastPosition;
		bool						   builtOnce = false; // `lastBuild` holds the build he followed last frame
		std::uint64_t				   lastBuild = 0;
		std::size_t					   opened = 0;
		std::array<std::size_t, static_cast<std::size_t>(LegEnd::Count)> endedBy{};
	};

} // namespace vision

#pragma once

// Internal to SnapEngine: the length step (building-construction D12, Common lengths and
// The length step). Exposed so SnapLengths.test.cpp can read what the study decided (which
// entries were dropped and why, how they grouped, which groups were kept). Not for use
// outside SnapEngine.cpp and the tests.
//
// Positions are double world meters, as in SnapGuidesDetail.h. The vectors in LengthStudy
// are cleared, never shrunk, so a study reused across calls stops allocating once it has
// seen its largest input.

#include "SnapEngine.h"
#include "SnapGuidesDetail.h"

#include <core/Vec2d.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace assets {
	struct SnappingConfig;
}

namespace construction::snap_lengths {

	using geometry::Vec2d;

	/// Member and Merged differ only for the tests; the step reads whether an entry was dropped.
	enum class LengthEntryStatus {
		Member,		  // in a group, within 1 mm of its length
		Merged,		  // in a group whose length differs from its own by more than 1 mm
		UnderMinimum, // shorter than commonLengthMinimumMeters
		OutOfRange,	  // its owner is farther than commonLengthRangeMeters from the cursor
	};

	/// An owner whose bounds are farther than the range by more than this lends no entries at
	/// all (not even OutOfRange ones): bounds and edge distances round differently, and this is
	/// far above double rounding at world scale, so every owner skipped is one the range drops.
	inline constexpr double kRangeCullSlackMeters = 1e-6;

	/// The shape ranks newest of all.
	inline constexpr std::uint64_t kShapeRecency = std::numeric_limits<std::uint64_t>::max();

	struct LengthEntry {
		Vec2d			  start;
		Vec2d			  end;
		double			  lengthMeters = 0.0;
		std::uint64_t	  recency = 0;		 // placement order; kShapeRecency for the shape
		double			  ownerMeters = 0.0; // the cursor's distance to the owner's nearest edge
		std::size_t		  order = 0;		 // generation order
		LengthEntryStatus status = LengthEntryStatus::Member;
		std::size_t		  group = 0; // index into LengthStudy::groups, for Member and Merged
	};

	struct LengthGroup {
		double		  lengthMeters = 0.0; // its first entry's
		std::size_t	  memberCount = 0;
		std::uint64_t newest = 0;
		double		  nearestOwnerMeters = 0.0;
	};

	/// A run of wall segments sharing one placement order, while collecting entries.
	struct ChainRun {
		std::size_t first = 0; // index into LengthStudy::chainSegments
		std::size_t count = 0;
	};

	struct LengthStudy {
		// Generation order: the shape, foundations, then wall chains. An owner culled by its
		// bounds (kRangeCullSlackMeters) has none.
		std::vector<LengthEntry> entries;
		std::vector<std::size_t> walk;	  // the entries not dropped, highest priority first
		std::vector<LengthGroup> groups;  // in the order they were created
		std::vector<std::size_t> ranked;  // indices into groups, best first
		std::size_t				 keptCount = 0; // the first keptCount of ranked are kept

		// Scratch while collecting wall chains: (placement order, index into segments()).
		std::vector<std::pair<std::uint64_t, std::size_t>> chainSegments;
		std::vector<ChainRun>							   chains;
	};

	/// Every entry, its filter, the groups, and their ranking (Common lengths, steps 1 to 4).
	void studyCommonLengths(const snap_guides::GuideContext& context, LengthStudy& study);

	/// Where a circle of radius `length` round `segmentStart` meets the line through
	/// `lineOrigin` along the unit `lineDirection`: the meeting nearer `nearPoint`, or none
	/// when the line passes farther than `length` from the start.
	[[nodiscard]] std::optional<Vec2d>
	targetAtLength(Vec2d segmentStart, double length, Vec2d lineOrigin, Vec2d lineDirection, Vec2d nearPoint);

	/// What the length step reads for one snap.
	struct LengthStep {
		const assets::SnappingConfig* snapping = nullptr;
		const LengthStudy*			  study = nullptr; // set when common lengths are on and not freeform
		bool						  rounding = false; // roundLengths on, an increment above zero, not freeform
		std::optional<Vec2d>		  segmentStart;		// the last placed point
	};

	/// Runs the study into `scratch` when common lengths are on, and says what the step may do.
	[[nodiscard]] LengthStep prepareLengthStep(const snap_guides::GuideContext& context, bool freeform, LengthStudy& scratch);

	/// A point the length step moved, and why.
	struct LengthRefinement {
		LengthSnapKind kind = LengthSnapKind::None;
		Vec2d		   point;
		double		   lengthMeters = 0.0;
		Vec2d		   measuredFrom; // the segment's start, or the guide's node for RoundedAlongGuide
		std::size_t	   group = 0;	 // index into LengthStudy::groups, for CommonLength
		double		   slideMeters = 0.0;
	};

	/// The length step along a line (`refine` in D12): a common length that catches, else,
	/// when `allowRounding`, the segment's length rounded to the increment. None when the
	/// point stays.
	[[nodiscard]] std::optional<LengthRefinement>
	refineLength(const LengthStep& step, Vec2d lineOrigin, Vec2d lineDirection, Vec2d currentPoint, bool allowRounding);

	/// The distance along a guide from its node to `currentPoint`, rounded to the increment.
	/// None when it rounds to zero. The caller checks LengthStep::rounding.
	[[nodiscard]] std::optional<LengthRefinement> roundAlongGuide(const LengthStep& step, Vec2d node, Vec2d direction, Vec2d currentPoint);

} // namespace construction::snap_lengths

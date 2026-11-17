#pragma once

// Bounding a colonist's thought record. When it reaches the start size, repeats merge and then the least
// memorable thoughts drop until it is back at the stop size. Held and major thoughts, the first of each
// event type, the first about each subject, and the recent ones are never touched. No randomness: the
// same record always prunes the same way. See /docs/design/game-systems/colonists/thoughts.md (What is kept).

#include "components/Thoughts.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ecs {

	/// The <kept> tuning in the sim's units.
	struct ThoughtKeeping {
		/// The record size pruning starts at, and the size it brings the record back to; stop is below start.
		std::size_t pruneStartCount = 0;
		std::size_t pruneStopCount = 0;
		/// A thought whose last moment is this recent is never pruned.
		double recentSimulationSeconds = 0.0;
		float  trivialWeight = 0.0F;
		float  normalWeight = 0.0F;
	};

	/// Working space a pruner keeps between prunes, so pruning allocates only while the record grows.
	struct ThoughtPruneScratch {
		struct Run {
			uint32_t begin = 0;
			uint32_t end = 0;
		};
		struct Scored {
			double	 score = 0.0;
			uint32_t index = 0;
		};
		std::vector<uint32_t> order;
		std::vector<uint8_t>  fates;
		std::vector<Run>	  runs;
		std::vector<Scored>	  scored;
	};

	/// The latest of a thought's arrivals and its end. Recency and age are measured from it.
	[[nodiscard]] double thoughtLastMoment(const Thought& thought);

	/// importance x (1 + size of one mood effect) x (1 + count) / age in game days. Not for a major thought.
	[[nodiscard]] double thoughtPruneScore(const Thought& thought, const ThoughtKeeping& keeping, double now);

	/// Prunes when the record holds pruneStartCount thoughts or more: first merging the oldest runs of
	/// repeats, then dropping the lowest scores, the oldest first on a tie, until it holds pruneStopCount
	/// or nothing prunable is left. Bumps the revision and returns true when it changed the record.
	/// O(n log n) in the record's size.
	bool pruneThoughts(Thoughts& thoughts, const ThoughtKeeping& keeping, double now, ThoughtPruneScratch& scratch);

} // namespace ecs

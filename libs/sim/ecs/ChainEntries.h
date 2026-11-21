#pragma once

// Every chain of the behavior library resolved once at load to what the chain runner reads beside the library's own
// handles: the sim's need for a `<need>` step and the thought type of a `<think>` step, so no step reads an id string
// when it runs. resolveChainEntries holds the runner's rules, each a load error naming the file, the chain, and the
// step:
//
// - a `<need>` step names a need the sim tracks, and a `<think>` step a thought type;
// - `<run>` and unmet `<have>` steps nest at most ChainProgress::kMaxDepth chains deep (the loader already refuses a
//   chain that runs itself);
// - a step names `inbound` only with `in_flight`, which the claim index pairs, and never on a take (the loader already
//   holds a chain to the four claims a task holds);
// - a `<do>` step's action (each member of a group) needs the target data of one effect row kind: a take, a put, a
//   work, a craft, or an install (a take paired with an install, both on one packaged thing);
// - a `<do>` whose action acts on a target has `on` or a `<go>` before it, and an action naming what it acts on is not
//   used on what its `<go>`'s query never returns (a harvest after a query of piles);
// - a `<drop>` has a take before it, and `held_gone` ends only a `<do>`;
// - `<leave_colony/>` is refused: it ends colony membership, which the sim does not have.
//
// See /docs/technical/queries-and-chains.md (Chains, The runner).

#include "ActionEntries.h"
#include "components/Needs.h"
#include "components/Thoughts.h"

#include <assets/Entries.h>

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ecs {

	class ThoughtTypes;

	/// What one step reads beyond its library entry.
	struct ChainStepValues {
		/// A `<need>` step's need.
		NeedType need = NeedType::Count;
		/// A `<think>` step's thought type.
		ThoughtEventTypeId thought = 0;
	};

	struct ChainEntries {
		/// Per chain, by its handle's index, the index of its first step in `steps`.
		std::vector<uint32_t>		 firstStep;
		std::vector<ChainStepValues> steps;

		[[nodiscard]] const ChainStepValues& of(assets::EntryHandle<assets::ChainEntry> chain, uint8_t step) const {
			assert(chain.valid() && chain.index < firstStep.size() && "a chain of a table that resolved without errors");
			return steps[firstStep[chain.index] + step];
		}
	};

	struct ChainEntriesLoad {
		ChainEntries			 chains;
		std::vector<std::string> errors;
	};

	/// Every chain of `library` with its steps resolved, and one error for each fault the runner's rules (above) find.
	/// `thoughts` resolves `<think>` steps; with none, a `<think>` step is an error.
	[[nodiscard]] ChainEntriesLoad
	resolveChainEntries(const assets::EntryLibrary& library, const ActionEntries& actions, const ThoughtTypes* thoughts);

} // namespace ecs

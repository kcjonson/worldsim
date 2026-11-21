#pragma once

// A running chain's progress, held on its Task: which chain and step, the targets resolved so far, when the step
// began, and what its takes lifted. Engine state, so the UI can show it; nothing of it survives Task::clear(), so an
// interrupted chain never resumes from its step. See /docs/technical/queries-and-chains.md (Chains, The runner).

#include "../EntityID.h"

#include <assets/Entries.h>

#include <glm/vec2.hpp>

#include <array>
#include <cstdint>

namespace ecs {

	/// One target of a chain: a thing it remembers, a box the colony knows (with one item), a creature, a place, or an
	/// entity the option names (a goal's destination, a station, a blueprint, a packaged thing).
	struct ChainTarget {
		enum class Kind : uint8_t { None, Thing, Box, Mover, Place, Entity };
		Kind kind = Kind::None;
		/// A thing's memory key, a box's world key; 0 otherwise.
		uint64_t key = 0;
		/// A box, a creature, or an entity target; kInvalidEntity for a thing or a place.
		EntityID entity = kInvalidEntity;
		uint32_t defNameId = 0;
		/// What it is a source of: a pile's or a box's item, a harvestable's yield.
		uint32_t  item = 0;
		glm::vec2 position{0.0F, 0.0F};
		/// Where a route to it ends: itself, or the stand point beside the obstacle it lies in.
		glm::vec2 goal{0.0F, 0.0F};
		/// The claim a step naming one takes on it (QueryCandidate's claim): use for a thing taken whole, source quantity
		/// for a pile or a box with the units it takes.
		assets::ClaimKind claimKind = assets::ClaimKind::Use;
		/// The claim's key space (ecs::TargetKind) and key.
		uint8_t	 claimSpace = 0;
		uint64_t claimKey = 0;
		uint32_t claimQuantity = 0;

		[[nodiscard]] bool resolved() const { return kind != Kind::None; }
	};

	/// One chain being run: the outermost is the task's chain, each `run` or unmet `have` adds the chain it runs.
	struct ChainFrame {
		assets::EntryHandle<assets::ChainEntry> chain;
		uint8_t									step = 0;
	};

	/// The goal a work chain serves, which sizes its inbound and in-flight claims and the item a `have` names as
	/// `goal`; none for a need's, an idle, or an order's chain.
	struct ChainGoal {
		uint64_t rootGoalId = 0;
		uint32_t item = 0;
		/// The chain's planned amount: what a take asks for, and what a `have` is met by.
		uint32_t quantity = 0;
	};

	struct ChainProgress {
		/// How deep `run` and unmet `have` steps may nest; deeper is a load error (resolveChainEntries).
		static constexpr uint8_t kMaxDepth = 4;

		std::array<ChainFrame, kMaxDepth> frames{};
		/// Frames in use; 0 while no chain runs.
		uint8_t depth = 0;
		/// The option's targets, which `<go to>` and `<do on>` name.
		ChainTarget target;
		ChainTarget source;
		ChainTarget destination;
		/// The target the last `go` walked to, which a `do` with no `on` acts on.
		ChainTarget stepTarget;
		ChainGoal	goal;
		/// TimeSystem::simulationSeconds when the step began.
		double stepStartedAt = 0.0;
		/// What the chain's takes lifted, which `drop` sets down and a `have` counts as handed over.
		uint32_t takenItem = 0;
		uint32_t takenQuantity = 0;
		/// The units the creature held for the step when it began, of the item `held_gone` watches; 0 for none.
		uint32_t heldItem = 0;
		uint32_t heldAtStart = 0;

		[[nodiscard]] bool				active() const { return depth > 0; }
		[[nodiscard]] const ChainFrame& frame() const { return frames[depth - 1]; }
		[[nodiscard]] ChainFrame&		frame() { return frames[depth - 1]; }
	};

} // namespace ecs

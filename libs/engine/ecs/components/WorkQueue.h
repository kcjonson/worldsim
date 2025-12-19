#pragma once

// WorkQueue Component
// Per-station job queue for crafting work.
// Stores player-queued crafting jobs that colonists can pick up and execute.
//
// See /docs/design/game-systems/colonists/technology-discovery.md for design details.

#include <cstdint>
#include <string>
#include <vector>

namespace ecs {

/// A single crafting job in the queue
struct CraftingJob {
	std::string recipeDefName;	///< Recipe to craft (e.g., "Recipe_AxePrimitive")
	uint32_t quantity = 1;		///< Total number to craft
	uint32_t completed = 0;		///< Number already completed

	/// Check if this job is finished
	[[nodiscard]] bool isComplete() const { return completed >= quantity; }

	/// Get remaining count
	[[nodiscard]] uint32_t remaining() const { return quantity - completed; }
};

/// Work queue component attached to crafting stations
struct WorkQueue {
	std::vector<CraftingJob> jobs;	///< Queued crafting jobs
	float progress = 0.0F;			///< Progress on current job (0.0 - 1.0)

	/// Add a new job to the queue
	void addJob(const std::string& recipeDefName, uint32_t quantity = 1) {
		// Check if we already have this recipe queued - if so, increase quantity
		for (auto& job : jobs) {
			if (job.recipeDefName == recipeDefName && !job.isComplete()) {
				job.quantity += quantity;
				return;
			}
		}
		// New recipe - add to queue
		jobs.push_back({recipeDefName, quantity, 0});
	}

	/// Get the next incomplete job, or nullptr if queue is empty
	[[nodiscard]] CraftingJob* getNextJob() {
		for (auto& job : jobs) {
			if (!job.isComplete()) {
				return &job;
			}
		}
		return nullptr;
	}

	/// Get the next incomplete job (const version)
	[[nodiscard]] const CraftingJob* getNextJob() const {
		for (const auto& job : jobs) {
			if (!job.isComplete()) {
				return &job;
			}
		}
		return nullptr;
	}

	/// Check if there's any pending work
	[[nodiscard]] bool hasPendingWork() const {
		return getNextJob() != nullptr;
	}

	/// Get total pending item count across all jobs
	[[nodiscard]] uint32_t totalPending() const {
		uint32_t total = 0;
		for (const auto& job : jobs) {
			total += job.remaining();
		}
		return total;
	}

	/// Remove completed jobs from the queue
	void cleanupCompleted() {
		jobs.erase(
			std::remove_if(jobs.begin(), jobs.end(), [](const CraftingJob& job) { return job.isComplete(); }),
			jobs.end()
		);
	}
};

} // namespace ecs

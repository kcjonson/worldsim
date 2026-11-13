#pragma once

// ColonistDetailsModel - Aggregator model for ColonistDetailsDialog
//
// Encapsulates all ECS queries needed for the dossier tabs.
// Each tab's data types are defined in their respective tab view headers.
// Supports per-frame refresh with change detection for live updates.

#include "tabs/BioTabView.h"
#include "tabs/GearTabView.h"
#include "tabs/NeedsTabView.h"
#include "tabs/MemoryTabView.h"
#include "tabs/SocialTabView.h"
#include "tabs/TasksTabView.h"

#include <ecs/EntityID.h>
#include <ecs/World.h>
#include <ecs/components/Task.h>

#include <array>

namespace world_sim {

	/// Aggregator model for ColonistDetailsDialog
	/// Refreshes data for all tabs from ECS world
	class ColonistDetailsModel {
	  public:
		/// Type of update needed after refresh()
		enum class UpdateType {
			None,	   // No change
			Values,	   // Same colonist, values changed (need bars, etc.)
			Structure, // Different colonist or structural change
		};

		/// Refresh model with current colonist data
		/// @param world ECS world (non-const for task queries via GoalTaskRegistry)
		/// @param colonistId Entity ID of the colonist
		/// @param deltaTime Time since last frame (seconds), for the Known work rows' refresh interval
		/// @return Type of update needed
		[[nodiscard]] UpdateType refresh(ecs::World& world, ecs::EntityID colonistId, float deltaTime);

		/// Read the Known work rows on the next refresh, whatever the interval (the dialog reopening).
		void invalidateTasks() { tasksStale = true; }

		/// Check if model has valid data
		[[nodiscard]] bool isValid() const { return valid; }

		/// Get data for each tab
		[[nodiscard]] const BioData&	 bio() const { return bioData; }
		[[nodiscard]] const NeedsData&	 needs() const { return needsData; }
		[[nodiscard]] const SocialData&  social() const { return socialData; }
		[[nodiscard]] const GearData&	 gear() const { return gearData; }
		[[nodiscard]] const MemoryData&  memory() const { return memoryData; }
		[[nodiscard]] const TasksTabData& tasks() const { return tasksData; }

	  private:
		/// Extract Bio tab data
		void extractBioData(const ecs::World& world, ecs::EntityID colonistId);

		/// Extract Needs tab data
		void extractNeedsData(const ecs::World& world, ecs::EntityID colonistId);

		/// Extract Social tab data (placeholder for now)
		void extractSocialData();

		/// Extract Gear tab data
		void extractGearData(const ecs::World& world, ecs::EntityID colonistId);

		/// Extract Memory tab data
		void extractMemoryData(const ecs::World& world, ecs::EntityID colonistId);

		/// Extract Tasks tab data: the Currently panel every frame, the Known work rows on the global task
		/// panel's refresh interval (a full pass over the colony's goals) or when `force`d. True when
		/// the rows changed.
		[[nodiscard]] bool extractTasksData(ecs::World& world, ecs::EntityID colonistId, float deltaTime, bool force);

		/// Get mood label from mood value
		[[nodiscard]] static std::string getMoodLabel(float mood);

		// State
		ecs::EntityID currentColonistId{0};
		bool		  valid = false;

		// Cached data for each tab
		BioData		 bioData;
		NeedsData	 needsData;
		SocialData	 socialData;
		GearData	 gearData;
		MemoryData	 memoryData;
		TasksTabData tasksData;

		// Previous values for change detection
		std::array<float, 8> prevNeedValues{};
		float				 prevMood = 0.0F;
		std::string			 prevGearSig;
		size_t				 prevMemoryCount = 0;
		// The bio "Current:" line is derived from task type + state + navState + labels;
		// diffing the rendered string catches every change to it without enumerating inputs.
		std::string			 prevBioTask;
		float				 prevCurrentProgress = -1.0F;
		float				 timeSinceTasksRefresh = 0.0F;
		bool				 tasksStale = true;
	};

} // namespace world_sim

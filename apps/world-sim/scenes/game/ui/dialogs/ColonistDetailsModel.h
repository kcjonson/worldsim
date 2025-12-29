#pragma once

// ColonistDetailsModel - Aggregator model for ColonistDetailsDialog
//
// Encapsulates all ECS queries needed for the 5 tabs.
// Each tab's data types are defined in their respective tab view headers.
// Supports per-frame refresh with change detection for live updates.

#include "tabs/BioTabView.h"
#include "tabs/GearTabView.h"
#include "tabs/HealthTabView.h"
#include "tabs/MemoryTabView.h"
#include "tabs/SocialTabView.h"

#include <ecs/EntityID.h>
#include <ecs/World.h>

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
		/// @param world ECS world
		/// @param colonistId Entity ID of the colonist
		/// @return Type of update needed
		[[nodiscard]] UpdateType refresh(const ecs::World& world, ecs::EntityID colonistId);

		/// Check if model has valid data
		[[nodiscard]] bool isValid() const { return valid; }

		/// Get data for each tab
		[[nodiscard]] const BioData&	bio() const { return bioData; }
		[[nodiscard]] const HealthData& health() const { return healthData; }
		[[nodiscard]] const SocialData& social() const { return socialData; }
		[[nodiscard]] const GearData&	gear() const { return gearData; }
		[[nodiscard]] const MemoryData& memory() const { return memoryData; }

	  private:
		/// Extract Bio tab data
		void extractBioData(const ecs::World& world, ecs::EntityID colonistId);

		/// Extract Health tab data
		void extractHealthData(const ecs::World& world, ecs::EntityID colonistId);

		/// Extract Social tab data (placeholder for now)
		void extractSocialData();

		/// Extract Gear tab data
		void extractGearData(const ecs::World& world, ecs::EntityID colonistId);

		/// Extract Memory tab data
		void extractMemoryData(const ecs::World& world, ecs::EntityID colonistId);

		/// Get mood label from mood value
		[[nodiscard]] static std::string getMoodLabel(float mood);

		// State
		ecs::EntityID currentColonistId{0};
		bool		  valid = false;

		// Cached data for each tab
		BioData	   bioData;
		HealthData healthData;
		SocialData socialData;
		GearData   gearData;
		MemoryData memoryData;

		// Previous values for change detection
		std::array<float, 8> prevNeedValues{};
		float				 prevMood = 0.0F;
		size_t				 prevInventorySize = 0;
		size_t				 prevMemoryCount = 0;
	};

} // namespace world_sim

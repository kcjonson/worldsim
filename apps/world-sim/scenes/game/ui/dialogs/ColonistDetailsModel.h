#pragma once

// ColonistDetailsModel - ViewModel for ColonistDetailsDialog
//
// Encapsulates all ECS queries needed for the 5 tabs:
// - Bio: name, placeholder age/traits/background
// - Health: 8 needs, mood
// - Social: placeholder relationships
// - Gear: inventory items
// - Memory: known entities categorized by capability
//
// Supports per-frame refresh with change detection for live updates
// while the game continues running.

#include <assets/AssetRegistry.h>
#include <ecs/EntityID.h>
#include <ecs/World.h>
#include <ecs/components/Inventory.h>

#include <array>
#include <string>
#include <vector>

namespace world_sim {

/// Data for Bio tab
struct BioData {
	std::string name;
	std::string age = "--";					   // Placeholder until age system
	std::vector<std::string> traits;		   // Empty for now
	std::string background = "No background";  // Placeholder
	float mood = 100.0F;					   // 0-100
	std::string moodLabel;					   // "Happy", "Content", etc.
	std::string currentTask;				   // e.g., "Eating", "Wandering"
};

/// Data for Health tab
struct HealthData {
	/// Need values (0-100) for all 8 needs, indexed by NeedType
	std::array<float, 8> needValues{};

	/// Whether each need is below seek threshold
	std::array<bool, 8> needsAttention{};

	/// Whether each need is critical
	std::array<bool, 8> isCritical{};

	float mood = 100.0F;
	std::string moodLabel;
};

/// Data for Social tab (placeholder)
struct SocialData {
	std::string placeholder = "Relationships not yet tracked";
};

/// Data for Gear tab
struct GearData {
	std::vector<ecs::ItemStack> items;
	uint32_t slotCount = 0;
	uint32_t maxSlots = 0;
};

/// A single known entity for Memory tab display
struct MemoryEntity {
	std::string name;	  // e.g., "Berry Bush"
	float x = 0.0F;		  // Position
	float y = 0.0F;
};

/// A category of known entities
struct MemoryCategory {
	std::string name;					 // e.g., "Food Sources"
	std::vector<MemoryEntity> entities;
	size_t count = 0;					 // Total count (may differ if truncated)
};

/// Data for Memory tab
struct MemoryData {
	std::vector<MemoryCategory> categories;
	size_t totalKnown = 0;
};

/// ViewModel for ColonistDetailsDialog
class ColonistDetailsModel {
  public:
	/// Type of update needed after refresh()
	enum class UpdateType {
		None,		 // No change
		Values,		 // Same colonist, values changed (need bars, etc.)
		Structure,	 // Different colonist or structural change
	};

	/// Refresh model with current colonist data
	/// @param world ECS world
	/// @param colonistId Entity ID of the colonist
	/// @return Type of update needed
	[[nodiscard]] UpdateType refresh(const ecs::World& world, ecs::EntityID colonistId);

	/// Check if model has valid data
	[[nodiscard]] bool isValid() const { return valid; }

	/// Get data for each tab
	[[nodiscard]] const BioData& bio() const { return bioData; }
	[[nodiscard]] const HealthData& health() const { return healthData; }
	[[nodiscard]] const SocialData& social() const { return socialData; }
	[[nodiscard]] const GearData& gear() const { return gearData; }
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
	bool valid = false;

	// Cached data for each tab
	BioData bioData;
	HealthData healthData;
	SocialData socialData;
	GearData gearData;
	MemoryData memoryData;

	// Previous values for change detection
	std::array<float, 8> prevNeedValues{};
	float prevMood = 0.0F;
	size_t prevInventorySize = 0;
	size_t prevMemoryCount = 0;
};

} // namespace world_sim

#pragma once

// Work Type Registry
// Central catalog for work categories and work types loaded from XML.
// Read from assets/config/legacy/work-types.xml, second in the work config's load order.
//
// See /docs/design/game-systems/colonists/work-types-config.md for design details.

#include "assets/WorkTypeDef.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace assets {

	/// Central registry for work categories and work types.
	/// Loaded from XML definition files at startup.
	class WorkTypeRegistry {
	  public:
		/// Get the singleton registry instance
		static WorkTypeRegistry& Get();

		// --- Loading ---

		/// Replaces every category and work type with those in `configRoot / file`: false, with problems() saying why
		/// and nothing held, when the file can't be read or has any mistake. Errors name the file as `file` is written.
		bool loadFromFile(const std::filesystem::path& configRoot, const std::filesystem::path& file);

		/// What the file got wrong: each names the file, the <Category> or <WorkType>, and the attribute.
		[[nodiscard]] const std::vector<std::string>& problems() const { return problemList; }

		/// Clear all loaded categories and work types
		void clear();

		// --- Category Queries ---

		/// Get a category by defName
		/// @param defName The category definition name (e.g., "Farming")
		/// @return Pointer to category, or nullptr if not found
		[[nodiscard]] const WorkCategoryDef* getCategory(const std::string& defName) const;

		/// Check if a category exists
		[[nodiscard]] bool hasCategory(const std::string& defName) const;

		/// Get all categories sorted by tier (lowest tier first = highest priority)
		[[nodiscard]] std::vector<const WorkCategoryDef*> getAllCategories() const;

		/// Get all category defNames
		[[nodiscard]] std::vector<std::string> getCategoryNames() const;

		// --- Work Type Queries ---

		/// Get a work type by defName
		/// @param defName The work type definition name (e.g., "Work_HarvestWild")
		/// @return Pointer to work type, or nullptr if not found
		[[nodiscard]] const WorkTypeDef* getWorkType(const std::string& defName) const;

		/// Check if a work type exists
		[[nodiscard]] bool hasWorkType(const std::string& defName) const;

		/// Get all work types in a category
		/// @param categoryDefName The category definition name
		/// @return Vector of work types (empty if category not found)
		[[nodiscard]] std::vector<const WorkTypeDef*> getWorkTypesInCategory(const std::string& categoryDefName) const;

		/// Get work types that trigger on a specific capability
		/// @param capabilityName The capability name (e.g., "Harvestable")
		/// @return Vector of matching work types
		[[nodiscard]] std::vector<const WorkTypeDef*> getWorkTypesForCapability(const std::string& capabilityName) const;

		/// Get all work type defNames
		[[nodiscard]] std::vector<std::string> getWorkTypeNames() const;

		// --- Counts ---

		[[nodiscard]] size_t categoryCount() const;
		[[nodiscard]] size_t workTypeCount() const;

	  private:
		WorkTypeRegistry() = default;

		/// Build capability index after loading
		void buildCapabilityIndex();

		// --- Storage ---

		/// All categories by defName
		std::unordered_map<std::string, WorkCategoryDef> categories;

		/// All work types by defName
		std::unordered_map<std::string, WorkTypeDef> workTypes;

		/// Work types indexed by trigger capability
		std::unordered_map<std::string, std::vector<const WorkTypeDef*>> byCapability;

		std::vector<std::string> problemList;
	};

} // namespace assets

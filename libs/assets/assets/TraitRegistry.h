#pragma once

// The traits a colonist can have: an id conditions name (trait(volatile)), a display name, a one-line
// description, how many a colonist gets, and which pairs never go together. A trait carries no effect of
// its own; effects come through conditions in thought and behavior XML. Every .xml file under
// assets/config/colonists/traits adds to the set, so a mod adds or replaces traits with a file of its own.
// See /docs/design/game-systems/colonists/attributes.md (Traits).

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace assets {

	struct TraitDef {
		std::string id;
		std::string name;
		std::string description;
	};

	/// How many traits a colonist gets, from `min` to `max` inclusive.
	struct TraitCount {
		int min = 0;
		int max = 0;
	};

	class TraitRegistry {
	  public:
		/// The game's traits, filled by ecs::loadColonistRules.
		static TraitRegistry& Get();

		/// Replace everything with the .xml files under `folder`: its own files first, then its subfolders'
		/// files, each in path order. A later file replaces a trait of the same id (logged at info level,
		/// naming both files) and may replace <Count>; exclusions accumulate, so a trait in several groups is
		/// excluded from all of their members. Returns every problem found, each naming its file; any problem
		/// leaves the registry empty, and an empty list means the set loaded clean.
		[[nodiscard]] std::vector<std::string> loadFromFolder(const std::filesystem::path& folder);

		void clear();

		[[nodiscard]] bool loaded() const { return !traitList.empty(); }

		/// In the order first declared.
		[[nodiscard]] const std::vector<TraitDef>& traits() const { return traitList; }
		[[nodiscard]] const TraitDef*			   find(std::string_view id) const;
		[[nodiscard]] TraitCount				   count() const { return traitCount; }

		/// True when a declared group lists both traits.
		[[nodiscard]] bool exclusive(std::string_view first, std::string_view second) const;

	  private:
		std::vector<TraitDef>				  traitList;
		TraitCount							  traitCount;
		std::vector<std::vector<std::string>> exclusiveGroups;
	};

} // namespace assets

#pragma once

// The traits a colonist can have: an id conditions name (trait(volatile)), a display name, a one-line
// description, how many a colonist gets, and which pairs never go together. A trait carries no effect of
// its own; effects come through conditions in thought and behavior XML. Every .xml file under
// assets/config/colonists/traits adds to the set, so a mod adds or replaces traits with a file of its own.
// See /docs/design/game-systems/colonists/attributes.md (Traits).

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace assets {

	struct TraitDef {
		std::string id;
		std::string name;
		std::string description;
	};

	/// How many traits a colonist gets, from `min` to `max` inclusive. `max` is an upper bound: it may be
	/// more than the exclusive groups let one colonist hold.
	struct TraitCount {
		int min = 0;
		int max = 0;
	};

	class TraitRegistry {
	  public:
		/// Replace everything with the .xml files under `folder`: its own files first, then its subfolders'
		/// files, each in path order. A later file replaces a trait of the same id (logged at info level,
		/// naming both files) and may replace <Count>; exclusions accumulate, so a trait in several groups is
		/// excluded from all of their members. Returns every problem found, each naming its file; any problem
		/// leaves the registry empty, and an empty list means the set loaded clean.
		[[nodiscard]] std::vector<std::string> loadFromFolder(const std::filesystem::path& folder);

		/// In the order first declared; an index into this is what reachable() takes.
		[[nodiscard]] const std::vector<TraitDef>& traits() const { return traitList; }
		[[nodiscard]] const TraitDef*			   find(std::string_view id) const;
		[[nodiscard]] TraitCount				   count() const { return traitCount; }

		/// True when a declared group lists both traits.
		[[nodiscard]] bool exclusive(std::string_view first, std::string_view second) const;

		/// Whether one colonist can hold `wanted` traits at once, `held` (indices into traits()) among them,
		/// with no two exclusive. The load checks <Count> min with it, and the roll each draw.
		[[nodiscard]] bool reachable(std::span<const std::size_t> held, std::size_t wanted) const;

	  private:
		void clear();
		[[nodiscard]] bool		  fits(const std::vector<std::size_t>& candidates, std::size_t wanted) const;
		[[nodiscard]] std::size_t mostHeldOf(const std::vector<std::size_t>& candidates) const;

		std::vector<TraitDef> traitList;
		TraitCount			  traitCount;
		/// By index into traitList: each group's members, and for each trait the traits it excludes.
		std::vector<std::vector<std::size_t>> exclusiveGroups;
		std::vector<std::vector<bool>>		  excludes;
	};

} // namespace assets

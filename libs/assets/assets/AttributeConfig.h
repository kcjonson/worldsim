#pragma once

// How each personality and physical attribute is rolled for a new colonist: a range, drawn from evenly.
// Every .xml file under assets/config/colonists/attributes adds to the set, so a mod replaces an
// attribute's range with a file of its own. The names an attribute can have belong to the sim, which
// passes them in. See /docs/design/game-systems/colonists/attributes.md (Rolling).

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace assets {

	/// The scale every attribute lives on.
	inline constexpr float kAttributeScaleMin = 0.0F;
	inline constexpr float kAttributeScaleMax = 20.0F;

	struct AttributeRange {
		std::string name;
		float		min = 0.0F;
		float		max = 0.0F;
	};

	struct AttributeConfigLoad {
		/// One per name given to the load, in that order; empty when there are errors.
		std::vector<AttributeRange> ranges;
		std::vector<std::string>	errors;
	};

	/// Load the .xml files under `folder`: its own files first, then its subfolders' files, each in path
	/// order. A later file replaces an attribute of the same name (logged at info level, naming both
	/// files). Every name in `names` has to be given a range exactly once after replacements, and a name
	/// not in `names` is an error. Every problem found is returned naming its file and the
	/// <Attribute name="...">; any problem leaves `ranges` empty.
	[[nodiscard]] AttributeConfigLoad loadAttributeConfig(const std::filesystem::path& folder, std::span<const std::string_view> names);

} // namespace assets

#pragma once

// How each personality and physical attribute is rolled for a new colonist: a range, drawn from evenly.
// Every file with an <Attributes> root in the human's folder (assets/world/creatures/Human) adds to the
// set, so a later file replaces an attribute's range. The names an attribute can have belong to the sim,
// which passes them in. See /docs/design/game-systems/colonists/attributes.md (Rolling).

#include "assets/ConfigFiles.h"

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
		float min = 0.0F;
		float max = 0.0F;
	};

	struct AttributeConfigLoad {
		/// One per name given to the load, in that order; empty when there are errors.
		std::vector<AttributeRange> ranges;
		std::vector<std::string>	errors;
	};

	/// Load `found`'s files, in their order, each named relative to `folder`. A later file replaces an
	/// attribute of the same name (logged at info level, naming both files). Every name in `names` has to be
	/// given a range exactly once after replacements, and a name not in `names` is an error. `found`'s error,
	/// or every problem found, is returned naming its file and the <Attribute name="...">; any problem leaves
	/// `ranges` empty.
	[[nodiscard]] AttributeConfigLoad
	loadAttributeConfig(const ConfigFiles& found, const std::filesystem::path& folder, std::span<const std::string_view> names);

} // namespace assets

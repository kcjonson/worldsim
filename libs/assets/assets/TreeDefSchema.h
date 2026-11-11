#pragma once

// The tree variety elements of an asset def (docs/technical/asset-system/tree-variety.md, R2):
// <ageClasses> and the placement scale range <placement><scale min max/>. The loader and
// AssetValidator read both through these functions, so they agree on what is valid.

#include "assets/AssetDefinition.h"

#include <optional>
#include <string>
#include <vector>

namespace pugi {
	class xml_node;
}

namespace assets {

	/// Age class weights must sum to 1 within this.
	inline constexpr float kAgeWeightTolerance = 0.001F;

	/// An element that breaks the schema: which one, why, and the value as written.
	struct TreeDefIssue {
		std::string field;
		std::string message;
		std::string value;
	};

	/// The def's <ageClasses>, or empty when it has none. With any issue the whole block is
	/// refused (empty, with the issues in `issues`), so the def keeps one age class.
	std::vector<AgeClass> readAgeClasses(const pugi::xml_node& defNode, std::vector<TreeDefIssue>& issues);

	/// The def's <placement><scale>, or nullopt when it has none or it is refused (with an issue).
	std::optional<PlacementScale> readPlacementScale(const pugi::xml_node& defNode, std::vector<TreeDefIssue>& issues);

} // namespace assets

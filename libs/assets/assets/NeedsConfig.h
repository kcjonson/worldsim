#pragma once

// Need tuning as data: each need's thresholds, the levels a need moves between, and the most mood it
// costs. Loaded from the folder assets/config/colonists/needs. Need names are the sim's need labels, which
// the sim checks. See /docs/technical/interruptable-crafts.md (Needs in three levels) and
// /docs/design/game-systems/colonists/needs.md (What a need costs mood).

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace assets {

	/// The names a need's thresholds go by, highest first: the attributes of a <Need>. A need is at its
	/// top level at or above the first, and one level lower below each.
	inline constexpr std::array<const char*, 3> kNeedThresholdNames = {"seek", "urgent", "critical"};

	/// A need's thresholds, as need percentages, in kNeedThresholdNames order, its mood cost, and the file
	/// that gave them.
	struct NeedDef {
		std::string									  need;
		std::array<float, kNeedThresholdNames.size()> thresholds{};
		/// The mood points the need takes off at its worst, a penalty of 1; 0 leaves it out of mood.
		float										  moodCost = 0.0F;
		std::string									  source;
	};

	struct NeedsConfig {
		std::vector<NeedDef> needs;
	};

	struct NeedsConfigLoad {
		NeedsConfig				 config;
		std::vector<std::string> errors;
	};

	/// Read every .xml file under `folder` (configFilesIn's order), each a <Needs> root holding
	/// <Need name seek urgent critical cost/> elements. Each threshold is a number from 0 to 100, below the
	/// one before it, and the cost is a number, zero or more. A later file's <Need> replaces an earlier one
	/// of the same name, cost included, which is logged. Unknown elements and attributes, a missing or
	/// malformed threshold or cost, and a need named twice in one file are errors naming the file and the
	/// <Need>; a file with any error adds nothing. An empty error list means
	/// it loaded clean.
	[[nodiscard]] NeedsConfigLoad loadNeedsConfig(const std::filesystem::path& folder);

	/// The index of a threshold name in kNeedThresholdNames; size() when it names none.
	[[nodiscard]] std::size_t needThresholdIndex(const std::string& name);

} // namespace assets

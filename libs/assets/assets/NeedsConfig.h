#pragma once

// Need tuning as data: each need's thresholds, the levels a need moves between. Loaded from the folder
// assets/config/colonists/needs. Need names are the sim's need labels, which the sim checks.
// See /docs/technical/interruptable-crafts.md (Needs in three levels).

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace assets {

	/// The names a need's thresholds go by, highest first: the attributes of a <Need>. A need is at its
	/// top level at or above the first, and one level lower below each.
	inline constexpr std::array<const char*, 3> kNeedThresholdNames = {"seek", "urgent", "critical"};

	/// A need's thresholds, as need percentages, in kNeedThresholdNames order, and the file that gave them.
	struct NeedThresholdsDef {
		std::string									  need;
		std::array<float, kNeedThresholdNames.size()> thresholds{};
		std::string									  source;
	};

	struct NeedsConfig {
		std::vector<NeedThresholdsDef> needs;
	};

	struct NeedsConfigLoad {
		NeedsConfig				 config;
		std::vector<std::string> errors;
	};

	/// Read every .xml file under `folder` (configFilesIn's order), each a <Needs> root holding
	/// <Need name seek urgent critical/> elements. Each threshold is a number from 0 to 100, below the one
	/// before it. A later file's <Need> replaces an earlier one of the same name, which is logged. Unknown
	/// elements and attributes, a missing or malformed threshold, and a need named twice in one file are
	/// errors naming the file and the <Need>; a file with any error adds nothing. An empty error list means
	/// it loaded clean.
	[[nodiscard]] NeedsConfigLoad loadNeedsConfig(const std::filesystem::path& folder);

	/// The index of a threshold name in kNeedThresholdNames; size() when it names none.
	[[nodiscard]] std::size_t needThresholdIndex(const std::string& name);

} // namespace assets

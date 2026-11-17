#pragma once

// Need tuning as data: each need's thresholds, the levels a need moves between. Loaded from
// assets/config/colonists/needs.xml. Need names are the sim's need labels, which the sim checks.
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

	/// A need's thresholds, as need percentages, in kNeedThresholdNames order.
	struct NeedThresholdsDef {
		std::string									  need;
		std::array<float, kNeedThresholdNames.size()> thresholds{};
	};

	struct NeedsConfig {
		std::vector<NeedThresholdsDef> needs;
	};

	struct NeedsConfigLoad {
		NeedsConfig				 config;
		std::vector<std::string> errors;
	};

	/// Read `file`, whose root is <Needs> holding one <Need name seek urgent critical/> per need. Each
	/// threshold is a number from 0 to 100, and each is below the one before it. Unknown elements and
	/// attributes, a missing or malformed threshold, and a need named twice are errors naming the file. An
	/// empty error list means it loaded clean.
	[[nodiscard]] NeedsConfigLoad loadNeedsConfig(const std::filesystem::path& file);

	/// The index of a threshold name in kNeedThresholdNames; size() when it names none.
	[[nodiscard]] std::size_t needThresholdIndex(const std::string& name);

} // namespace assets

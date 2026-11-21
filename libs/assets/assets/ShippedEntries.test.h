#pragma once

// The shipped behavior library's sources for tests and benchmarks: assets/config, every creature folder, and the
// shipped asset definitions' names, found as the game finds them.

#include "assets/CreatureConfigFiles.h"
#include "assets/EntryLoader.h"

#include <assets-testing/DefinitionNames.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace assets::testing {

	inline std::filesystem::path shippedRepoRoot() {
		// <root>/libs/assets/assets/ShippedEntries.test.h -> <root>
		return std::filesystem::path(__FILE__).lexically_normal().parent_path().parent_path().parent_path().parent_path();
	}

	/// The shipped asset definitions' names, read from every definition file under assets/world, with ids in the order
	/// found: enough to tell whether an entry names a definition that exists. The ids are not the asset registry's.
	inline const std::map<std::string, uint32_t, std::less<>>& shippedDefinitionNames() {
		static const std::map<std::string, uint32_t, std::less<>> names = [] {
			std::map<std::string, uint32_t, std::less<>> found;
			for (std::string& name : definitionNamesIn(shippedRepoRoot() / "assets" / "world")) {
				found.try_emplace(std::move(name), static_cast<uint32_t>(found.size()));
			}
			return found;
		}();
		return names;
	}

	/// The shipped config's sources, with the ladder's `rungs` and the sim's `trackedNeeds`; every creature folder's
	/// structural errors are in `errors`.
	struct ShippedEntrySources {
		EntrySources			 sources;
		std::vector<std::string> errors;
	};

	inline ShippedEntrySources shippedEntrySources(std::vector<std::string> rungs, std::vector<std::string> trackedNeeds) {
		ShippedEntrySources shipped;
		shipped.sources.configRoot = shippedRepoRoot() / "assets" / "config";
		shipped.sources.rungs = std::move(rungs);
		shipped.sources.trackedNeeds = std::move(trackedNeeds);
		shipped.sources.definitionId = [](std::string_view name) -> std::optional<uint32_t> {
			const auto found = shippedDefinitionNames().find(name);
			return found == shippedDefinitionNames().end() ? std::nullopt : std::optional<uint32_t>(found->second);
		};
		std::vector<std::filesystem::path> folders;
		for (const auto& folder : std::filesystem::directory_iterator(shippedRepoRoot() / "assets" / "world" / "creatures")) {
			if (folder.is_directory()) {
				folders.push_back(folder.path());
			}
		}
		std::ranges::sort(folders);
		for (const std::filesystem::path& folder : folders) {
			CreatureConfigFiles files = creatureConfigFiles(folder);
			shipped.errors.insert(shipped.errors.end(), files.errors.begin(), files.errors.end());
			shipped.sources.creatures.push_back(std::move(files));
		}
		return shipped;
	}

} // namespace assets::testing

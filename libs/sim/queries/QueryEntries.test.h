#pragma once

// Query entries for tests, read by the real loader: the XML is written as one behavior file under a temp
// folder unique to the run, beside the shipped fact tuning, and loaded with loadEntries.

#include <assets/AssetRegistry.h>
#include <assets/EntryLoader.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace queries::testing {

	/// The library holding `queries` (the inside of a `<behavior>`, as `behaviors/test/sample.xml` writes it, so
	/// its ids are `test/<id>`) and `workQueries` (the inside of a `<colonyWork>` in `work/`, top-level ids, where
	/// a query may read the goal), with the shipped facts/ tuning. A load error fails the test and leaves the
	/// library empty.
	inline assets::EntryLibrary loadQueryEntries(const std::string& queries, const std::string& workQueries = {}) {
		static const std::string runToken = [] {
			std::random_device device;
			return std::to_string(device()) + "_" + std::to_string(device());
		}();
		static int					loads = 0;
		const std::filesystem::path root =
			std::filesystem::temp_directory_path() / ("worldsim-queries-" + runToken + "-" + std::to_string(++loads));
		std::filesystem::remove_all(root);
		for (const char* folder : {"behaviors/test", "actions", "work", "orders", "facts"}) {
			std::filesystem::create_directories(root / folder);
		}
		std::filesystem::copy_file(std::filesystem::path(SIM_TEST_REPO_DIR) / "assets/config/facts/facts.xml", root / "facts/facts.xml");
		std::ofstream(root / "behaviors/test/sample.xml") << "<behavior id=\"sample\">" << queries << "</behavior>";
		if (!workQueries.empty()) {
			std::ofstream(root / "work/queries.xml") << "<colonyWork>" << workQueries << "</colonyWork>";
		}

		const assets::AssetRegistry& registry = assets::AssetRegistry::Get();
		const assets::EntrySources	 sources{
			  .configRoot = root,
			  .definitionId = [&registry](std::string_view name) -> std::optional<uint32_t> {
				  const uint32_t id = registry.getDefNameId(std::string(name));
				  return registry.getDefinitionById(id) == nullptr ? std::nullopt : std::optional<uint32_t>(id);
			  },
		  };
		assets::EntryLoad loaded = assets::loadEntries(sources);
		std::filesystem::remove_all(root);
		EXPECT_TRUE(loaded.errors.empty()) << ::testing::PrintToString(loaded.errors);
		return std::move(loaded.library);
	}

} // namespace queries::testing

#pragma once

// Query entries for tests, read by the real loader: the XML is written as one behavior file under a temp folder
// unique to the run, beside the shipped fact tuning, and loaded with loadEntries (ecs::testing::SyntheticEntries).

#include <ecs/SyntheticEntries.test.h>

#include <gtest/gtest.h>

#include <string>
#include <utility>

namespace queries::test_entries {

	/// The library holding `queries` (the inside of a `<behavior>`, as `behaviors/test/sample.xml` writes it, so
	/// its ids are `test/<id>`) and `workQueries` (the inside of a `<colonyWork>` in `work/`, top-level ids, where
	/// a query may read the goal), with the shipped facts/ tuning. A load error fails the test and leaves the
	/// library empty.
	inline assets::EntryLibrary loadQueryEntries(const std::string& queries, const std::string& workQueries = {}) {
		const ecs::testing::SyntheticEntries entries("queries");
		entries.write("behaviors/test/sample.xml", "<behavior id=\"sample\">" + queries + "</behavior>");
		if (!workQueries.empty()) {
			entries.write("work/queries.xml", "<colonyWork>" + workQueries + "</colonyWork>");
		}
		assets::EntryLoad loaded = entries.load();
		EXPECT_TRUE(loaded.errors.empty()) << ::testing::PrintToString(loaded.errors);
		return std::move(loaded.library);
	}

} // namespace queries::test_entries

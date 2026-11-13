#include "TaskRowText.h"

#include <gtest/gtest.h>

#include <vector>

namespace world_sim::adapters {

	TEST(TaskRowText, NamesTwoThenCountsTheRest) {
		const std::vector<std::string> names{"Alice", "Bob", "Cara", "Dev"};
		EXPECT_EQ(joinNames(std::span(names).first(0)), "");
		EXPECT_EQ(joinNames(std::span(names).first(1)), "Alice");
		EXPECT_EQ(joinNames(std::span(names).first(2)), "Alice, Bob");
		EXPECT_EQ(joinNames(std::span(names).first(3)), "Alice, Bob +1");
		EXPECT_EQ(joinNames(names), "Alice, Bob +2");
	}

	TEST(TaskRowText, QuantityLeavesOutAZeroPart) {
		EXPECT_EQ(quantityText(12, 4), "needs 12, 4 on the way");
		EXPECT_EQ(quantityText(12, 0), "needs 12");
		EXPECT_EQ(quantityText(0, 4), "4 on the way");
		EXPECT_EQ(quantityText(0, 0), "");
	}

	// Each status wins over every one after it, so each step drops the fact that set the one before.
	TEST(TaskRowText, StatusPrecedence) {
		TaskRowFacts facts{
			.blocked = true,
			.noSource = true,
			.allSourcesClaimed = true,
			.distance = 80.0F,
			.workers = "Alice",
		};
		const auto status = [&] { return taskRowText(facts).status; };
		EXPECT_EQ(status(), "Blocked");
		EXPECT_TRUE(taskRowText(facts).blocked);
		facts.blocked = false;
		EXPECT_EQ(status(), "Waiting (no source found)");
		EXPECT_TRUE(taskRowText(facts).blocked);
		facts.noSource = false;
		EXPECT_EQ(status(), "Working");
		EXPECT_FALSE(taskRowText(facts).blocked);
		facts.workers.clear();
		EXPECT_EQ(status(), "Waiting (all known sources claimed)");
		EXPECT_TRUE(taskRowText(facts).blocked);
		facts.allSourcesClaimed = false;
		EXPECT_EQ(status(), "Available");
		EXPECT_EQ(taskRowText(facts).detail, "far");
		facts.distance = 50.0F;
		EXPECT_EQ(status(), "Unassigned");
		EXPECT_TRUE(taskRowText(facts).unassigned);
	}

	TEST(TaskRowText, BuildSiteStatuses) {
		TaskRowFacts facts{.buildSite = true, .buildPercent = 40, .blocked = true, .workers = "Mara"};
		EXPECT_EQ(taskRowText(facts).status, "Needs materials") << "a site's own status, whatever the goal's";
		EXPECT_EQ(taskRowText(facts).detail, "Mara");
		EXPECT_TRUE(taskRowText(facts).blocked);
		facts.underConstruction = true;
		EXPECT_EQ(taskRowText(facts).status, "Building");
		EXPECT_EQ(taskRowText(facts).detail, "Mara (40%)");
		facts.workers.clear();
		facts.knownTo = "Rin";
		EXPECT_EQ(taskRowText(facts).status, "Ready to build");
		EXPECT_EQ(taskRowText(facts).detail, "40%; known to Rin");
	}

	TEST(TaskRowText, DetailJoinsItsPartsLeavingOutEmptyOnes) {
		TaskRowFacts facts{.needed = 8, .onTheWay = 4, .workers = "Idris", .knownTo = "Rin"};
		EXPECT_EQ(taskRowText(facts).detail, "Idris; needs 8, 4 on the way") << "known to only while nobody works it";
		facts.workers.clear();
		EXPECT_EQ(taskRowText(facts).detail, "needs 8, 4 on the way; known to Rin");
		facts.needed = 0;
		facts.onTheWay = 0;
		EXPECT_EQ(taskRowText(facts).detail, "known to Rin");
		facts.knownTo.clear();
		EXPECT_EQ(taskRowText(facts).detail, "");
		facts.blocked = true;
		facts.materials = CraftMaterials{.landed = 2, .required = 5};
		facts.onTheWay = 3;
		EXPECT_EQ(taskRowText(facts).detail, "2/5 materials; 3 on the way");
	}

} // namespace world_sim::adapters

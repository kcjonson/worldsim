// Unit tests for Work Configuration System
// Tests WorkTypeRegistry, PriorityConfig (the arbitration file), ConfigValidator, and
// loadWorkConfig.

#include "assets/WorkConfig.h"
#include "assets/ConfigValidator.h"
#include "assets/ConstructionRegistry.h"
#include "assets/MemoryConfig.h"
#include "assets/MoodConfig.h"
#include "assets/PriorityConfig.h"
#include "assets/WorkTypeRegistry.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <string>
#include <vector>

using namespace assets;

// ============================================================================
// Test Helpers
// ============================================================================

class WorkConfigTest : public ::testing::Test {
  protected:
	void SetUp() override {
		// Clear all registries before each test
		WorkTypeRegistry::Get().clear();
		PriorityConfig::Get().clear();
		ConstructionRegistry::Get().clear();
		ConfigValidator::clearErrors();
	}

	void TearDown() override {
		// Clean up temp files
		for (const auto& file : tempFiles) {
			std::remove(file.c_str());
		}
		tempFiles.clear();
	}

	// Write content to a temp file and return path. The name includes a
	// process-unique random token so test binaries ctest runs in parallel
	// don't collide, and so stale files from a prior crashed run aren't reused.
	std::string writeTempFile(const std::string& content, const std::string& suffix) {
		static const std::string token = [] {
			std::random_device rd;
			return std::to_string(rd()) + "_" + std::to_string(rd());
		}();
		static std::atomic<int> counter{0};
		std::filesystem::path	path =
			std::filesystem::temp_directory_path() / ("workconfig_" + token + "_" + std::to_string(counter++) + suffix);

		std::ofstream file(path);
		file << content;
		file.close();

		tempFiles.push_back(path.string());
		return path.string();
	}

	/// Loads the temp file at `path` into `config`, naming it by its file name.
	template <typename Config>
	static bool loadTemp(Config& config, const std::string& path) {
		const std::filesystem::path file(path);
		return config.loadFromFile(file.parent_path(), file.filename());
	}

	static std::string fileNameOf(const std::string& path) { return std::filesystem::path(path).filename().string(); }

	std::vector<std::string> tempFiles;
};

// ============================================================================
// WorkTypeRegistry Tests
// ============================================================================

TEST_F(WorkConfigTest, WorkTypeRegistry_LoadValidXML) {
	std::string xml = R"(<?xml version="1.0"?>
<WorkTypes>
  <Category defName="Farming" tier="3">
    <label>Farming</label>
    <WorkType defName="Work_Harvest">
      <label>Harvest</label>
      <triggerCapability>Harvestable</triggerCapability>
    </WorkType>
  </Category>
</WorkTypes>)";

	std::string path = writeTempFile(xml, ".xml");
	bool		loaded = loadTemp(WorkTypeRegistry::Get(), path);

	EXPECT_TRUE(loaded);
	EXPECT_EQ(WorkTypeRegistry::Get().categoryCount(), 1);
	EXPECT_EQ(WorkTypeRegistry::Get().workTypeCount(), 1);
}

TEST_F(WorkConfigTest, WorkTypeRegistry_GetCategory) {
	std::string xml = R"(<?xml version="1.0"?>
<WorkTypes>
  <Category defName="Hauling" tier="6" canDisable="true">
    <label>Hauling</label>
    <WorkType defName="Work_Haul">
      <triggerCapability>Carryable</triggerCapability>
    </WorkType>
  </Category>
</WorkTypes>)";

	std::string path = writeTempFile(xml, ".xml");
	loadTemp(WorkTypeRegistry::Get(), path);

	const auto* cat = WorkTypeRegistry::Get().getCategory("Hauling");
	ASSERT_NE(cat, nullptr);
	EXPECT_EQ(cat->tier, 6.0F);
	EXPECT_TRUE(cat->canDisable);
	EXPECT_EQ(cat->workTypeDefNames.size(), 1);
}

TEST_F(WorkConfigTest, WorkTypeRegistry_GetWorkTypesForCapability) {
	std::string xml = R"(<?xml version="1.0"?>
<WorkTypes>
  <Category defName="Farming" tier="3">
    <WorkType defName="Work_HarvestCrops">
      <triggerCapability>Harvestable</triggerCapability>
    </WorkType>
    <WorkType defName="Work_HarvestWild">
      <triggerCapability>Harvestable</triggerCapability>
    </WorkType>
  </Category>
</WorkTypes>)";

	std::string path = writeTempFile(xml, ".xml");
	loadTemp(WorkTypeRegistry::Get(), path);

	auto workTypes = WorkTypeRegistry::Get().getWorkTypesForCapability("Harvestable");
	EXPECT_EQ(workTypes.size(), 2);
}

TEST_F(WorkConfigTest, WorkTypeRegistry_CategoriesSortedByTier) {
	std::string xml = R"(<?xml version="1.0"?>
<WorkTypes>
  <Category defName="Cleaning" tier="7">
    <WorkType defName="Work_Clean"><triggerCapability>Cleanable</triggerCapability></WorkType>
  </Category>
  <Category defName="Emergency" tier="1">
    <WorkType defName="Work_Rescue"><triggerCapability>Incapacitated</triggerCapability></WorkType>
  </Category>
  <Category defName="Hauling" tier="6">
    <WorkType defName="Work_Haul"><triggerCapability>Carryable</triggerCapability></WorkType>
  </Category>
</WorkTypes>)";

	std::string path = writeTempFile(xml, ".xml");
	loadTemp(WorkTypeRegistry::Get(), path);

	auto categories = WorkTypeRegistry::Get().getAllCategories();
	ASSERT_EQ(categories.size(), 3);
	EXPECT_EQ(categories[0]->defName, "Emergency");
	EXPECT_EQ(categories[1]->defName, "Hauling");
	EXPECT_EQ(categories[2]->defName, "Cleaning");
}

// ============================================================================
// PriorityConfig Tests
// ============================================================================

TEST_F(WorkConfigTest, PriorityConfig_LoadValidXML) {
	std::string xml = R"(<?xml version="1.0"?>
<arbitration>
  <ladder><rung name="survival"/><rung name="critical_need"/><rung name="idle"/></ladder>
  <Bonuses>
    <DistanceFactor><maxDistance>60</maxDistance><maxFactor>300</maxFactor></DistanceFactor>
    <InProgress>
      <bonus>40</bonus>
    </InProgress>
    <TaskAge><bonusPerMinute>1</bonusPerMinute><maxBonus>100</maxBonus></TaskAge>
    <StoragePriority><weight>300</weight></StoragePriority>
  </Bonuses>
</arbitration>)";

	std::string path = writeTempFile(xml, ".xml");
	bool		loaded = loadTemp(PriorityConfig::Get(), path);

	EXPECT_TRUE(loaded);
	EXPECT_EQ(PriorityConfig::Get().getInProgressBonus(), 40);
	EXPECT_EQ(PriorityConfig::Get().rungs(), (std::vector<std::string>{"survival", "critical_need", "idle"}));
}

// ============================================================================
// ConfigValidator Tests
// ============================================================================

TEST_F(WorkConfigTest, ConfigValidator_ValidConfig) {
	std::string workXml = R"(<?xml version="1.0"?>
<WorkTypes>
  <Category defName="Hauling" tier="6">
    <WorkType defName="Work_Haul">
      <triggerCapability>Carryable</triggerCapability>
    </WorkType>
  </Category>
</WorkTypes>)";
	std::string workPath = writeTempFile(workXml, "_work.xml");
	loadTemp(WorkTypeRegistry::Get(), workPath);

	// Validate
	EXPECT_TRUE(ConfigValidator::validateWorkTypes());
	EXPECT_EQ(ConfigValidator::getErrorCount(), 0);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(WorkConfigTest, Integration_FullConfigLoad) {
	// This tests loading all configs in dependency order

	// 1. Work types
	std::string workXml = R"(<?xml version="1.0"?>
<WorkTypes>
  <Category defName="Farming" tier="3">
    <WorkType defName="Work_Harvest">
      <triggerCapability>Harvestable</triggerCapability>
    </WorkType>
  </Category>
  <Category defName="Hauling" tier="6">
    <WorkType defName="Work_Haul">
      <triggerCapability>Carryable</triggerCapability>
    </WorkType>
  </Category>
</WorkTypes>)";
	std::string workPath = writeTempFile(workXml, "_work.xml");
	ASSERT_TRUE(loadTemp(WorkTypeRegistry::Get(), workPath));
	ASSERT_TRUE(ConfigValidator::validateWorkTypes());

	// 2. The arbitration file (deps: work types for category order)
	std::string priorityXml = R"(<?xml version="1.0"?>
<arbitration>
  <ladder><rung name="survival"/><rung name="idle"/></ladder>
  <WorkCategoryOrder>
    <Category name="Farming" tier="3"/>
    <Category name="Hauling" tier="6"/>
  </WorkCategoryOrder>
  <Bonuses>
    <DistanceFactor><maxDistance>60</maxDistance><maxFactor>300</maxFactor></DistanceFactor>
    <InProgress><bonus>50</bonus></InProgress>
    <TaskAge><bonusPerMinute>1</bonusPerMinute><maxBonus>100</maxBonus></TaskAge>
    <StoragePriority><weight>300</weight></StoragePriority>
  </Bonuses>
</arbitration>)";
	std::string priorityPath = writeTempFile(priorityXml, "_priority.xml");
	ASSERT_TRUE(loadTemp(PriorityConfig::Get(), priorityPath));
	ASSERT_TRUE(ConfigValidator::validatePriorityConfig());

	// Verify everything loaded correctly
	EXPECT_EQ(WorkTypeRegistry::Get().categoryCount(), 2);
	EXPECT_EQ(WorkTypeRegistry::Get().workTypeCount(), 2);
}

// ============================================================================
// loadWorkConfig
// ============================================================================

namespace {
	std::filesystem::path shippedConfigRoot() {
		// <root>/libs/assets/assets/WorkConfig.test.cpp -> <root>/assets/config
		return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "assets" / "config";
	}

	std::filesystem::path shippedHumanFolder() {
		return shippedConfigRoot().parent_path() / "world" / "creatures" / "Human";
	}

	/// The shipped human's files; a broken shipped folder stops the run, as the sim tests' shippedHumanConfig does.
	CreatureConfigFiles shippedHuman() {
		CreatureConfigFiles			   human = creatureConfigFiles(shippedHumanFolder());
		std::vector<std::string>	   errors = human.errors;
		const std::vector<std::string> missing = missingKinds(human);
		errors.insert(errors.end(), missing.begin(), missing.end());
		for (const std::string& error : errors) {
			std::fprintf(stderr, "shipped human config: %s\n", error.c_str());
		}
		if (!errors.empty()) {
			std::abort();
		}
		return human;
	}
} // namespace

TEST_F(WorkConfigTest, LoadWorkConfig_ShippedConfigLoadsClean) {
	for (const auto& error : loadWorkConfig(shippedConfigRoot(), shippedHuman())) {
		ADD_FAILURE() << error;
	}
	EXPECT_GT(WorkTypeRegistry::Get().workTypeCount(), 0);
	EXPECT_TRUE(ConstructionRegistry::Get().materialsLoaded());
}

namespace {
	std::string priorityTuningWith(const std::string& maxDistance, const std::string& tier = "5") {
		return "<arbitration><ladder><rung name=\"survival\"/></ladder><TaskTiers><Task name=\"Craft\" tier=\"" + tier +
			   "\"/></TaskTiers><Bonuses>" + "<DistanceFactor>" + maxDistance + "<maxFactor>300</maxFactor></DistanceFactor>" +
			   "<InProgress><bonus>50</bonus></InProgress>" +
			   "<TaskAge><bonusPerMinute>1</bonusPerMinute><maxBonus>100</maxBonus></TaskAge>" +
			   "<StoragePriority><weight>300</weight></StoragePriority></Bonuses></arbitration>";
	}

	std::string moodWith(const std::string& easing) {
		return "<Mood>" + easing +
			   "<NeedPenalty comfortable=\"70\" warning=\"30\" penaltyAtWarning=\"0.3\"/>"
			   "<Bands><Band name=\"Content\" from=\"0\"/></Bands></Mood>";
	}
} // namespace

TEST_F(WorkConfigTest, PriorityConfig_ShippedValuesArePinned) {
	ASSERT_TRUE(PriorityConfig::Get().loadFromFile(shippedConfigRoot(), "arbitration/priority-tuning.xml"));
	const PriorityConfig& config = PriorityConfig::Get();
	EXPECT_TRUE(config.problems().empty());
	EXPECT_EQ(
		config.rungs(),
		(std::vector<std::string>{
			"survival",
			"critical_need",
			"low_mood",
			"player_order",
			"urgent_need",
			"own_unfinished_work",
			"committed_work",
			"ordinary_need",
			"opportunistic_work",
			"idle"
		})
	);
	EXPECT_FLOAT_EQ(config.getDistanceFactorConfig().maxDistance, 60.0F);
	EXPECT_FLOAT_EQ(config.getDistanceFactorConfig().maxFactor, 300.0F);
	EXPECT_EQ(config.getInProgressBonus(), 50);
	EXPECT_EQ(config.getStoragePriorityWeight(), 300);
	EXPECT_EQ(config.getTaskAgeConfig().bonusPerMinute, 1);
	EXPECT_EQ(config.getTaskAgeConfig().maxBonus, 100);
	EXPECT_EQ(config.getTaskTier("FulfillNeed"), 5);
	EXPECT_EQ(config.getTaskTier("Craft"), 4);
	EXPECT_EQ(config.getTaskTier("Haul"), 6);
	EXPECT_EQ(config.getTaskTier("Wander"), 7);
	EXPECT_EQ(config.getCategoryOrder().size(), 7U);
	EXPECT_EQ(config.getCategoryOrder().front(), "Emergency");
}

TEST_F(WorkConfigTest, MoodConfig_ShippedValuesArePinned) {
	MoodConfig config;
	ASSERT_TRUE(config.loadFromFile(shippedHumanFolder(), "mood.xml"));
	EXPECT_TRUE(config.problems().empty());
	EXPECT_FLOAT_EQ(config.pointsPerGameHour(), 5.0F);
	EXPECT_FLOAT_EQ(config.needPenaltyCurve().comfortable, 70.0F);
	EXPECT_FLOAT_EQ(config.needPenaltyCurve().warning, 30.0F);
	EXPECT_FLOAT_EQ(config.needPenaltyCurve().penaltyAtWarning, 0.3F);
	ASSERT_EQ(config.bands().size(), 4U);
	EXPECT_FLOAT_EQ(config.bands().front().from, 55.0F);
}

TEST_F(WorkConfigTest, PriorityConfig_BadNumbersAreErrorsNamingFileElementAndAttribute) {
	const auto errorsFor = [this](const std::string& xml) {
		const std::string path = writeTempFile(xml, ".xml");
		PriorityConfig::Get().clear();
		EXPECT_FALSE(loadTemp(PriorityConfig::Get(), path));
		std::vector<std::string> missingTiers;
		EXPECT_FALSE(PriorityConfig::Get().validateTaskTiers({"Craft"}, missingTiers)) << "a bad file leaves no tier behind";
		ConfigValidator::clearErrors();
		EXPECT_FALSE(ConfigValidator::validatePriorityConfig()) << xml;
		std::string joined;
		for (const ValidationError& error : ConfigValidator::getErrors()) {
			joined += error.message + "\n";
		}
		EXPECT_NE(joined.find(fileNameOf(path) + ": "), std::string::npos) << joined;
		return joined;
	};

	EXPECT_NE(errorsFor(priorityTuningWith("")).find("<DistanceFactor> maxDistance is missing"), std::string::npos);
	for (const char* text : {"30abc", "abc", "", "nan"}) {
		const std::string joined = errorsFor(priorityTuningWith(std::string("<maxDistance>") + text + "</maxDistance>"));
		EXPECT_NE(joined.find(std::string("<DistanceFactor> maxDistance must be a number, got \"") + text + "\""), std::string::npos)
			<< text << ": " << joined;
	}
	for (const char* text : {"5x", "x", "", "2.5", "99999999999"}) {
		const std::string joined = errorsFor(priorityTuningWith("<maxDistance>60</maxDistance>", text));
		EXPECT_NE(joined.find(std::string("<Task name=\"Craft\"> tier must be a whole number, got \"") + text + "\""), std::string::npos)
			<< text << ": " << joined;
	}
}

TEST_F(WorkConfigTest, PriorityConfig_ElementTextIsTrimmedAndALeadingPlusIsRead) {
	const std::string path = writeTempFile(
		"<arbitration><ladder><rung name=\"survival\"/></ladder><Bonuses><DistanceFactor><maxDistance>\n  60\n</maxDistance>"
		"<maxFactor>+300</maxFactor></DistanceFactor>"
		"<InProgress><bonus>-32768</bonus></InProgress><TaskAge><bonusPerMinute> 1 </bonusPerMinute><maxBonus>32767</maxBonus></TaskAge>"
		"<StoragePriority><weight>32767</weight></StoragePriority></Bonuses></arbitration>",
		".xml"
	);
	ASSERT_TRUE(loadTemp(PriorityConfig::Get(), path));
	const PriorityConfig& config = PriorityConfig::Get();
	EXPECT_FLOAT_EQ(config.getDistanceFactorConfig().maxDistance, 60.0F);
	EXPECT_FLOAT_EQ(config.getDistanceFactorConfig().maxFactor, 300.0F);
	EXPECT_EQ(config.getTaskAgeConfig().bonusPerMinute, 1);
	EXPECT_EQ(config.getStoragePriorityWeight(), 32767);
	EXPECT_TRUE(config.problems().empty());
}

TEST_F(WorkConfigTest, PriorityConfig_NumbersInSixteenBitFieldsAreRangeChecked) {
	const std::string path = writeTempFile(
		"<arbitration><ladder><rung name=\"survival\"/></ladder><Bonuses><DistanceFactor><maxDistance>\n  60\n</maxDistance>"
		"<maxFactor>+300</maxFactor></DistanceFactor>"
		"<InProgress><bonus>40000</bonus></InProgress><TaskAge><bonusPerMinute> 1 </bonusPerMinute><maxBonus>-32769</maxBonus></TaskAge>"
		"<StoragePriority><weight>32767</weight></StoragePriority></Bonuses></arbitration>",
		".xml"
	);
	EXPECT_FALSE(loadTemp(PriorityConfig::Get(), path));
	const PriorityConfig& config = PriorityConfig::Get();
	ASSERT_EQ(config.problems().size(), 2U);
	EXPECT_NE(config.problems()[0].find("<InProgress> bonus must be a whole number from -32768 to 32767, got \"40000\""), std::string::npos)
		<< config.problems()[0];
	EXPECT_NE(
		config.problems()[1].find("<TaskAge> maxBonus must be a whole number from -32768 to 32767, got \"-32769\""), std::string::npos
	) << config.problems()[1];
}

TEST_F(WorkConfigTest, MoodConfig_BadNumbersAreErrorsNamingFileElementAndAttribute) {
	const auto errorsFor = [this](const std::string& xml) {
		const std::string path = writeTempFile(xml, ".xml");
		MoodConfig		  config;
		EXPECT_FALSE(loadTemp(config, path));
		ConfigValidator::clearErrors();
		EXPECT_FALSE(ConfigValidator::validateMood(config)) << xml;
		std::string joined;
		for (const ValidationError& error : ConfigValidator::getErrors()) {
			joined += error.message + "\n";
		}
		EXPECT_NE(joined.find(fileNameOf(path) + ": "), std::string::npos) << joined;
		return joined;
	};

	EXPECT_NE(errorsFor(moodWith("")).find("<Easing> pointsPerGameHour is missing"), std::string::npos);
	EXPECT_NE(errorsFor(moodWith("<Easing/>")).find("<Easing> pointsPerGameHour is missing"), std::string::npos);
	for (const char* text : {"30abc", "abc", "", "nan"}) {
		const std::string joined = errorsFor(moodWith(std::string("<Easing pointsPerGameHour=\"") + text + "\"/>"));
		EXPECT_NE(joined.find(std::string("<Easing> pointsPerGameHour must be a number, got \"") + text + "\""), std::string::npos)
			<< text << ": " << joined;
	}
}

TEST_F(WorkConfigTest, LoadWorkConfig_MissingFolderReportsError) {
	EXPECT_FALSE(loadWorkConfig(std::filesystem::temp_directory_path() / "no-such-work-config", shippedHuman()).empty());
}

TEST_F(WorkConfigTest, ConfigValidator_MemoryWindowMustBeAPositiveNumber) {
	for (const char* window : {"30", "0.5", "+30"}) {
		MemoryConfig config;
		ASSERT_TRUE(
			loadTemp(config, writeTempFile(std::string("<Memory><RecentSighting gameMinutes=\"") + window + "\"/></Memory>", ".xml"))
		);
		ConfigValidator::clearErrors();
		EXPECT_TRUE(ConfigValidator::validateMemory(config)) << window;
	}

	for (const char* window : {"0", "-5"}) {
		MemoryConfig config;
		ASSERT_TRUE(
			loadTemp(config, writeTempFile(std::string("<Memory><RecentSighting gameMinutes=\"") + window + "\"/></Memory>", ".xml"))
		);
		ConfigValidator::clearErrors();
		EXPECT_FALSE(ConfigValidator::validateMemory(config)) << window;
		EXPECT_EQ(ConfigValidator::getErrorCount(), 1);
	}

	const auto problemFor = [this](const std::string& sighting) {
		const std::string path = writeTempFile("<Memory>" + sighting + "</Memory>", ".xml");
		MemoryConfig	  config;
		EXPECT_FALSE(loadTemp(config, path)) << sighting;
		EXPECT_FALSE(config.loaded());
		return config.problems().size() == 1 ? config.problems().front().substr(fileNameOf(path).size()) : std::string("several problems");
	};
	EXPECT_EQ(problemFor(""), ": <RecentSighting> gameMinutes is missing");
	EXPECT_EQ(problemFor("<RecentSighting/>"), ": <RecentSighting> gameMinutes is missing");
	for (const char* window : {"abc", "30abc", "", " 30", "nan", "inf"}) {
		EXPECT_EQ(
			problemFor(std::string("<RecentSighting gameMinutes=\"") + window + "\"/>"),
			std::string(": <RecentSighting> gameMinutes must be a number, got \"") + window + "\""
		);
	}
}

TEST_F(WorkConfigTest, LoadWorkConfig_ShippedValuesArePinned) {
	ASSERT_TRUE(loadWorkConfig(shippedConfigRoot(), shippedHuman()).empty());

	EXPECT_TRUE(WorkTypeRegistry::Get().getWorkType("Work_HaulToStorage")->filter.looseItem.value_or(false));
	EXPECT_FALSE(WorkTypeRegistry::Get().getWorkType("Work_HaulToStorage")->filter.indoor.has_value())
		<< "a filter element left out is no condition";

	const std::map<std::string, float> tiers = {
		{"Emergency", 1.0F},
		{"Medical", 2.0F},
		{"Farming", 3.0F},
		{"Crafting", 4.0F},
		{"Construction", 5.0F},
		{"Hauling", 6.0F},
		{"Cleaning", 7.0F}
	};
	ASSERT_EQ(WorkTypeRegistry::Get().categoryCount(), tiers.size());
	for (const auto& [name, tier] : tiers) {
		const WorkCategoryDef* category = WorkTypeRegistry::Get().getCategory(name);
		ASSERT_NE(category, nullptr) << name;
		EXPECT_EQ(category->tier, tier) << name;
	}
	EXPECT_FALSE(WorkTypeRegistry::Get().getCategory("Emergency")->canDisable);
	EXPECT_TRUE(WorkTypeRegistry::Get().getCategory("Hauling")->canDisable) << "canDisable left out means it can be disabled";
	EXPECT_EQ(WorkTypeRegistry::Get().getWorkType("Work_Doctoring")->minSkillLevel, 1.0F);
	EXPECT_EQ(WorkTypeRegistry::Get().getWorkType("Work_HarvestCrops")->minSkillLevel, 0.0F) << "minSkillLevel left out means no minimum";
	EXPECT_EQ(MemoryConfig::Get().recentSightingGameMinutes(), 30.0F);
}

TEST_F(WorkConfigTest, PriorityConfig_LadderMistakesNameTheFileAndLoadNothing) {
	const auto problemsFor = [this](const std::string& ladder, const std::string& extra = "") {
		const std::string bonuses = "<Bonuses><DistanceFactor><maxDistance>60</maxDistance><maxFactor>300</maxFactor></DistanceFactor>"
									"<InProgress><bonus>50</bonus></InProgress><TaskAge><bonusPerMinute>1</bonusPerMinute>"
									"<maxBonus>100</maxBonus></TaskAge><StoragePriority><weight>300</weight></StoragePriority></Bonuses>";
		const std::string path = writeTempFile("<arbitration>" + ladder + extra + bonuses + "</arbitration>", ".xml");
		EXPECT_FALSE(loadTemp(PriorityConfig::Get(), path));
		EXPECT_TRUE(PriorityConfig::Get().rungs().empty()) << "a file with any mistake loads no rung";
		std::vector<std::string> problems = PriorityConfig::Get().problems();
		for (std::string& problem : problems) {
			EXPECT_EQ(problem.rfind(fileNameOf(path) + ": ", 0), 0U) << problem;
			problem.erase(0, fileNameOf(path).size() + 2);
		}
		return problems;
	};

	EXPECT_EQ(problemsFor(""), std::vector<std::string>{"<arbitration> has 0 <ladder> elements; the ladder is declared once"});
	EXPECT_EQ(
		problemsFor("<ladder><rung name=\"idle\"/></ladder><ladder><rung name=\"idle\"/></ladder>"),
		std::vector<std::string>{"<arbitration> has 2 <ladder> elements; the ladder is declared once"}
	);
	EXPECT_EQ(problemsFor("<ladder/>"), std::vector<std::string>{"<ladder> has no <rung>"});
	EXPECT_EQ(
		problemsFor("<ladder><rung name=\"survival\"/><rung name=\"idle\"/><rung name=\"idle\"/></ladder>"),
		std::vector<std::string>{"<ladder> rung 3 name \"idle\" is given twice"}
	);
	EXPECT_EQ(
		problemsFor("<ladder><rung name=\"survival\"/><rung/></ladder>"), std::vector<std::string>{"<ladder> rung 2 name is missing"}
	);
	EXPECT_EQ(
		problemsFor("<ladder><rung name=\"survival\"/><rung name=\"idle\" tier=\"9\"/></ladder>"),
		std::vector<std::string>{"<ladder> rung 2: unknown attribute 'tier'"}
	);
	EXPECT_EQ(
		problemsFor("<ladder><step name=\"idle\"/></ladder>"),
		(std::vector<std::string>{"<ladder>: unknown element <step>", "<ladder> has no <rung>"})
	);
	EXPECT_EQ(
		problemsFor("<ladder><rung name=\"survival\"/></ladder>", "<Chains/>"),
		std::vector<std::string>{"<arbitration>: unknown element <Chains>"}
	);
	EXPECT_EQ(
		problemsFor("<ladder><rung name=\"idle\"/></ladder>"),
		std::vector<std::string>{"<ladder> has no rung named \"survival\", which is reserved for threats"}
	);
	EXPECT_EQ(problemsFor("<ladder><rung/></ladder>"), std::vector<std::string>{"<ladder> rung 1 name is missing"})
		<< "a nameless rung is the one error, not also the missing survival rung";
	std::string manyRungs = "<ladder><rung name=\"survival\"/>";
	for (int rung = 1; rung < 257; ++rung) {
		manyRungs += "<rung name=\"rung_" + std::to_string(rung) + "\"/>";
	}
	EXPECT_EQ(problemsFor(manyRungs + "</ladder>"), std::vector<std::string>{"<ladder> has 257 rungs; it holds at most 256"});
}

TEST_F(WorkConfigTest, WorkTypeRegistry_MistakesNameTheInstanceAndLoadNothing) {
	const auto problemsFor = [this](const std::string& categoryAttributes, const std::string& workTypeBody) {
		const std::string path = writeTempFile(
			"<WorkTypes><Category defName=\"Emergency\" tier=\"1\"><WorkType "
			"defName=\"Work_Rescue\"><triggerCapability>Incapacitated</triggerCapability></WorkType></Category>"
			"<Category defName=\"Medical\"" +
				categoryAttributes + "><WorkType defName=\"Work_Doctoring\">" + workTypeBody + "</WorkType></Category></WorkTypes>",
			".xml"
		);
		EXPECT_FALSE(loadTemp(WorkTypeRegistry::Get(), path));
		EXPECT_EQ(WorkTypeRegistry::Get().categoryCount(), 0U) << "a file with any mistake loads no category";
		EXPECT_EQ(WorkTypeRegistry::Get().workTypeCount(), 0U);
		std::vector<std::string> problems = WorkTypeRegistry::Get().problems();
		for (std::string& problem : problems) {
			EXPECT_EQ(problem.rfind(fileNameOf(path) + ": ", 0), 0U) << problem;
			problem.erase(0, fileNameOf(path).size() + 2);
		}
		return problems;
	};

	EXPECT_EQ(problemsFor("", ""), std::vector<std::string>{"<Category defName=\"Medical\"> tier is missing"});
	EXPECT_EQ(
		problemsFor(" tier=\"second\"", ""),
		std::vector<std::string>{"<Category defName=\"Medical\"> tier must be a number, got \"second\""}
	);
	EXPECT_EQ(
		problemsFor(" tier=\"2\"", "<skillRequired>Medicine</skillRequired><minSkillLevel>high</minSkillLevel>"),
		std::vector<std::string>{"<WorkType defName=\"Work_Doctoring\"> minSkillLevel must be a number, got \"high\""}
	);
	EXPECT_EQ(
		problemsFor(" tier=\"2\" order=\"1\"", ""), std::vector<std::string>{"<Category defName=\"Medical\">: unknown attribute 'order'"}
	);
	EXPECT_EQ(
		problemsFor(" tier=\"2\"", "<skill>Medicine</skill>"),
		std::vector<std::string>{"<WorkType defName=\"Work_Doctoring\">: unknown element <skill>"}
	);
	EXPECT_EQ(
		problemsFor(" tier=\"2\"", "<taskChain>Chain_Rescue</taskChain>"),
		std::vector<std::string>{"<WorkType defName=\"Work_Doctoring\">: unknown element <taskChain>"}
	) << "task-chains.xml is gone, so a leftover chain name is no longer read";
	EXPECT_EQ(
		problemsFor(" tier=\"-1\"", ""),
		std::vector<std::string>{"<Category defName=\"Medical\"> tier must be a number, 0 or more, got \"-1\""}
	);
	EXPECT_EQ(
		problemsFor(" tier=\"2\"", "<skillRequired>Medicine</skillRequired><minSkillLevel>-2</minSkillLevel>"),
		std::vector<std::string>{"<WorkType defName=\"Work_Doctoring\"> minSkillLevel must be a number, 0 or more, got \"-2\""}
	);
	EXPECT_EQ(
		problemsFor(" tier=\"2\" canDisable=\"no\"", ""),
		std::vector<std::string>{"<Category defName=\"Medical\"> canDisable must be true or false, got \"no\""}
	);
	EXPECT_EQ(
		problemsFor(" tier=\"2\"", "<filter><looseItem>ture</looseItem></filter>"),
		std::vector<std::string>{"<WorkType defName=\"Work_Doctoring\"> filter looseItem must be true or false, got \"ture\""}
	);
	EXPECT_EQ(
		problemsFor(" tier=\"2\"", "<filter><indoor>1</indoor></filter>"),
		std::vector<std::string>{"<WorkType defName=\"Work_Doctoring\"> filter indoor must be true or false, got \"1\""}
	);
}

TEST_F(WorkConfigTest, PriorityConfig_TaskAgeBonusIsCappedAtAnyAge) {
	ASSERT_TRUE(loadTemp(PriorityConfig::Get(), writeTempFile(priorityTuningWith("<maxDistance>60</maxDistance>"), ".xml")));
	const PriorityConfig& config = PriorityConfig::Get();
	EXPECT_EQ(config.calculateTaskAgeBonus(0.0F), 0);
	EXPECT_EQ(config.calculateTaskAgeBonus(30.0F * 60.0F), 30);
	EXPECT_EQ(config.calculateTaskAgeBonus(100.0F * 60.0F), 100);
	EXPECT_EQ(config.calculateTaskAgeBonus(40000.0F * 60.0F), 100) << "past the int16 range of minutes";
	EXPECT_EQ(config.calculateTaskAgeBonus(1.0e30F), 100);
}

TEST_F(WorkConfigTest, LoadWorkConfig_ErrorsNameFilesRelativeToTheConfigRoot) {
	const std::filesystem::path root = writeTempFile("", "_root");
	std::filesystem::remove(root);
	std::filesystem::copy(shippedConfigRoot(), root, std::filesystem::copy_options::recursive);
	const auto replaceIn = [&root](const std::filesystem::path& file, const std::string& from, const std::string& to) {
		std::ifstream in(root / file);
		std::string	  text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		in.close();
		ASSERT_NE(text.find(from), std::string::npos) << from;
		text.replace(text.find(from), from.size(), to);
		std::ofstream(root / file) << text;
	};
	replaceIn("arbitration/priority-tuning.xml", "<bonus>50</bonus>", "<bonus>fifty</bonus>");
	EXPECT_EQ(
		loadWorkConfig(root, shippedHuman()),
		std::vector<std::string>{
			"[arbitration] priority-tuning.xml: <InProgress> bonus must be a whole number from -32768 to 32767, got \"fifty\""
		}
	);
	EXPECT_TRUE(PriorityConfig::Get().rungs().empty());

	std::ofstream(root / "arbitration" / "more-rungs.xml") << "<arbitration/>";
	EXPECT_EQ(
		loadWorkConfig(root, shippedHuman()),
		std::vector<std::string>{
			"[arbitration] arbitration: 2 files; one file declares the ladder and the bonuses (more-rungs.xml, priority-tuning.xml)"
		}
	);

	replaceIn(
		"legacy/work-types.xml",
		"<Category defName=\"Emergency\" tier=\"1\" canDisable=\"false\">",
		"<Category defName=\"Emergency\" tier=\"1\" canDisable=\"ture\">"
	);
	EXPECT_EQ(
		loadWorkConfig(root, shippedHuman()),
		std::vector<std::string>{"legacy/work-types.xml: <Category defName=\"Emergency\"> canDisable must be true or false, got \"ture\""}
	);
	EXPECT_EQ(WorkTypeRegistry::Get().categoryCount(), 0U);
	std::filesystem::remove_all(root);
}

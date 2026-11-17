// Unit tests for Work Configuration System
// Tests ActionTypeRegistry, TaskChainRegistry, WorkTypeRegistry,
// PriorityConfig, ConfigValidator, and loadWorkConfig.

#include "assets/ActionTypeRegistry.h"
#include "assets/ConfigValidator.h"
#include "assets/ConstructionRegistry.h"
#include "assets/MemoryConfig.h"
#include "assets/MoodConfig.h"
#include "assets/PriorityConfig.h"
#include "assets/TaskChainRegistry.h"
#include "assets/WorkConfig.h"
#include "assets/WorkTypeRegistry.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
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
        ActionTypeRegistry::Get().clear();
        TaskChainRegistry::Get().clear();
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
        std::filesystem::path	path = std::filesystem::temp_directory_path() /
                                    ("workconfig_" + token + "_" + std::to_string(counter++) + suffix);

        std::ofstream file(path);
        file << content;
        file.close();

        tempFiles.push_back(path.string());
        return path.string();
    }

    /// Loads the temp file at `path` into `config`, naming it by its file name.
    template <typename Config> static bool loadTemp(Config& config, const std::string& path) {
        const std::filesystem::path file(path);
        return config.loadFromFile(file.parent_path(), file.filename());
    }

    static std::string fileNameOf(const std::string& path) { return std::filesystem::path(path).filename().string(); }

    std::vector<std::string> tempFiles;
};

// ============================================================================
// ActionTypeRegistry Tests
// ============================================================================

TEST_F(WorkConfigTest, ActionTypeRegistry_LoadValidXML) {
    std::string xml = R"(<?xml version="1.0"?>
<ActionTypes>
  <Action defName="Eat" needsHands="true">
    <description>Consuming food</description>
  </Action>
  <Action defName="Sleep" needsHands="false">
    <description>Resting</description>
  </Action>
</ActionTypes>)";

    std::string path = writeTempFile(xml, ".xml");
    ASSERT_FALSE(path.empty());

    bool loaded = ActionTypeRegistry::Get().loadFromFile(path);
    EXPECT_TRUE(loaded);
    EXPECT_EQ(ActionTypeRegistry::Get().size(), 2);
}

TEST_F(WorkConfigTest, ActionTypeRegistry_GetAction) {
    std::string xml = R"(<?xml version="1.0"?>
<ActionTypes>
  <Action defName="Pickup" needsHands="true"/>
</ActionTypes>)";

    std::string path = writeTempFile(xml, ".xml");
    ActionTypeRegistry::Get().loadFromFile(path);

    const auto* action = ActionTypeRegistry::Get().getAction("Pickup");
    ASSERT_NE(action, nullptr);
    EXPECT_EQ(action->defName, "Pickup");
    EXPECT_TRUE(action->needsHands);
}

TEST_F(WorkConfigTest, ActionTypeRegistry_ActionNeedsHands) {
    std::string xml = R"(<?xml version="1.0"?>
<ActionTypes>
  <Action defName="Eat" needsHands="true"/>
  <Action defName="Sleep" needsHands="false"/>
</ActionTypes>)";

    std::string path = writeTempFile(xml, ".xml");
    ActionTypeRegistry::Get().loadFromFile(path);

    EXPECT_TRUE(ActionTypeRegistry::Get().actionNeedsHands("Eat"));
    EXPECT_FALSE(ActionTypeRegistry::Get().actionNeedsHands("Sleep"));
    EXPECT_FALSE(ActionTypeRegistry::Get().actionNeedsHands("NonExistent"));
}

TEST_F(WorkConfigTest, ActionTypeRegistry_HasAction) {
    std::string xml = R"(<?xml version="1.0"?>
<ActionTypes>
  <Action defName="Craft" needsHands="true"/>
</ActionTypes>)";

    std::string path = writeTempFile(xml, ".xml");
    ActionTypeRegistry::Get().loadFromFile(path);

    EXPECT_TRUE(ActionTypeRegistry::Get().hasAction("Craft"));
    EXPECT_FALSE(ActionTypeRegistry::Get().hasAction("NonExistent"));
}

// ============================================================================
// TaskChainRegistry Tests
// ============================================================================

TEST_F(WorkConfigTest, TaskChainRegistry_LoadValidXML) {
    std::string xml = R"(<?xml version="1.0"?>
<TaskChains>
  <Chain defName="Chain_Haul">
    <label>Haul Item</label>
    <steps>
      <Step order="0" action="Pickup" target="source"/>
      <Step order="1" action="Deposit" target="destination"/>
    </steps>
  </Chain>
</TaskChains>)";

    std::string path = writeTempFile(xml, ".xml");
    bool loaded = loadTemp(TaskChainRegistry::Get(), path);

    EXPECT_TRUE(loaded);
    EXPECT_EQ(TaskChainRegistry::Get().size(), 1);
}

TEST_F(WorkConfigTest, TaskChainRegistry_GetChain) {
    std::string xml = R"(<?xml version="1.0"?>
<TaskChains>
  <Chain defName="Chain_Test">
    <label>Test Chain</label>
    <steps>
      <Step order="0" action="Action1" target="t1"/>
      <Step order="1" action="Action2" target="t2" optional="true"/>
    </steps>
  </Chain>
</TaskChains>)";

    std::string path = writeTempFile(xml, ".xml");
    loadTemp(TaskChainRegistry::Get(), path);

    const auto* chain = TaskChainRegistry::Get().getChain("Chain_Test");
    ASSERT_NE(chain, nullptr);
    EXPECT_EQ(chain->defName, "Chain_Test");
    EXPECT_EQ(chain->label, "Test Chain");
    EXPECT_EQ(chain->stepCount(), 2);
}

TEST_F(WorkConfigTest, TaskChainRegistry_ChainSteps) {
    std::string xml = R"(<?xml version="1.0"?>
<TaskChains>
  <Chain defName="Chain_Multi">
    <steps>
      <Step order="0" action="A" target="t1"/>
      <Step order="1" action="B" target="t2" requiresPreviousStep="true"/>
      <Step order="2" action="C" target="t3" optional="true"/>
    </steps>
  </Chain>
</TaskChains>)";

    std::string path = writeTempFile(xml, ".xml");
    loadTemp(TaskChainRegistry::Get(), path);

    const auto* chain = TaskChainRegistry::Get().getChain("Chain_Multi");
    ASSERT_NE(chain, nullptr);

    const auto* step0 = chain->getStep(0);
    ASSERT_NE(step0, nullptr);
    EXPECT_EQ(step0->actionDefName, "A");

    const auto* step1 = chain->getStep(1);
    ASSERT_NE(step1, nullptr);
    EXPECT_TRUE(step1->requiresPreviousStep);

    const auto* step2 = chain->getStep(2);
    ASSERT_NE(step2, nullptr);
    EXPECT_TRUE(step2->optional);

    const auto* nextStep = chain->getNextStep(0);
    ASSERT_NE(nextStep, nullptr);
    EXPECT_EQ(nextStep->actionDefName, "B");
}

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
    bool loaded = loadTemp(WorkTypeRegistry::Get(), path);

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
<PriorityTuning>
  <Bonuses>
    <DistanceFactor><maxDistance>60</maxDistance><maxFactor>300</maxFactor></DistanceFactor>
    <InProgress>
      <bonus>40</bonus>
    </InProgress>
    <TaskAge><bonusPerMinute>1</bonusPerMinute><maxBonus>100</maxBonus></TaskAge>
    <StoragePriority><weight>300</weight></StoragePriority>
  </Bonuses>
</PriorityTuning>)";

    std::string path = writeTempFile(xml, ".xml");
    bool loaded = loadTemp(PriorityConfig::Get(), path);

    EXPECT_TRUE(loaded);
    EXPECT_EQ(PriorityConfig::Get().getInProgressBonus(), 40);
}

// ============================================================================
// ConfigValidator Tests
// ============================================================================

TEST_F(WorkConfigTest, ConfigValidator_ValidConfig) {
    // Load valid action types
    std::string actionXml = R"(<?xml version="1.0"?>
<ActionTypes>
  <Action defName="Pickup" needsHands="true"/>
  <Action defName="Deposit" needsHands="true"/>
</ActionTypes>)";
    std::string actionPath = writeTempFile(actionXml, "_actions.xml");
    ActionTypeRegistry::Get().loadFromFile(actionPath);

    // Load valid chains that reference valid actions
    std::string chainXml = R"(<?xml version="1.0"?>
<TaskChains>
  <Chain defName="Chain_Haul">
    <steps>
      <Step order="0" action="Pickup" target="source"/>
      <Step order="1" action="Deposit" target="dest"/>
    </steps>
  </Chain>
</TaskChains>)";
    std::string chainPath = writeTempFile(chainXml, "_chains.xml");
    loadTemp(TaskChainRegistry::Get(), chainPath);

    // Load valid work types that reference valid chains
    std::string workXml = R"(<?xml version="1.0"?>
<WorkTypes>
  <Category defName="Hauling" tier="6">
    <WorkType defName="Work_Haul">
      <triggerCapability>Carryable</triggerCapability>
      <taskChain>Chain_Haul</taskChain>
    </WorkType>
  </Category>
</WorkTypes>)";
    std::string workPath = writeTempFile(workXml, "_work.xml");
    loadTemp(WorkTypeRegistry::Get(), workPath);

    // Validate
    EXPECT_TRUE(ConfigValidator::validateActionTypes());
    EXPECT_TRUE(ConfigValidator::validateTaskChains());
    EXPECT_TRUE(ConfigValidator::validateWorkTypes());
    EXPECT_EQ(ConfigValidator::getErrorCount(), 0);
}

TEST_F(WorkConfigTest, ConfigValidator_InvalidChainActionReference) {
    // Load action types
    std::string actionXml = R"(<?xml version="1.0"?>
<ActionTypes>
  <Action defName="Pickup" needsHands="true"/>
</ActionTypes>)";
    std::string actionPath = writeTempFile(actionXml, "_actions.xml");
    ActionTypeRegistry::Get().loadFromFile(actionPath);

    // Load chain with invalid action reference
    std::string chainXml = R"(<?xml version="1.0"?>
<TaskChains>
  <Chain defName="Chain_Bad">
    <steps>
      <Step order="0" action="Pickup" target="source"/>
      <Step order="1" action="NonExistentAction" target="dest"/>
    </steps>
  </Chain>
</TaskChains>)";
    std::string chainPath = writeTempFile(chainXml, "_chains.xml");
    loadTemp(TaskChainRegistry::Get(), chainPath);

    // Validate should fail
    EXPECT_FALSE(ConfigValidator::validateTaskChains());
    EXPECT_GT(ConfigValidator::getErrorCount(), 0);
}

TEST_F(WorkConfigTest, ConfigValidator_InvalidWorkTypeChainReference) {
    // Load action types
    std::string actionXml = R"(<?xml version="1.0"?>
<ActionTypes>
  <Action defName="Test" needsHands="true"/>
</ActionTypes>)";
    std::string actionPath = writeTempFile(actionXml, "_actions.xml");
    ActionTypeRegistry::Get().loadFromFile(actionPath);

    // Load valid chain
    std::string chainXml = R"(<?xml version="1.0"?>
<TaskChains>
  <Chain defName="Chain_Valid">
    <steps>
      <Step order="0" action="Test" target="t"/>
    </steps>
  </Chain>
</TaskChains>)";
    std::string chainPath = writeTempFile(chainXml, "_chains.xml");
    loadTemp(TaskChainRegistry::Get(), chainPath);

    // Load work type with invalid chain reference
    std::string workXml = R"(<?xml version="1.0"?>
<WorkTypes>
  <Category defName="Test" tier="5">
    <WorkType defName="Work_Bad">
      <triggerCapability>Test</triggerCapability>
      <taskChain>Chain_NonExistent</taskChain>
    </WorkType>
  </Category>
</WorkTypes>)";
    std::string workPath = writeTempFile(workXml, "_work.xml");
    loadTemp(WorkTypeRegistry::Get(), workPath);

    // Validate should fail
    EXPECT_FALSE(ConfigValidator::validateWorkTypes());
    EXPECT_GT(ConfigValidator::getErrorCount(), 0);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(WorkConfigTest, Integration_FullConfigLoad) {
    // This tests loading all configs in dependency order

    // 1. Action types (no deps)
    std::string actionXml = R"(<?xml version="1.0"?>
<ActionTypes>
  <Action defName="Eat" needsHands="true"/>
  <Action defName="Pickup" needsHands="true"/>
  <Action defName="Deposit" needsHands="true"/>
  <Action defName="Harvest" needsHands="true"/>
</ActionTypes>)";
    std::string actionPath = writeTempFile(actionXml, "_actions.xml");
    ASSERT_TRUE(ActionTypeRegistry::Get().loadFromFile(actionPath));

    // 2. Task chains (deps: actions)
    std::string chainXml = R"(<?xml version="1.0"?>
<TaskChains>
  <Chain defName="Chain_PickupDeposit">
    <steps>
      <Step order="0" action="Pickup" target="source"/>
      <Step order="1" action="Deposit" target="dest"/>
    </steps>
  </Chain>
</TaskChains>)";
    std::string chainPath = writeTempFile(chainXml, "_chains.xml");
    ASSERT_TRUE(loadTemp(TaskChainRegistry::Get(), chainPath));
    ASSERT_TRUE(ConfigValidator::validateTaskChains());

    // 3. Work types (deps: chains)
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
      <taskChain>Chain_PickupDeposit</taskChain>
    </WorkType>
  </Category>
</WorkTypes>)";
    std::string workPath = writeTempFile(workXml, "_work.xml");
    ASSERT_TRUE(loadTemp(WorkTypeRegistry::Get(), workPath));
    ASSERT_TRUE(ConfigValidator::validateWorkTypes());

    // 4. Priority config (deps: work types for category order)
    std::string priorityXml = R"(<?xml version="1.0"?>
<PriorityTuning>
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
</PriorityTuning>)";
    std::string priorityPath = writeTempFile(priorityXml, "_priority.xml");
    ASSERT_TRUE(loadTemp(PriorityConfig::Get(), priorityPath));
    ASSERT_TRUE(ConfigValidator::validatePriorityConfig());

    // Verify everything loaded correctly
    EXPECT_EQ(ActionTypeRegistry::Get().size(), 4);
    EXPECT_EQ(TaskChainRegistry::Get().size(), 1);
    EXPECT_EQ(WorkTypeRegistry::Get().categoryCount(), 2);
    EXPECT_EQ(WorkTypeRegistry::Get().workTypeCount(), 2);
}

// ============================================================================
// loadWorkConfig
// ============================================================================

namespace {
    std::filesystem::path shippedConfigRoot() {
        // <root>/libs/assets/assets/WorkConfig.test.cpp -> <root>/assets/config
        return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "assets" /
               "config";
    }
} // namespace

TEST_F(WorkConfigTest, LoadWorkConfig_ShippedConfigLoadsClean) {
    for (const auto& error : loadWorkConfig(shippedConfigRoot())) {
        ADD_FAILURE() << error;
    }
    EXPECT_GT(ActionTypeRegistry::Get().size(), 0);
    EXPECT_GT(TaskChainRegistry::Get().size(), 0);
    EXPECT_GT(WorkTypeRegistry::Get().workTypeCount(), 0);
    EXPECT_TRUE(ConstructionRegistry::Get().materialsLoaded());
}

namespace {
    std::string priorityTuningWith(const std::string& maxDistance, const std::string& tier = "5") {
        return "<PriorityTuning><TaskTiers><Task name=\"Craft\" tier=\"" + tier + "\"/></TaskTiers><Bonuses>" +
               "<DistanceFactor>" + maxDistance + "<maxFactor>300</maxFactor></DistanceFactor>" +
               "<InProgress><bonus>50</bonus></InProgress>" +
               "<TaskAge><bonusPerMinute>1</bonusPerMinute><maxBonus>100</maxBonus></TaskAge>" +
               "<StoragePriority><weight>300</weight></StoragePriority></Bonuses></PriorityTuning>";
    }

    std::string moodWith(const std::string& easing) {
        return "<Mood>" + easing +
               "<NeedPenalty comfortable=\"70\" warning=\"30\" penaltyAtWarning=\"0.3\"/>"
               "<NeedWeights><Need name=\"Hunger\" weight=\"1\"/></NeedWeights>"
               "<Bands><Band name=\"Content\" from=\"0\"/></Bands></Mood>";
    }
} // namespace

TEST_F(WorkConfigTest, PriorityConfig_ShippedValuesArePinned) {
    ASSERT_TRUE(PriorityConfig::Get().loadFromFile(shippedConfigRoot(), "work/priority-tuning.xml"));
    const PriorityConfig& config = PriorityConfig::Get();
    EXPECT_TRUE(config.problems().empty());
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
    ASSERT_TRUE(config.loadFromFile(shippedConfigRoot(), "colonists/mood.xml"));
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
        EXPECT_NE(joined.find(std::string("<DistanceFactor> maxDistance must be a number, got \"") + text + "\""), std::string::npos) << text << ": " << joined;
    }
    for (const char* text : {"5x", "x", "", "2.5", "99999999999"}) {
        const std::string joined = errorsFor(priorityTuningWith("<maxDistance>60</maxDistance>", text));
        EXPECT_NE(joined.find(std::string("<Task name=\"Craft\"> tier must be a whole number, got \"") + text + "\""), std::string::npos) << text << ": " << joined;
    }
}

TEST_F(WorkConfigTest, PriorityConfig_ElementTextIsTrimmedAndALeadingPlusIsRead) {
    const std::string path = writeTempFile(
        "<PriorityTuning><Bonuses><DistanceFactor><maxDistance>\n  60\n</maxDistance><maxFactor>+300</maxFactor></DistanceFactor>"
        "<InProgress><bonus>-32768</bonus></InProgress><TaskAge><bonusPerMinute> 1 </bonusPerMinute><maxBonus>32767</maxBonus></TaskAge>"
        "<StoragePriority><weight>32767</weight></StoragePriority></Bonuses></PriorityTuning>",
        ".xml");
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
        "<PriorityTuning><Bonuses><DistanceFactor><maxDistance>\n  60\n</maxDistance><maxFactor>+300</maxFactor></DistanceFactor>"
        "<InProgress><bonus>40000</bonus></InProgress><TaskAge><bonusPerMinute> 1 </bonusPerMinute><maxBonus>-32769</maxBonus></TaskAge>"
        "<StoragePriority><weight>32767</weight></StoragePriority></Bonuses></PriorityTuning>",
        ".xml");
    EXPECT_FALSE(loadTemp(PriorityConfig::Get(), path));
    const PriorityConfig& config = PriorityConfig::Get();
    ASSERT_EQ(config.problems().size(), 2U);
    EXPECT_NE(config.problems()[0].find("<InProgress> bonus must be a whole number from -32768 to 32767, got \"40000\""), std::string::npos)
        << config.problems()[0];
    EXPECT_NE(config.problems()[1].find("<TaskAge> maxBonus must be a whole number from -32768 to 32767, got \"-32769\""), std::string::npos)
        << config.problems()[1];
}

TEST_F(WorkConfigTest, MoodConfig_BadNumbersAreErrorsNamingFileElementAndAttribute) {
    const auto errorsFor = [this](const std::string& xml) {
        const std::string path = writeTempFile(xml, ".xml");
        MoodConfig config;
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
    EXPECT_NE(errorsFor("<Mood><Easing pointsPerGameHour=\"5\"/><NeedPenalty comfortable=\"70\" warning=\"30\" penaltyAtWarning=\"0.3\"/>"
                        "<NeedWeights><Need name=\"Hunger\" weight=\"6abc\"/></NeedWeights><Bands><Band name=\"Content\" from=\"0\"/></Bands></Mood>")
                  .find("<Need name=\"Hunger\"> weight must be a number, got \"6abc\""),
              std::string::npos);
    EXPECT_NE(errorsFor(moodWith("<Easing/>")).find("<Easing> pointsPerGameHour is missing"), std::string::npos);
    for (const char* text : {"30abc", "abc", "", "nan"}) {
        const std::string joined = errorsFor(moodWith(std::string("<Easing pointsPerGameHour=\"") + text + "\"/>"));
        EXPECT_NE(joined.find(std::string("<Easing> pointsPerGameHour must be a number, got \"") + text + "\""), std::string::npos) << text << ": " << joined;
    }
}

TEST_F(WorkConfigTest, LoadWorkConfig_MissingFolderReportsError) {
    EXPECT_FALSE(loadWorkConfig(std::filesystem::temp_directory_path() / "no-such-work-config").empty());
}

TEST_F(WorkConfigTest, ConfigValidator_MemoryWindowMustBeAPositiveNumber) {
    for (const char* window : {"30", "0.5", "+30"}) {
        MemoryConfig config;
        ASSERT_TRUE(loadTemp(config, writeTempFile(std::string("<Memory><RecentSighting gameMinutes=\"") + window + "\"/></Memory>", ".xml")));
        ConfigValidator::clearErrors();
        EXPECT_TRUE(ConfigValidator::validateMemory(config)) << window;
    }

    for (const char* window : {"0", "-5"}) {
        MemoryConfig config;
        ASSERT_TRUE(loadTemp(config, writeTempFile(std::string("<Memory><RecentSighting gameMinutes=\"") + window + "\"/></Memory>", ".xml")));
        ConfigValidator::clearErrors();
        EXPECT_FALSE(ConfigValidator::validateMemory(config)) << window;
        EXPECT_EQ(ConfigValidator::getErrorCount(), 1);
    }

    const auto problemFor = [this](const std::string& sighting) {
        const std::string path = writeTempFile("<Memory>" + sighting + "</Memory>", ".xml");
        MemoryConfig config;
        EXPECT_FALSE(loadTemp(config, path)) << sighting;
        EXPECT_FALSE(config.loaded());
        return config.problems().size() == 1 ? config.problems().front().substr(fileNameOf(path).size()) : std::string("several problems");
    };
    EXPECT_EQ(problemFor(""), ": <RecentSighting> gameMinutes is missing");
    EXPECT_EQ(problemFor("<RecentSighting/>"), ": <RecentSighting> gameMinutes is missing");
    for (const char* window : {"abc", "30abc", "", " 30", "nan", "inf"}) {
        EXPECT_EQ(problemFor(std::string("<RecentSighting gameMinutes=\"") + window + "\"/>"),
                  std::string(": <RecentSighting> gameMinutes must be a number, got \"") + window + "\"");
    }
}

TEST_F(WorkConfigTest, LoadWorkConfig_ShippedValuesArePinned) {
    ASSERT_TRUE(loadWorkConfig(shippedConfigRoot()).empty());

    const TaskChainDef* haul = TaskChainRegistry::Get().getChain("Chain_PickupDeposit");
    ASSERT_NE(haul, nullptr);
    ASSERT_EQ(haul->stepCount(), 2U);
    EXPECT_EQ(haul->steps[0].order, 0);
    EXPECT_EQ(haul->steps[1].order, 1);
    const TaskChainDef* craft = TaskChainRegistry::Get().getChain("Chain_CraftWithGather");
    ASSERT_NE(craft, nullptr);
    ASSERT_EQ(craft->stepCount(), 3U);
    EXPECT_EQ(craft->steps[2].order, 2);
    EXPECT_EQ(craft->steps[2].actionDefName, "Deposit");
    EXPECT_EQ(craft->steps[2].target, "outputStorage");
    EXPECT_TRUE(craft->steps[2].optional);
    EXPECT_FALSE(craft->steps[1].optional) << "optional left out means the step must be done";
    EXPECT_TRUE(craft->steps[1].requiresPreviousStep) << "requiresPreviousStep left out means it waits";
    EXPECT_TRUE(WorkTypeRegistry::Get().getWorkType("Work_HaulToStorage")->filter.looseItem.value_or(false));
    EXPECT_FALSE(WorkTypeRegistry::Get().getWorkType("Work_HaulToStorage")->filter.indoor.has_value()) << "a filter element left out is no condition";

    const std::map<std::string, float> tiers = {
        {"Emergency", 1.0F}, {"Medical", 2.0F}, {"Farming", 3.0F}, {"Crafting", 4.0F}, {"Construction", 5.0F}, {"Hauling", 6.0F}, {"Cleaning", 7.0F}};
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

TEST_F(WorkConfigTest, TaskChainRegistry_MistakesNameTheChainAndLoadNothing) {
    const auto problemsFor = [this](const std::string& steps) {
        const std::string path = writeTempFile(
            "<TaskChains><Chain defName=\"Chain_Ok\"><steps><Step order=\"0\" action=\"A\" target=\"t\"/></steps></Chain>"
            "<Chain defName=\"Chain_Haul\"><steps>" + steps + "</steps></Chain></TaskChains>",
            ".xml");
        EXPECT_FALSE(loadTemp(TaskChainRegistry::Get(), path));
        EXPECT_EQ(TaskChainRegistry::Get().size(), 0U) << "a file with any mistake loads no chain";
        std::vector<std::string> problems = TaskChainRegistry::Get().problems();
        for (std::string& problem : problems) {
            EXPECT_EQ(problem.rfind(fileNameOf(path) + ": ", 0), 0U) << problem;
            problem.erase(0, fileNameOf(path).size() + 2);
        }
        return problems;
    };
    const std::string pickup = "<Step order=\"0\" action=\"Pickup\" target=\"source\"/>";

    EXPECT_EQ(problemsFor(pickup + "<Step action=\"Deposit\" target=\"dest\"/>"),
              std::vector<std::string>{"<Chain defName=\"Chain_Haul\"> step 2 order is missing"});
    EXPECT_EQ(problemsFor(pickup + "<Step order=\"1x\" action=\"Deposit\" target=\"dest\"/>"),
              std::vector<std::string>{"<Chain defName=\"Chain_Haul\"> step 2 order must be a whole number from 0 to 255, got \"1x\""});
    EXPECT_EQ(problemsFor(pickup + "<Step order=\"256\" action=\"Deposit\" target=\"dest\"/>"),
              std::vector<std::string>{"<Chain defName=\"Chain_Haul\"> step 2 order must be a whole number from 0 to 255, got \"256\""});
    EXPECT_EQ(problemsFor(pickup + "<Step order=\"1\" target=\"dest\"/>"), std::vector<std::string>{"<Chain defName=\"Chain_Haul\"> step 2 has no action"});
    EXPECT_EQ(problemsFor(pickup + "<Step order=\"1\" action=\"Deposit\" target=\"dest\" priority=\"2\"/>"),
              std::vector<std::string>{"<Chain defName=\"Chain_Haul\"> step 2: unknown attribute 'priority'"});
    EXPECT_EQ(problemsFor(""), std::vector<std::string>{"<Chain defName=\"Chain_Haul\"> has no <Step>"});
    EXPECT_EQ(problemsFor(pickup + "<Step order=\"1\" action=\"Deposit\"/>"), std::vector<std::string>{"<Chain defName=\"Chain_Haul\"> step 2 has no target"});
    EXPECT_EQ(problemsFor(pickup + "<Step order=\"0\" action=\"Deposit\" target=\"dest\"/>"),
              std::vector<std::string>{"<Chain defName=\"Chain_Haul\"> step 2 order must be different from every other step's order, got \"0\""});
    EXPECT_EQ(problemsFor(pickup + "<Step order=\"1\" action=\"Deposit\" target=\"dest\" optional=\"yes\"/>"),
              std::vector<std::string>{"<Chain defName=\"Chain_Haul\"> step 2 optional must be true or false, got \"yes\""});
    EXPECT_EQ(problemsFor(pickup + "<Step order=\"1\" action=\"Deposit\" target=\"dest\" requiresPreviousStep=\"ture\"/>"),
              std::vector<std::string>{"<Chain defName=\"Chain_Haul\"> step 2 requiresPreviousStep must be true or false, got \"ture\""});
}

TEST_F(WorkConfigTest, WorkTypeRegistry_MistakesNameTheInstanceAndLoadNothing) {
    const auto problemsFor = [this](const std::string& categoryAttributes, const std::string& workTypeBody) {
        const std::string path = writeTempFile(
            "<WorkTypes><Category defName=\"Emergency\" tier=\"1\"><WorkType defName=\"Work_Rescue\"><triggerCapability>Incapacitated</triggerCapability></WorkType></Category>"
            "<Category defName=\"Medical\"" + categoryAttributes + "><WorkType defName=\"Work_Doctoring\">" + workTypeBody + "</WorkType></Category></WorkTypes>",
            ".xml");
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
    EXPECT_EQ(problemsFor(" tier=\"second\"", ""), std::vector<std::string>{"<Category defName=\"Medical\"> tier must be a number, got \"second\""});
    EXPECT_EQ(problemsFor(" tier=\"2\"", "<skillRequired>Medicine</skillRequired><minSkillLevel>high</minSkillLevel>"),
              std::vector<std::string>{"<WorkType defName=\"Work_Doctoring\"> minSkillLevel must be a number, got \"high\""});
    EXPECT_EQ(problemsFor(" tier=\"2\" order=\"1\"", ""), std::vector<std::string>{"<Category defName=\"Medical\">: unknown attribute 'order'"});
    EXPECT_EQ(problemsFor(" tier=\"2\"", "<skill>Medicine</skill>"),
              std::vector<std::string>{"<WorkType defName=\"Work_Doctoring\">: unknown element <skill>"});
    EXPECT_EQ(problemsFor(" tier=\"-1\"", ""), std::vector<std::string>{"<Category defName=\"Medical\"> tier must be a number, 0 or more, got \"-1\""});
    EXPECT_EQ(problemsFor(" tier=\"2\"", "<skillRequired>Medicine</skillRequired><minSkillLevel>-2</minSkillLevel>"),
              std::vector<std::string>{"<WorkType defName=\"Work_Doctoring\"> minSkillLevel must be a number, 0 or more, got \"-2\""});
    EXPECT_EQ(problemsFor(" tier=\"2\" canDisable=\"no\"", ""),
              std::vector<std::string>{"<Category defName=\"Medical\"> canDisable must be true or false, got \"no\""});
    EXPECT_EQ(problemsFor(" tier=\"2\"", "<filter><looseItem>ture</looseItem></filter>"),
              std::vector<std::string>{"<WorkType defName=\"Work_Doctoring\"> filter looseItem must be true or false, got \"ture\""});
    EXPECT_EQ(problemsFor(" tier=\"2\"", "<filter><indoor>1</indoor></filter>"),
              std::vector<std::string>{"<WorkType defName=\"Work_Doctoring\"> filter indoor must be true or false, got \"1\""});
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
    {
        std::ifstream in(root / "work" / "task-chains.xml");
        std::string	  text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        const std::string step = "<Step order=\"1\" action=\"Deposit\" target=\"destination\"";
        ASSERT_NE(text.find(step), std::string::npos);
        text.replace(text.find(step), step.size(), "<Step order=\"one\" action=\"Deposit\" target=\"destination\"");
        std::ofstream(root / "work" / "task-chains.xml") << text;
    }
    const std::vector<std::string> errors = loadWorkConfig(root);
    std::filesystem::remove_all(root);
    EXPECT_EQ(errors, std::vector<std::string>{
                          "work/task-chains.xml: <Chain defName=\"Chain_PickupDeposit\"> step 2 order must be a whole number from 0 to 255, got \"one\""});
    EXPECT_EQ(TaskChainRegistry::Get().size(), 0U);
}

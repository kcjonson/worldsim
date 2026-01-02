// Unit tests for Work Configuration System
// Tests ActionTypeRegistry, TaskChainRegistry, WorkTypeRegistry,
// PriorityConfig, and ConfigValidator.

#include "assets/ActionTypeRegistry.h"
#include "assets/ConfigValidator.h"
#include "assets/PriorityConfig.h"
#include "assets/TaskChainRegistry.h"
#include "assets/WorkTypeRegistry.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>

using namespace engine::assets;

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
        ConfigValidator::clearErrors();
    }

    void TearDown() override {
        // Clean up temp files
        for (const auto& file : tempFiles) {
            std::remove(file.c_str());
        }
        tempFiles.clear();
    }

    // Write content to a temp file and return path
    std::string writeTempFile(const std::string& content, const std::string& suffix) {
        char nameTemplate[] = "/tmp/workconfig_XXXXXX";
        int fd = mkstemp(nameTemplate);
        if (fd == -1) return "";
        close(fd);

        std::string path = std::string(nameTemplate) + suffix;
        std::rename(nameTemplate, path.c_str());

        std::ofstream file(path);
        file << content;
        file.close();

        tempFiles.push_back(path);
        return path;
    }

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
    bool loaded = TaskChainRegistry::Get().loadFromFile(path);

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
    TaskChainRegistry::Get().loadFromFile(path);

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
    TaskChainRegistry::Get().loadFromFile(path);

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
    bool loaded = WorkTypeRegistry::Get().loadFromFile(path);

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
    WorkTypeRegistry::Get().loadFromFile(path);

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
    WorkTypeRegistry::Get().loadFromFile(path);

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
    WorkTypeRegistry::Get().loadFromFile(path);

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
  <Bands>
    <Band name="Critical" base="30000"/>
    <Band name="WorkHigh" base="5000"/>
  </Bands>
  <Bonuses>
    <Distance>
      <optimalDistance>5.0</optimalDistance>
      <maxPenaltyDistance>50.0</maxPenaltyDistance>
      <maxBonus>50</maxBonus>
      <maxPenalty>50</maxPenalty>
    </Distance>
    <ChainContinuation>
      <bonus>2000</bonus>
    </ChainContinuation>
  </Bonuses>
</PriorityTuning>)";

    std::string path = writeTempFile(xml, ".xml");
    bool loaded = PriorityConfig::Get().loadFromFile(path);

    EXPECT_TRUE(loaded);
    EXPECT_EQ(PriorityConfig::Get().getBandBase("Critical"), 30000);
    EXPECT_EQ(PriorityConfig::Get().getBandBase("WorkHigh"), 5000);
}

TEST_F(WorkConfigTest, PriorityConfig_DistanceBonus) {
    std::string xml = R"(<?xml version="1.0"?>
<PriorityTuning>
  <Bonuses>
    <Distance>
      <optimalDistance>5.0</optimalDistance>
      <maxPenaltyDistance>50.0</maxPenaltyDistance>
      <maxBonus>50</maxBonus>
      <maxPenalty>50</maxPenalty>
    </Distance>
  </Bonuses>
</PriorityTuning>)";

    std::string path = writeTempFile(xml, ".xml");
    PriorityConfig::Get().loadFromFile(path);

    // At optimal distance, should get max bonus
    EXPECT_EQ(PriorityConfig::Get().calculateDistanceBonus(3.0F), 50);

    // At max penalty distance, should get max penalty
    EXPECT_EQ(PriorityConfig::Get().calculateDistanceBonus(50.0F), -50);

    // Midpoint should be around 0
    int16_t midBonus = PriorityConfig::Get().calculateDistanceBonus(27.5F);
    EXPECT_GE(midBonus, -10);
    EXPECT_LE(midBonus, 10);
}

TEST_F(WorkConfigTest, PriorityConfig_ChainBonus) {
    std::string xml = R"(<?xml version="1.0"?>
<PriorityTuning>
  <Bonuses>
    <ChainContinuation>
      <bonus>2000</bonus>
    </ChainContinuation>
  </Bonuses>
</PriorityTuning>)";

    std::string path = writeTempFile(xml, ".xml");
    PriorityConfig::Get().loadFromFile(path);

    EXPECT_EQ(PriorityConfig::Get().getChainBonus(), 2000);
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
    TaskChainRegistry::Get().loadFromFile(chainPath);

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
    WorkTypeRegistry::Get().loadFromFile(workPath);

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
    TaskChainRegistry::Get().loadFromFile(chainPath);

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
    TaskChainRegistry::Get().loadFromFile(chainPath);

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
    WorkTypeRegistry::Get().loadFromFile(workPath);

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
    ASSERT_TRUE(TaskChainRegistry::Get().loadFromFile(chainPath));
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
    ASSERT_TRUE(WorkTypeRegistry::Get().loadFromFile(workPath));
    ASSERT_TRUE(ConfigValidator::validateWorkTypes());

    // 4. Priority config (deps: work types for category order)
    std::string priorityXml = R"(<?xml version="1.0"?>
<PriorityTuning>
  <WorkCategoryOrder>
    <Category name="Farming" tier="3"/>
    <Category name="Hauling" tier="6"/>
  </WorkCategoryOrder>
  <Bonuses>
    <ChainContinuation><bonus>2000</bonus></ChainContinuation>
  </Bonuses>
</PriorityTuning>)";
    std::string priorityPath = writeTempFile(priorityXml, "_priority.xml");
    ASSERT_TRUE(PriorityConfig::Get().loadFromFile(priorityPath));
    ASSERT_TRUE(ConfigValidator::validatePriorityConfig());

    // Verify everything loaded correctly
    EXPECT_EQ(ActionTypeRegistry::Get().size(), 4);
    EXPECT_EQ(TaskChainRegistry::Get().size(), 1);
    EXPECT_EQ(WorkTypeRegistry::Get().categoryCount(), 2);
    EXPECT_EQ(WorkTypeRegistry::Get().workTypeCount(), 2);
    EXPECT_EQ(PriorityConfig::Get().getChainBonus(), 2000);
}

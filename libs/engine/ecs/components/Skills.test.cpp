#include "Skills.h"

#include "assets/WorkTypeDef.h"

#include <gtest/gtest.h>

using namespace ecs;

// ============================================================================
// Basic Operations Tests
// ============================================================================

TEST(SkillsTests, DefaultConstruction) {
	Skills skills;

	EXPECT_EQ(skills.totalSkillPoints(), 0.0F);
	EXPECT_EQ(skills.countSkillsAbove(0.0F), 0);
}

TEST(SkillsTests, GetLevelReturnsZeroForUnknownSkill) {
	Skills skills;

	EXPECT_EQ(skills.getLevel("Farming"), 0.0F);
	EXPECT_EQ(skills.getLevel("NonexistentSkill"), 0.0F);
}

TEST(SkillsTests, SetAndGetLevel) {
	Skills skills;

	skills.setLevel("Farming", 5.0F);
	EXPECT_EQ(skills.getLevel("Farming"), 5.0F);

	skills.setLevel("Crafting", 10.0F);
	EXPECT_EQ(skills.getLevel("Crafting"), 10.0F);
}

TEST(SkillsTests, SetLevelClampsToValidRange) {
	Skills skills;

	// Test lower bound clamping
	skills.setLevel("Farming", -5.0F);
	EXPECT_EQ(skills.getLevel("Farming"), 0.0F);

	// Test upper bound clamping
	skills.setLevel("Crafting", 25.0F);
	EXPECT_EQ(skills.getLevel("Crafting"), 20.0F);

	// Test exact bounds
	skills.setLevel("Construction", 0.0F);
	EXPECT_EQ(skills.getLevel("Construction"), 0.0F);

	skills.setLevel("Medicine", 20.0F);
	EXPECT_EQ(skills.getLevel("Medicine"), 20.0F);
}

// ============================================================================
// Requirement Checking Tests
// ============================================================================

TEST(SkillsTests, MeetsRequirementWhenAbove) {
	Skills skills;
	skills.setLevel("Farming", 5.0F);

	EXPECT_TRUE(skills.meetsRequirement("Farming", 5.0F));
	EXPECT_TRUE(skills.meetsRequirement("Farming", 4.0F));
	EXPECT_TRUE(skills.meetsRequirement("Farming", 0.0F));
}

TEST(SkillsTests, MeetsRequirementWhenBelow) {
	Skills skills;
	skills.setLevel("Farming", 5.0F);

	EXPECT_FALSE(skills.meetsRequirement("Farming", 6.0F));
	EXPECT_FALSE(skills.meetsRequirement("Farming", 10.0F));
}

TEST(SkillsTests, MeetsRequirementForUntrainedSkill) {
	Skills skills;

	// Untrained skill (0.0) meets requirement of 0
	EXPECT_TRUE(skills.meetsRequirement("Unknown", 0.0F));

	// Untrained skill fails any positive requirement
	EXPECT_FALSE(skills.meetsRequirement("Unknown", 1.0F));
}

// ============================================================================
// WorkType Filtering Tests
// ============================================================================

TEST(SkillsTests, CanPerformWorkTypeWithNoRequirement) {
	Skills skills; // No skills at all

	engine::assets::WorkTypeDef workType;
	workType.defName = "Work_HarvestWild";
	workType.skillRequired = std::nullopt; // No skill required

	EXPECT_TRUE(skills.canPerformWorkType(workType));
}

TEST(SkillsTests, CanPerformWorkTypeWithMetRequirement) {
	Skills skills;
	skills.setLevel("Farming", 5.0F);

	engine::assets::WorkTypeDef workType;
	workType.defName = "Work_HarvestCrops";
	workType.skillRequired = "Farming";
	workType.minSkillLevel = 3.0F;

	EXPECT_TRUE(skills.canPerformWorkType(workType));
}

TEST(SkillsTests, CannotPerformWorkTypeWithUnmetRequirement) {
	Skills skills;
	skills.setLevel("Farming", 2.0F);

	engine::assets::WorkTypeDef workType;
	workType.defName = "Work_HarvestCrops";
	workType.skillRequired = "Farming";
	workType.minSkillLevel = 5.0F;

	EXPECT_FALSE(skills.canPerformWorkType(workType));
}

TEST(SkillsTests, CannotPerformWorkTypeWithUntrainedSkill) {
	Skills skills; // No skills

	engine::assets::WorkTypeDef workType;
	workType.defName = "Work_Doctoring";
	workType.skillRequired = "Medicine";
	workType.minSkillLevel = 1.0F;

	EXPECT_FALSE(skills.canPerformWorkType(workType));
}

// ============================================================================
// Utility Method Tests
// ============================================================================

TEST(SkillsTests, TotalSkillPoints) {
	Skills skills;

	EXPECT_EQ(skills.totalSkillPoints(), 0.0F);

	skills.setLevel("Farming", 5.0F);
	EXPECT_EQ(skills.totalSkillPoints(), 5.0F);

	skills.setLevel("Crafting", 3.0F);
	EXPECT_EQ(skills.totalSkillPoints(), 8.0F);

	skills.setLevel("Construction", 7.0F);
	EXPECT_EQ(skills.totalSkillPoints(), 15.0F);
}

TEST(SkillsTests, CountSkillsAbove) {
	Skills skills;
	skills.setLevel("Farming", 5.0F);
	skills.setLevel("Crafting", 10.0F);
	skills.setLevel("Construction", 15.0F);

	EXPECT_EQ(skills.countSkillsAbove(0.0F), 3);
	EXPECT_EQ(skills.countSkillsAbove(5.0F), 3); // 5, 10, 15 all >= 5
	EXPECT_EQ(skills.countSkillsAbove(6.0F), 2); // 10, 15
	EXPECT_EQ(skills.countSkillsAbove(10.0F), 2); // 10, 15
	EXPECT_EQ(skills.countSkillsAbove(11.0F), 1); // 15
	EXPECT_EQ(skills.countSkillsAbove(15.0F), 1); // 15
	EXPECT_EQ(skills.countSkillsAbove(16.0F), 0);
}

TEST(SkillsTests, ClearSkills) {
	Skills skills;
	skills.setLevel("Farming", 5.0F);
	skills.setLevel("Crafting", 10.0F);

	EXPECT_EQ(skills.totalSkillPoints(), 15.0F);

	skills.clear();

	EXPECT_EQ(skills.totalSkillPoints(), 0.0F);
	EXPECT_EQ(skills.getLevel("Farming"), 0.0F);
	EXPECT_EQ(skills.getLevel("Crafting"), 0.0F);
}

// ============================================================================
// Skill Level Description Tests
// ============================================================================

TEST(SkillsTests, SkillLevelDescriptions) {
	EXPECT_STREQ(SkillLevels::getDescription(0.0F), "Untrained");
	EXPECT_STREQ(SkillLevels::getDescription(0.5F), "Untrained");
	EXPECT_STREQ(SkillLevels::getDescription(1.0F), "Novice");
	EXPECT_STREQ(SkillLevels::getDescription(4.0F), "Novice");
	EXPECT_STREQ(SkillLevels::getDescription(5.0F), "Competent");
	EXPECT_STREQ(SkillLevels::getDescription(9.0F), "Competent");
	EXPECT_STREQ(SkillLevels::getDescription(10.0F), "Skilled");
	EXPECT_STREQ(SkillLevels::getDescription(14.0F), "Skilled");
	EXPECT_STREQ(SkillLevels::getDescription(15.0F), "Expert");
	EXPECT_STREQ(SkillLevels::getDescription(19.0F), "Expert");
	EXPECT_STREQ(SkillLevels::getDescription(20.0F), "Master");
}

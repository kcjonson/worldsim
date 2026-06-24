#include "Attributes.h"

#include <gtest/gtest.h>

using namespace ecs;

TEST(AttributesTests, DefaultsAreAverage) {
	Attributes a;
	EXPECT_FLOAT_EQ(a.strength, 10.0F);
	EXPECT_FLOAT_EQ(a.intelligence, 10.0F);
}

TEST(AttributesTests, CarryCapacityScalesWithStrength) {
	// 20kg base + 1.5kg per strength point; average (10) reproduces the prior flat 35kg.
	EXPECT_FLOAT_EQ(Attributes::carryCapacityKg(0.0F), 20.0F);
	EXPECT_FLOAT_EQ(Attributes::carryCapacityKg(10.0F), 35.0F);
	EXPECT_FLOAT_EQ(Attributes::carryCapacityKg(20.0F), 50.0F);
}

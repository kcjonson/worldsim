#include "Attributes.h"

#include "../ColonistRules.h"
#include "../ShippedConfig.test.h"

#include <gtest/gtest.h>

using namespace ecs;

namespace {

	AttributeRolls shippedRolls() {
		AttributeRolls rolls;
		const assets::CreatureConfigFiles human = ecs::testing::shippedHumanConfig();
		for (const std::string& error : loadAttributeRolls(rolls, human.attributes, human.folder)) {
			ADD_FAILURE() << error;
		}
		return rolls;
	}

} // namespace

TEST(AttributesTests, CarryCapacityScalesWithStrength) {
	EXPECT_FLOAT_EQ(Attributes::carryCapacityKg(0.0F), 20.0F);
	EXPECT_FLOAT_EQ(Attributes::carryCapacityKg(10.0F), 35.0F);
	EXPECT_FLOAT_EQ(Attributes::carryCapacityKg(20.0F), 50.0F);
}

TEST(AttributesTests, RollStaysInRangeAndIsDeterministicPerSeed) {
	const AttributeRolls rolls = shippedRolls();
	std::mt19937		 first(42);
	std::mt19937		 again(42);
	std::mt19937		 other(43);
	const Attributes	 rolled = rollAttributes(rolls, first);
	const Attributes	 repeated = rollAttributes(rolls, again);
	const Attributes	 different = rollAttributes(rolls, other);

	bool differs = false;
	for (std::size_t index = 0; index < kAttributeFields.size(); ++index) {
		const AttributeField& field = kAttributeFields[index];
		EXPECT_GE(rolled.*(field.value), rolls.ranges[index].min) << field.id;
		EXPECT_LE(rolled.*(field.value), rolls.ranges[index].max) << field.id;
		EXPECT_EQ(rolled.*(field.value), repeated.*(field.value)) << field.id;
		differs = differs || rolled.*(field.value) != different.*(field.value);
	}
	EXPECT_TRUE(differs) << "a different seed gives a different colonist";
}

TEST(AttributesTests, RollMeanIsTheMiddleOfEachRange) {
	const AttributeRolls rolls = shippedRolls();
	std::mt19937		 rng(7);
	constexpr int		 kRolls = 20000;
	std::array<double, kAttributeFields.size()> sums{};
	for (int roll = 0; roll < kRolls; ++roll) {
		const Attributes rolled = rollAttributes(rolls, rng);
		for (std::size_t index = 0; index < sums.size(); ++index) {
			sums[index] += static_cast<double>(rolled.*(kAttributeFields[index].value));
		}
	}
	for (std::size_t index = 0; index < sums.size(); ++index) {
		const double middle = (static_cast<double>(rolls.ranges[index].min) + static_cast<double>(rolls.ranges[index].max)) / 2.0;
		EXPECT_NEAR(sums[index] / kRolls, middle, 0.1) << kAttributeFields[index].id;
	}
}

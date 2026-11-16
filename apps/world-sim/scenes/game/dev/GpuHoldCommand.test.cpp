#include "scenes/game/dev/GpuHoldCommand.h"

#include <gtest/gtest.h>

#include <map>
#include <string>

namespace {

	using Params = std::map<std::string, std::string>;

	constexpr const char* kBadValue = "gpuhold: value must be 0 or 1";
	constexpr const char* kBadTarget = "gpuhold: target must be a number from 0.05 to 0.95";

} // namespace

TEST(GpuHoldCommand, ValueTurnsTheHoldOnOrOff) {
	std::string error;
	const auto	on = world_sim::parseGpuHoldCommand({{"value", "1"}}, error);
	ASSERT_TRUE(on.has_value());
	EXPECT_EQ(on->enabled, true);
	EXPECT_FALSE(on->target.has_value());

	const auto off = world_sim::parseGpuHoldCommand({{"value", "0"}, {"target", "0.6"}}, error);
	ASSERT_TRUE(off.has_value());
	EXPECT_EQ(off->enabled, false);
	ASSERT_TRUE(off->target.has_value());
	EXPECT_FLOAT_EQ(*off->target, 0.6F);
}

TEST(GpuHoldCommand, TargetTakesItsBoundsAndNothingIsAStateRead) {
	std::string error;
	for (const char* target : {"0.05", "0.95", "0.8"}) {
		const auto command = world_sim::parseGpuHoldCommand({{"target", target}}, error);
		ASSERT_TRUE(command.has_value()) << target;
		EXPECT_FALSE(command->enabled.has_value()) << "target alone leaves the hold as it is";
	}
	const auto none = world_sim::parseGpuHoldCommand({}, error);
	ASSERT_TRUE(none.has_value()) << "no parameters only reads the state back";
	EXPECT_FALSE(none->enabled.has_value());
	EXPECT_FALSE(none->target.has_value());
}

TEST(GpuHoldCommand, MalformedValuesAreRefusedWithTheMessage) {
	for (const Params& params :
		 {Params{{"value", "2"}}, Params{{"value", "true"}}, Params{{"value", ""}}, Params{{"value", "on"}, {"target", "0.5"}}}) {
		std::string error;
		EXPECT_FALSE(world_sim::parseGpuHoldCommand(params, error).has_value());
		EXPECT_EQ(error, kBadValue);
	}
	for (const char* target : {"0.04", "0.96", "abc", "0.5x", "nan", "inf", "", "-0.5"}) {
		std::string error;
		EXPECT_FALSE(world_sim::parseGpuHoldCommand({{"value", "1"}, {"target", target}}, error).has_value()) << target;
		EXPECT_EQ(error, kBadTarget) << target;
	}
}

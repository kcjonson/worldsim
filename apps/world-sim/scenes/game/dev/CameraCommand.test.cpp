#include "scenes/game/dev/CameraCommand.h"

#include <gtest/gtest.h>

#include <map>
#include <string>

namespace {

	using Params = std::map<std::string, std::string>;

	constexpr const char* kMalformed = "Camera parameters must be finite numbers (x+y together, zoom, panx, pany)";
	constexpr const char* kNothing = "Camera action requires x+y, zoom, panx, or pany parameters";

	std::optional<world_sim::CameraCommand> parse(const Params& params, std::string& error) {
		const char* message = nullptr;
		auto		command = world_sim::parseCameraCommand(params, message);
		error = message != nullptr ? message : "";
		return command;
	}

} // namespace

TEST(CameraCommand, PositionAndZoom) {
	std::string error;
	const auto	command = parse({{"x", "256"}, {"y", "-12.5"}, {"zoom", "3"}}, error);
	ASSERT_TRUE(command.has_value());
	EXPECT_TRUE(command->hasPosition);
	EXPECT_FLOAT_EQ(command->x, 256.0F);
	EXPECT_FLOAT_EQ(command->y, -12.5F);
	EXPECT_TRUE(command->hasZoom);
	EXPECT_FLOAT_EQ(command->zoom, 3.0F);
	EXPECT_FALSE(command->hasPan);
}

TEST(CameraCommand, PanIsClampedAndTheMissingAxisIsZero) {
	std::string error;
	const auto	command = parse({{"panx", "4"}}, error);
	ASSERT_TRUE(command.has_value());
	EXPECT_TRUE(command->hasPan);
	EXPECT_FLOAT_EQ(command->panX, 1.0F);
	EXPECT_FLOAT_EQ(command->panY, 0.0F);
	EXPECT_FALSE(command->hasPosition);
	EXPECT_FALSE(command->hasZoom);

	const auto stop = parse({{"panx", "0"}, {"pany", "-0.5"}}, error);
	ASSERT_TRUE(stop.has_value());
	EXPECT_FLOAT_EQ(stop->panX, 0.0F);
	EXPECT_FLOAT_EQ(stop->panY, -0.5F);
}

TEST(CameraCommand, MalformedValuesAreRefused) {
	for (const Params& params : {
			 Params{{"x", "1"}},
			 Params{{"y", "1"}},
			 Params{{"x", "abc"}, {"y", "1"}},
			 Params{{"x", "1.5x"}, {"y", "1"}},
			 Params{{"zoom", "inf"}},
			 Params{{"zoom", "nan"}},
			 Params{{"zoom", ""}},
			 Params{{"panx", "left"}},
		 }) {
		std::string error;
		EXPECT_FALSE(parse(params, error).has_value()) << params.begin()->first << "=" << params.begin()->second;
		EXPECT_EQ(error, kMalformed);
	}
}

TEST(CameraCommand, NothingToDoIsRefused) {
	std::string error;
	EXPECT_FALSE(parse({}, error).has_value());
	EXPECT_EQ(error, kNothing);
	EXPECT_FALSE(parse({{"speed", "2"}}, error).has_value());
	EXPECT_EQ(error, kNothing);
}

#include "scenes/game/dev/DevJson.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

TEST(DevJson, ANumberThatIsNotFiniteIsNullBecauseJsonHasNoNanOrInfinity) {
	EXPECT_EQ(world_sim::jsonNumber(std::numeric_limits<double>::quiet_NaN()), "null");
	EXPECT_EQ(world_sim::jsonNumber(std::numeric_limits<double>::infinity()), "null");
	EXPECT_EQ(world_sim::jsonNumber(-std::numeric_limits<double>::infinity()), "null");
}

TEST(DevJson, ANumberPrintsToThreeDecimals) {
	EXPECT_EQ(world_sim::jsonNumber(0.0), "0.000");
	EXPECT_EQ(world_sim::jsonNumber(-0.0), "-0.000");
	EXPECT_EQ(world_sim::jsonNumber(-0.0004), "-0.000");
	EXPECT_EQ(world_sim::jsonNumber(12.3456), "12.346");
	EXPECT_EQ(world_sim::jsonNumber(4.0), "4.000");
	EXPECT_EQ(world_sim::jsonNumber(-2.5), "-2.500");
}

TEST(DevJson, EveryByteEscapesTheWayJsonNeedsIt) {
	constexpr char kHexDigits[] = "0123456789abcdef";
	for (int value = 0; value < 256; ++value) {
		const char			character = static_cast<char>(value);
		std::string expected;
		switch (character) {
			case '"': expected = "\\\""; break;
			case '\\': expected = "\\\\"; break;
			case '\b': expected = "\\b"; break;
			case '\f': expected = "\\f"; break;
			case '\n': expected = "\\n"; break;
			case '\r': expected = "\\r"; break;
			case '\t': expected = "\\t"; break;
			default:
				if (value < 0x20) {
					expected = std::string("\\u00") + kHexDigits[value / 16] + kHexDigits[value % 16];
				} else {
					expected = std::string(1, character);
				}
		}
		EXPECT_EQ(world_sim::jsonEscape(std::string(1, character)), expected) << "byte " << value;
	}
}

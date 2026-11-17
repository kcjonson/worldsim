// Reading a config attribute's text as a number.

#include "assets/ConfigNumber.h"

#include <gtest/gtest.h>

using assets::parseConfigNumber;

TEST(ConfigNumber, ReadsWholeSignedAndDecimalNumbers) {
	EXPECT_EQ(parseConfigNumber("30"), 30.0F);
	EXPECT_EQ(parseConfigNumber("-6"), -6.0F);
	EXPECT_EQ(parseConfigNumber("+4.5"), 4.5F);
	EXPECT_EQ(parseConfigNumber("0.25"), 0.25F);
}

TEST(ConfigNumber, RefusesTextThatIsNotEntirelyANumber) {
	EXPECT_FALSE(parseConfigNumber("").has_value());
	EXPECT_FALSE(parseConfigNumber("30abc").has_value());
	EXPECT_FALSE(parseConfigNumber(" 30").has_value());
	EXPECT_FALSE(parseConfigNumber("+").has_value());
	EXPECT_FALSE(parseConfigNumber("++3").has_value());
	EXPECT_FALSE(parseConfigNumber("+-3").has_value());
	EXPECT_FALSE(parseConfigNumber("lots").has_value());
}

TEST(ConfigNumber, RefusesNumbersThatAreNotFinite) {
	for (const char* text : {"nan", "NaN", "-nan", "+nan", "inf", "-inf", "+inf", "infinity", "INF", "1e999"}) {
		EXPECT_FALSE(parseConfigNumber(text).has_value()) << text;
	}
}

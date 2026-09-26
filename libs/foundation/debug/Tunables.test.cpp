#include "debug/Tunables.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace Foundation;

// Tunables::instance() is process-wide, so each test uses its own name prefix.

TEST(Tunables, AddReturnsStableStorageSeededWithTheDefault) {
	Tunables&	 t		= Tunables::instance();
	const float* scalar = t.add("test/add/scalar", std::array{1.5F});
	const float* color	= t.add("test/add/color", std::array{0.1F, 0.2F, 0.3F});
	EXPECT_FLOAT_EQ(*scalar, 1.5F);
	EXPECT_FLOAT_EQ(color[2], 0.3F);

	// Registering many more entries must not move the first ones.
	for (int i = 0; i < 200; ++i) {
		t.add("test/add/filler" + std::to_string(i), std::array{0.0F});
	}
	EXPECT_EQ(t.add("test/add/scalar", std::array{9.0F}), scalar) << "a second add returns the same storage";
	EXPECT_FLOAT_EQ(*scalar, 1.5F) << "and keeps the current value, not the new default";
}

TEST(Tunables, SetChecksNameAndComponentCount) {
	Tunables&	 t	   = Tunables::instance();
	const float* color = t.add("test/set/color", std::array{0.0F, 0.0F, 0.0F});

	EXPECT_TRUE(t.set("test/set/color", std::array{0.5F, 0.6F, 0.7F}));
	EXPECT_FLOAT_EQ(color[1], 0.6F);
	EXPECT_FALSE(t.set("test/set/color", std::array{1.0F})) << "wrong component count";
	EXPECT_FALSE(t.set("test/set/missing", std::array{1.0F})) << "unknown name";
	EXPECT_FLOAT_EQ(color[0], 0.5F);
}

TEST(Tunables, SetRejectsNonFiniteValues) {
	Tunables&	 t = Tunables::instance();
	const float* scalar = t.add("test/set/nonfinite", std::array{1.0F});

	EXPECT_FALSE(t.set("test/set/nonfinite", std::array{std::numeric_limits<float>::quiet_NaN()})) << "nan";
	EXPECT_FALSE(t.set("test/set/nonfinite", std::array{std::numeric_limits<float>::infinity()})) << "inf";
	EXPECT_FLOAT_EQ(*scalar, 1.0F) << "a rejected set must not touch the stored value";
}

TEST(Tunables, ParseValuesAcceptsWellFormedSpecs) {
	std::vector<float> out;
	EXPECT_TRUE(Tunables::parseValues("1.6", out));
	EXPECT_EQ(out, (std::vector<float>{1.6F}));

	EXPECT_TRUE(Tunables::parseValues("0.2,0.3,0.4", out));
	EXPECT_EQ(out, (std::vector<float>{0.2F, 0.3F, 0.4F}));
}

TEST(Tunables, ParseValuesRejectsMalformedSpecs) {
	std::vector<float> out;
	for (const char* spec : {"1.6junk", "nan", "inf", "1,", ",1", "", "1,,2"}) {
		out.assign({99.0F}); // must be cleared on failure, not left with stale data
		EXPECT_FALSE(Tunables::parseValues(spec, out)) << "spec: '" << spec << "'";
		EXPECT_TRUE(out.empty()) << "spec: '" << spec << "'";
	}
}

TEST(Tunables, ResetByNameAndByPrefix) {
	Tunables&	 t = Tunables::instance();
	const float* a = t.add("test/reset/group/a", std::array{1.0F});
	const float* b = t.add("test/reset/group/b", std::array{2.0F});
	const float* c = t.add("test/reset/other", std::array{3.0F});
	t.set("test/reset/group/a", std::array{10.0F});
	t.set("test/reset/group/b", std::array{20.0F});
	t.set("test/reset/other", std::array{30.0F});

	EXPECT_EQ(t.reset("test/reset/group/a"), 1U);
	EXPECT_FLOAT_EQ(*a, 1.0F);
	EXPECT_EQ(t.reset("test/reset/*"), 3U);
	EXPECT_FLOAT_EQ(*b, 2.0F);
	EXPECT_FLOAT_EQ(*c, 3.0F);
}

TEST(Tunables, JsonListsOnlyThePrefix) {
	Tunables& t = Tunables::instance();
	t.add("test/json/x", std::array{0.25F});
	t.add("test/jsonother/y", std::array{1.0F});

	const std::string json = t.toJson("test/json/");
	EXPECT_EQ(json, "{\"tunables\":[{\"name\":\"test/json/x\",\"value\":[0.25],\"default\":[0.25]}]}");
}

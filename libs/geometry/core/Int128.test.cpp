#include "Int128.h"
#include "Vec2i64.h"

#include <cstdint>
#include <limits>
#include <gtest/gtest.h>

using namespace geometry;

namespace {

	constexpr std::int64_t kI64Min = std::numeric_limits<std::int64_t>::min();
	constexpr std::int64_t kI64Max = std::numeric_limits<std::int64_t>::max();

} // namespace

TEST(Int128, ProductSign) {
	EXPECT_EQ(Int128::product(3, 4).sign(), 1);
	EXPECT_EQ(Int128::product(-3, 4).sign(), -1);
	EXPECT_EQ(Int128::product(3, -4).sign(), -1);
	EXPECT_EQ(Int128::product(-3, -4).sign(), 1);
	EXPECT_EQ(Int128::product(0, 12345).sign(), 0);
}

TEST(Int128, ProductEquality) {
	EXPECT_EQ(Int128::product(6, 7), Int128::product(42, 1));
	EXPECT_NE(Int128::product(6, 7), Int128::product(6, 8));
}

TEST(Int128, AdditionCarryAcrossLimb) {
	// 2^63 * 2 = 2^64: forces a carry out of the low limb.
	Int128 big	= Int128::product(kI64Max, 2);		   // ~2^64
	Int128 more = big + Int128::product(kI64Max, 2);   // ~2^65
	EXPECT_EQ(more.sign(), 1);
	EXPECT_TRUE(big < more);
}

TEST(Int128, SubtractionBorrow) {
	Int128 a = Int128::product(1, 1);
	Int128 b = Int128::product(kI64Max, kI64Max); // huge positive
	EXPECT_EQ((a - b).sign(), -1);
	EXPECT_EQ((b - a).sign(), 1);
	EXPECT_EQ((b - b).sign(), 0);
}

TEST(Int128, NegationRoundTrip) {
	Int128 v = Int128::product(123456789, 987654321);
	EXPECT_EQ(-(-v), v);
	EXPECT_EQ((v + (-v)).sign(), 0);
}

TEST(Int128, Int64MinTimesNegativeOne) {
	// INT64_MIN * -1 overflows int64 but is representable in 128 bits.
	Int128 v = Int128::product(kI64Min, -1);
	EXPECT_EQ(v.sign(), 1);
	// It must equal +2^63, i.e. INT64_MAX + 1.
	Int128 expected = Int128(kI64Max) + Int128(1);
	EXPECT_EQ(v, expected);
}

TEST(Int128, Int64MinSquared) {
	Int128 v = Int128::product(kI64Min, kI64Min);
	EXPECT_EQ(v.sign(), 1);
	EXPECT_EQ(v, Int128::product(kI64Min, kI64Min));
}

TEST(Int128, Comparisons) {
	Int128 a = Int128::product(5, 5);
	Int128 b = Int128::product(6, 6);
	EXPECT_TRUE(a < b);
	EXPECT_TRUE(b > a);
	EXPECT_TRUE(a <= a);
	EXPECT_TRUE(a >= a);
	EXPECT_FALSE(a > b);

	Int128 neg = -b;
	EXPECT_TRUE(neg < a);
	EXPECT_TRUE(neg < Int128(0));
}

TEST(Int128, CompareSquareToProduct) {
	// 5^2 == 5*5
	EXPECT_EQ(Int128::compareSquareToProduct(Int128(5), Int128(5), Int128(5)), 0);
	// 5^2 < 6*5
	EXPECT_EQ(Int128::compareSquareToProduct(Int128(5), Int128(6), Int128(5)), -1);
	// 6^2 > 5*5
	EXPECT_EQ(Int128::compareSquareToProduct(Int128(6), Int128(5), Int128(5)), 1);
}

TEST(Int128, CompareSquareToProductLarge) {
	// Use 128-bit magnitudes to exercise the full 256-bit multiply.
	Int128 c = Int128::product(kI64Max, kI64Max); // ~2^126
	Int128 a = c;
	Int128 b = Int128(1);
	// c^2 vs (c * 1): c^2 >> c for c > 1.
	EXPECT_EQ(Int128::compareSquareToProduct(c, a, b), 1);
	// c^2 vs c*c == 0.
	EXPECT_EQ(Int128::compareSquareToProduct(c, c, c), 0);
}

TEST(Vec2i64, Arithmetic) {
	Vec2i64 a{3, 4};
	Vec2i64 b{1, 2};
	EXPECT_EQ(a + b, (Vec2i64{4, 6}));
	EXPECT_EQ(a - b, (Vec2i64{2, 2}));
	EXPECT_EQ(-a, (Vec2i64{-3, -4}));
	EXPECT_EQ(a * 2, (Vec2i64{6, 8}));
	EXPECT_EQ(3 * b, (Vec2i64{3, 6}));
}

TEST(Vec2i64, Ordering) {
	EXPECT_TRUE((Vec2i64{1, 2}) < (Vec2i64{1, 3}));
	EXPECT_TRUE((Vec2i64{1, 9}) < (Vec2i64{2, 0}));
	EXPECT_FALSE((Vec2i64{2, 0}) < (Vec2i64{1, 9}));
}

TEST(Vec2i64, DotCross) {
	Vec2i64 a{2, 0};
	Vec2i64 b{0, 3};
	EXPECT_EQ(dot(a, b).sign(), 0);
	EXPECT_EQ(cross(a, b), Int128::product(2, 3));
	EXPECT_EQ(cross(b, a).sign(), -1);
}

TEST(Vec2i64, QuantizeRoundTrip) {
	Foundation::Vec2 m{1.5F, -2.25F};
	Vec2i64			 q = quantize(m);
	EXPECT_EQ(q.x, 1500);
	EXPECT_EQ(q.y, -2250);

	Foundation::Vec2 back = dequantize(q);
	EXPECT_FLOAT_EQ(back.x, 1.5F);
	EXPECT_FLOAT_EQ(back.y, -2.25F);
}

TEST(Vec2i64, QuantizeRoundsToNearestMillimeter) {
	// 1.23456 m -> 1234.56 mm -> rounds to 1235 mm.
	Vec2i64 q = quantize(Foundation::Vec2{1.23456F, 0.0F});
	EXPECT_EQ(q.x, 1235);
}

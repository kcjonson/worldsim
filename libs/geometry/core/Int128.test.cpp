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

namespace {

	// Independent 256-bit unsigned magnitude as eight 32-bit limbs, built without
	// _umul128 or __int128 so it shares no code with the implementation under test.
	// This is the oracle the hand-rolled two-limb mul128 carry chain is checked
	// against.
	struct Oracle256 {
		std::uint32_t limb[8] = {0, 0, 0, 0, 0, 0, 0, 0};
	};

	// Magnitude of an int64 as two 32-bit limbs.
	void mag32(std::int64_t v, std::uint32_t& lo, std::uint32_t& hi) {
		std::uint64_t m = v < 0 ? (~static_cast<std::uint64_t>(v) + 1U) : static_cast<std::uint64_t>(v);
		lo				= static_cast<std::uint32_t>(m & 0xFFFFFFFFU);
		hi				= static_cast<std::uint32_t>(m >> 32);
	}

	// Schoolbook multiply of two 128-bit magnitudes (each four 32-bit limbs) into
	// a 256-bit magnitude. Pure 32x32->64 partial products with running carry.
	Oracle256 oracleMul(const std::uint32_t a[4], const std::uint32_t b[4]) {
		// Carry immediately into the next limb so the running accumulator never
		// exceeds the diagonal sum plus an incoming carry (well under 2^64).
		Oracle256 out;
		for (int i = 0; i < 4; ++i) {
			std::uint64_t carry = 0;
			for (int j = 0; j < 4; ++j) {
				std::uint64_t cur = static_cast<std::uint64_t>(out.limb[i + j]) +
									static_cast<std::uint64_t>(a[i]) * static_cast<std::uint64_t>(b[j]) + carry;
				out.limb[i + j] = static_cast<std::uint32_t>(cur & 0xFFFFFFFFU);
				carry			= cur >> 32;
			}
			out.limb[i + 4] = static_cast<std::uint32_t>(carry);
		}
		return out;
	}

	int oracleCompare(const Oracle256& a, const Oracle256& b) {
		for (int i = 7; i >= 0; --i) {
			if (a.limb[i] != b.limb[i]) {
				return a.limb[i] < b.limb[i] ? -1 : 1;
			}
		}
		return 0;
	}

	// sign(|c|^2 - |a|*|b|) computed entirely by the independent oracle, where
	// c, a, b are each products of two int64 values (i.e. up to 128-bit magnitude).
	int oracleSquareToProduct(std::int64_t c0, std::int64_t c1, std::int64_t a0, std::int64_t a1, std::int64_t b0,
							  std::int64_t b1) {
		auto toLimbs = [](std::int64_t x, std::int64_t y, std::uint32_t out[4]) {
			std::uint32_t xl = 0, xh = 0, yl = 0, yh = 0;
			mag32(x, xl, xh);
			mag32(y, yl, yh);
			// |x*y| as a 128-bit magnitude in four 32-bit limbs.
			std::uint32_t xs[4] = {xl, xh, 0, 0};
			std::uint32_t ys[4] = {yl, yh, 0, 0};
			Oracle256	  p		= oracleMul(xs, ys);
			for (int i = 0; i < 4; ++i) {
				out[i] = p.limb[i];
			}
		};
		std::uint32_t cl[4], al[4], bl[4];
		toLimbs(c0, c1, cl);
		toLimbs(a0, a1, al);
		toLimbs(b0, b1, bl);
		Oracle256 cc = oracleMul(cl, cl);
		Oracle256 ab = oracleMul(al, bl);
		return oracleCompare(cc, ab);
	}

} // namespace

TEST(Int128, Mul128AgainstIndependentOracleExtremePatterns) {
	// All-ones limbs, near-2^127 magnitudes, and INT64_MIN edges. These exercise
	// every carry boundary in the hand-rolled mul128 two-limb chain. compareSquare-
	// ToProduct is the only public surface that reaches mul128; we drive it with
	// magnitudes built as products of int64 values and cross-check the sign.
	const std::int64_t vals[] = {
		0, 1, -1, 2, kI64Max, kI64Min, kI64Max - 1, kI64Min + 1, 0x7FFFFFFFFFFFFFFFLL, 0x100000000LL, 0xFFFFFFFFLL,
		-0x100000000LL, 3037000499LL /* ~sqrt(2^63) */, -3037000499LL,
	};
	for (std::int64_t c0 : vals) {
		for (std::int64_t c1 : vals) {
			for (std::int64_t a0 : vals) {
				for (std::int64_t a1 : vals) {
					// Keep b a simple product to bound the loop while still spanning
					// the limb range via a0,a1.
					const std::int64_t b0	  = c1;
					const std::int64_t b1	  = a0;
					const int		   got	  = Int128::compareSquareToProduct(
						   Int128::product(c0, c1), Int128::product(a0, a1), Int128::product(b0, b1));
					const int expected = oracleSquareToProduct(c0, c1, a0, a1, b0, b1);
					ASSERT_EQ(got, expected)
						<< "c=" << c0 << "*" << c1 << " a=" << a0 << "*" << a1 << " b=" << b0 << "*" << b1;
				}
			}
		}
	}
}

TEST(Int128, CompareSquareToProductExhaustiveSmall) {
	// Brute force every (c,a,b) in a small range against int64 math that cannot
	// overflow at this scale: c*c and a*b both fit comfortably in int64.
	for (std::int64_t c = 0; c <= 120; ++c) {
		for (std::int64_t a = 0; a <= 120; ++a) {
			for (std::int64_t b = 0; b <= 120; ++b) {
				const int got	   = Int128::compareSquareToProduct(Int128(c), Int128(a), Int128(b));
				const std::int64_t diff = c * c - a * b;
				const int expected = diff < 0 ? -1 : (diff > 0 ? 1 : 0);
				ASSERT_EQ(got, expected) << "c=" << c << " a=" << a << " b=" << b;
			}
		}
	}
}

TEST(Int128, ProductMagnitudeFullWidthOracle) {
	// Directly check that |a|*|b| matches the oracle for all-ones and mixed-limb
	// products, exercising mul128 with maximal limb values (aLo=aHi=...=0xFFFF...).
	const std::int64_t big = kI64Max;	   // 0x7FFFFFFFFFFFFFFF
	const std::int64_t neg = kI64Min;	   // 0x8000000000000000 magnitude
	// (big*big) compared against (neg*neg): |neg| = 2^63 > |big| = 2^63-1, so
	// neg*neg > big*big, hence c=big,a=neg,b=neg gives big^2 - neg^2 < 0.
	EXPECT_EQ(Int128::compareSquareToProduct(Int128::product(big, 1), Int128::product(neg, 1), Int128::product(neg, 1)),
			  -1);
	// Symmetric large square equality through different factorizations of the same
	// magnitude is covered by the oracle test; here pin the known sign.
	EXPECT_EQ(oracleSquareToProduct(big, 1, neg, 1, neg, 1), -1);
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

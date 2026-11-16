#include "ClipRing.h"
#include "../polygon/Polygon.h"
#include "MarchingSquares.h"
#include "RingPins.h"
#include "ScalarField.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>
#include <gtest/gtest.h>

using namespace geometry;

namespace {

	const RectMm kRect{{0, 0}, {10000, 10000}};

	Int128 totalArea2(const std::vector<Ring>& rings) {
		Int128 acc(0);
		for (const Ring& r : rings) {
			acc = acc + signedAreaDoubled(r);
		}
		return acc;
	}

	void expectValidPieces(const std::vector<Ring>& pieces, Winding winding) {
		for (const Ring& p : pieces) {
			EXPECT_GE(p.size(), 3u);
			EXPECT_EQ(windingOrder(p), winding);
			EXPECT_TRUE(isSimple(p).pass);
		}
	}

	Ring reversed(const Ring& r) { return Ring(r.rbegin(), r.rend()); }

	// A concave "U" opening upward across the bottom border y = 0: both legs cross
	// the border, the bridge between them stays below it.
	Ring uShape() {
		return {{1000, -3000}, {9000, -3000}, {9000, 4000}, {6000, 4000}, {6000, -1000}, {4000, -1000}, {4000, 4000}, {1000, 4000}};
	}

} // namespace

TEST(ClipRingToRect, FullyInsideIsUnchanged) {
	const Ring				r	   = {{1000, 1000}, {5000, 1200}, {3000, 6000}};
	const std::vector<Ring> pieces = clipRingToRect(r, kRect);
	ASSERT_EQ(pieces.size(), 1u);
	EXPECT_EQ(pieces[0], r);
}

TEST(ClipRingToRect, InsideTouchingBoundaryIsUnchanged) {
	// Vertices on the boundary and an edge along the bottom running CCW.
	const Ring				r	   = {{0, 0}, {6000, 0}, {3000, 5000}, {0, 5000}};
	const std::vector<Ring> pieces = clipRingToRect(r, kRect);
	ASSERT_EQ(pieces.size(), 1u);
	EXPECT_EQ(pieces[0], r);
}

TEST(ClipRingToRect, FullyOutsideIsEmpty) {
	EXPECT_TRUE(clipRingToRect({{20000, 0}, {30000, 0}, {25000, 5000}}, kRect).empty());
}

TEST(ClipRingToRect, OutsideSharingABoundaryEdgeIsEmpty) {
	// A square directly below the rect shares part of y = 0; no area overlaps.
	const Ring below = {{2000, -4000}, {8000, -4000}, {8000, 0}, {2000, 0}};
	EXPECT_TRUE(clipRingToRect(below, kRect).empty());
	EXPECT_TRUE(clipRingToRect(reversed(below), kRect).empty());
}

TEST(ClipRingToRect, ContainingRingGivesRectInRingOrientation) {
	const Ring big = {{-5000, -5000}, {15000, -5000}, {15000, 15000}, {-5000, 15000}};
	const std::vector<Ring> ccw = clipRingToRect(big, kRect);
	ASSERT_EQ(ccw.size(), 1u);
	EXPECT_EQ(windingOrder(ccw[0]), Winding::CounterClockwise);
	EXPECT_EQ(signedAreaDoubled(ccw[0]), Int128(2 * 10000LL * 10000LL));

	const std::vector<Ring> cw = clipRingToRect(reversed(big), kRect);
	ASSERT_EQ(cw.size(), 1u);
	EXPECT_EQ(windingOrder(cw[0]), Winding::Clockwise);
	EXPECT_EQ(signedAreaDoubled(cw[0]), Int128(-2 * 10000LL * 10000LL));
}

TEST(ClipRingToRect, StraddlingRingClipsToOverlap) {
	// [-5000, 5000] x [2000, 6000] clipped at x = 0: [0, 5000] x [2000, 6000].
	const Ring				r	   = {{-5000, 2000}, {5000, 2000}, {5000, 6000}, {-5000, 6000}};
	const std::vector<Ring> pieces = clipRingToRect(r, kRect);
	ASSERT_EQ(pieces.size(), 1u);
	expectValidPieces(pieces, Winding::CounterClockwise);
	EXPECT_EQ(signedAreaDoubled(pieces[0]), Int128(2 * 5000LL * 4000LL));
	EXPECT_NE(std::find(pieces[0].begin(), pieces[0].end(), Vec2i64{0, 2000}), pieces[0].end());
	EXPECT_NE(std::find(pieces[0].begin(), pieces[0].end(), Vec2i64{0, 6000}), pieces[0].end());
}

TEST(ClipRingToRect, EdgeAlongBoundaryInsideSide) {
	// Shares the whole bottom boundary with the rect, running CCW along it.
	const Ring				r	   = {{0, -5000}, {10000, -5000}, {10000, 5000}, {0, 5000}};
	const std::vector<Ring> pieces = clipRingToRect(r, kRect);
	ASSERT_EQ(pieces.size(), 1u);
	expectValidPieces(pieces, Winding::CounterClockwise);
	EXPECT_EQ(signedAreaDoubled(pieces[0]), Int128(2 * 10000LL * 5000LL));
}

TEST(ClipRingToRect, ConcaveRingSplitsIntoDisconnectedPieces) {
	const std::vector<Ring> pieces = clipRingToRect(uShape(), kRect);
	ASSERT_EQ(pieces.size(), 2u);
	expectValidPieces(pieces, Winding::CounterClockwise);
	// Two legs, each 3000 wide and 4000 tall above y = 0.
	EXPECT_EQ(totalArea2(pieces), Int128(2 * 2 * 3000LL * 4000LL));
}

TEST(ClipRingToRect, HoleRingKeepsClockwise) {
	const std::vector<Ring> pieces = clipRingToRect(reversed(uShape()), kRect);
	ASSERT_EQ(pieces.size(), 2u);
	expectValidPieces(pieces, Winding::Clockwise);
	EXPECT_EQ(totalArea2(pieces), Int128(-2 * 2 * 3000LL * 4000LL));
}

TEST(ClipRingToRect, WrapsAroundCorners) {
	// A square centered on the rect's lower-right corner: the piece inside is
	// closed along the boundary through that corner.
	const Ring r = {{8000, -2000}, {12000, -2000}, {12000, 2000}, {8000, 2000}};
	const std::vector<Ring> pieces = clipRingToRect(r, kRect);
	ASSERT_EQ(pieces.size(), 1u);
	expectValidPieces(pieces, Winding::CounterClockwise);
	EXPECT_EQ(signedAreaDoubled(pieces[0]), Int128(2 * 2000LL * 2000LL));
	EXPECT_NE(std::find(pieces[0].begin(), pieces[0].end(), Vec2i64{10000, 0}), pieces[0].end());
}

TEST(ClipRingToRect, NeighborRectsShareBorderVerticesAndPartitionArea) {
	// A slanted ring across x = 10000 clipped to both sides: the pieces meet at the
	// same rounded border points and their areas sum to the ring's (up to the
	// rounding of those two points).
	const Ring	 r = {{7000, 1000}, {13001, 2002}, {12500, 7777}, {6003, 6001}};
	const RectMm right{{10000, 0}, {20000, 10000}};
	const std::vector<Ring> left  = clipRingToRect(r, kRect);
	const std::vector<Ring> rightP = clipRingToRect(r, right);
	ASSERT_EQ(left.size(), 1u);
	ASSERT_EQ(rightP.size(), 1u);
	expectValidPieces(left, Winding::CounterClockwise);
	expectValidPieces(rightP, Winding::CounterClockwise);
	Ring borderLeft;
	Ring borderRight;
	for (const Vec2i64& v : left[0]) {
		if (v.x == 10000) {
			borderLeft.push_back(v);
		}
	}
	for (const Vec2i64& v : rightP[0]) {
		if (v.x == 10000) {
			borderRight.push_back(v);
		}
	}
	std::sort(borderLeft.begin(), borderLeft.end());
	std::sort(borderRight.begin(), borderRight.end());
	ASSERT_EQ(borderLeft.size(), 2u);
	EXPECT_EQ(borderLeft, borderRight);
	const double whole = signedAreaDoubled(r).toDouble();
	const double parts = signedAreaDoubled(left[0]).toDouble() + signedAreaDoubled(rightP[0]).toDouble();
	EXPECT_NEAR(parts, whole, 2.0 * 8000.0);
}

TEST(ClipRingToRect, RandomShoresPartitionAcrossATiling) {
	// Marched rings from random smooth fields, clipped against a 3 x 3 tiling of
	// rects that covers them: every piece is simple and in the ring's orientation,
	// and the pieces' areas sum exactly to the ring's area once the tiling lines are
	// pinned into it (the pieces share those pinned border points).
	const std::array<std::int64_t, 4> xLines = {-1000, 6003, 12000, 21000};
	const std::array<std::int64_t, 4> yLines = {-1000, 4750, 11011, 17000};
	for (std::uint32_t seed = 1; seed <= 4; ++seed) {
		std::mt19937						   rng(seed);
		std::uniform_real_distribution<double> freq(0.0006, 0.003);
		std::uniform_real_distribution<double> phase(0.0, 6.283);
		std::array<std::array<double, 3>, 4>   waves{};
		for (auto& w : waves) {
			w = {freq(rng), freq(rng), phase(rng)};
		}
		ScalarField f({0, 0}, 250, 80, 64);
		for (int y = 1; y < f.height - 1; ++y) {
			for (int x = 1; x < f.width - 1; ++x) {
				const Vec2i64 p = f.samplePositionMm(x, y);
				double		  v = 0.5;
				for (const auto& w : waves) {
					v += 0.3 * std::sin(static_cast<double>(p.x) * w[0] + static_cast<double>(p.y) * w[1] + w[2]);
				}
				f.at(x, y) = static_cast<float>(v);
			}
		}

		const std::vector<Ring> rings = marchingSquares(f, 0.5F);
		ASSERT_FALSE(rings.empty()) << "seed " << seed;
		for (const Ring& ring : rings) {
			// Pinned in advance, the crossings are already ring vertices lying on
			// the rect boundaries (the builder's case); the pieces must not change.
			Ring pinned = ring;
			pinAxisLineCrossings(pinned, xLines, yLines);

			const Winding winding = windingOrder(ring);
			Int128		  pieceArea(0);
			for (std::size_t i = 0; i + 1 < xLines.size(); ++i) {
				for (std::size_t j = 0; j + 1 < yLines.size(); ++j) {
					const RectMm			rect{{xLines[i], yLines[j]}, {xLines[i + 1], yLines[j + 1]}};
					const std::vector<Ring> pieces = clipRingToRect(ring, rect);
					for (const Ring& piece : pieces) {
						EXPECT_TRUE(isSimple(piece).pass) << "seed " << seed;
						EXPECT_EQ(windingOrder(piece), winding) << "seed " << seed;
						pieceArea = pieceArea + signedAreaDoubled(piece);
					}
					std::vector<Ring> fromPinned = clipRingToRect(pinned, rect);
					EXPECT_EQ(fromPinned.size(), pieces.size()) << "seed " << seed;
					Int128 pinnedArea(0);
					for (const Ring& piece : fromPinned) {
						pinnedArea = pinnedArea + signedAreaDoubled(piece);
					}
					Int128 directArea(0);
					for (const Ring& piece : pieces) {
						directArea = directArea + signedAreaDoubled(piece);
					}
					EXPECT_EQ(pinnedArea, directArea) << "seed " << seed;
				}
			}
			EXPECT_EQ(pieceArea, signedAreaDoubled(pinned)) << "seed " << seed;
		}
	}
}

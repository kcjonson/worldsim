#include "world/rendering/PileLayout.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

using worldview::pileLayout;
using worldview::pileSpriteCount;

TEST(PileLayout, SpriteCountFollowsFillFraction) {
	EXPECT_EQ(pileSpriteCount(1, 40), 1U);
	EXPECT_EQ(pileSpriteCount(2, 40), 2U) << "any multi-unit pile shows at least 2";
	EXPECT_EQ(pileSpriteCount(16, 40), 2U);
	EXPECT_EQ(pileSpriteCount(30, 40), 4U);
	EXPECT_EQ(pileSpriteCount(40, 40), 5U);
	EXPECT_EQ(pileSpriteCount(3, 0), 3U) << "unbounded: one per unit";
	EXPECT_EQ(pileSpriteCount(500, UINT32_MAX), 5U);
}

// A single unit draws where a lone dynamic entity draws, so a pile of one looks unchanged.
TEST(PileLayout, SingleUnitMatchesDynamicTransform) {
	const glm::vec2 pos{10.0F, -4.0F};
	const glm::vec2 boundsCenter{0.5F, 0.2F};
	const auto		pile = pileLayout(pos, "Wood", 1.0F, {1, 1, 1, 1}, Foundation::Rect{0.0F, 0.0F, 1.0F, 0.4F}, {0.4F, 40}, 1);
	ASSERT_EQ(pile.count, 1U);
	const glm::vec2 want = worldview::dynamicInstanceTransform(pos, 1.0F, boundsCenter).origin;
	EXPECT_FLOAT_EQ(pile.sprites[0].position.x, want.x);
	EXPECT_FLOAT_EQ(pile.sprites[0].position.y, want.y);
	EXPECT_FLOAT_EQ(pile.sprites[0].anchorY, want.y + 0.4F);
}

// A full stack of 0.4 m logs stays a compact heap centered on the entity: sprite centers
// within ~0.5 m of each other, deterministic, and front rows sort in front of back rows.
TEST(PileLayout, FullPileIsCompactAndCentered) {
	const glm::vec2 pos{0.0F, 0.0F};
	const auto		pile = pileLayout(pos, "Wood", 1.0F, {1, 1, 1, 1}, Foundation::Rect{-0.5F, -0.2F, 1.0F, 0.4F}, {0.4F, 40}, 40);
	ASSERT_EQ(pile.count, 5U);
	float sumY = 0.0F;
	for (std::size_t i = 0; i < pile.count; ++i) {
		for (std::size_t j = i + 1; j < pile.count; ++j) {
			const float dx = pile.sprites[i].position.x - pile.sprites[j].position.x;
			const float dy = pile.sprites[i].position.y - pile.sprites[j].position.y;
			EXPECT_LE(std::sqrt(dx * dx + dy * dy), 0.5F);
		}
		if (i > 0) {
			EXPECT_GT(pile.sprites[i].anchorY, pile.sprites[i - 1].anchorY);
		}
		sumY += pile.sprites[i].position.y;
	}
	EXPECT_NEAR(sumY / static_cast<float>(pile.count), 0.0F, 1e-5F);
}

// The footprint holds every sprite of a FULL pile, so a pile that grows by top-ups stays inside
// the room its drop left it; an unstackable item's footprint is just its one sprite.
TEST(PileLayout, FootprintHoldsFullPile) {
	const Foundation::Rect		 bounds{0.0F, 0.0F, 1.07F, 0.4F};
	const worldview::PileItem	 wood{0.4F, 40};
	const float					 radius = worldview::pileFootprintRadius(bounds, 1.0F, wood);
	const auto					 full = pileLayout({0.0F, 0.0F}, "Wood", 1.0F, {1, 1, 1, 1}, bounds, wood, 40);
	for (std::size_t i = 0; i < full.count; ++i) {
		const glm::vec2 lo = full.sprites[i].position;
		const glm::vec2 hi = lo + glm::vec2{bounds.width, bounds.height};
		for (const glm::vec2 corner : {lo, hi, glm::vec2{lo.x, hi.y}, glm::vec2{hi.x, lo.y}}) {
			EXPECT_LE(std::sqrt(corner.x * corner.x + corner.y * corner.y), radius + 1e-5F);
		}
	}
	EXPECT_GT(radius, 0.535F) << "at least half of one log's width";

	// At half scale the heap's row offsets shrink with the sprites, and the footprint still holds it.
	const float halfRadius = worldview::pileFootprintRadius(bounds, 0.5F, wood);
	const auto	half = pileLayout({0.0F, 0.0F}, "Wood", 0.5F, {1, 1, 1, 1}, bounds, wood, 40);
	EXPECT_LT(halfRadius, radius);
	EXPECT_NEAR(half.sprites[1].position.y - half.sprites[0].position.y, 0.5F * (full.sprites[1].position.y - full.sprites[0].position.y), 1e-5F);
	for (std::size_t i = 0; i < half.count; ++i) {
		const glm::vec2 lo = half.sprites[i].position;
		const glm::vec2 hi = lo + 0.5F * glm::vec2{bounds.width, bounds.height};
		for (const glm::vec2 corner : {lo, hi, glm::vec2{lo.x, hi.y}, glm::vec2{hi.x, lo.y}}) {
			EXPECT_LE(std::sqrt(corner.x * corner.x + corner.y * corner.y), halfRadius + 1e-5F);
		}
	}

	const float single = worldview::pileFootprintRadius(Foundation::Rect{-0.2F, -0.1F, 0.4F, 0.2F}, 1.0F, {0.3F, 1});
	EXPECT_NEAR(single, std::hypot(0.2F, 0.1F), 1e-5F);
}

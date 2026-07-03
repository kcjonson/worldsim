// Tests for the shared asset instance transform (Story C): the one local<->world
// affine that render, outline, and hit-test share. Guards the forward/inverse
// round-trip for both conventions and parity with the render paths' own math, so
// a hit-test can't drift off the drawn sprite.

#include "world/rendering/AssetInstanceTransform.h"

#include <gtest/gtest.h>

#include <cmath>

using engine::world::AssetInstanceTransform;
using engine::world::dynamicInstanceTransform;
using engine::world::staticInstanceTransform;

namespace {

	void expectVecNear(glm::vec2 got, glm::vec2 want, float eps = 1e-4F) {
		EXPECT_NEAR(got.x, want.x, eps);
		EXPECT_NEAR(got.y, want.y, eps);
	}

	// The static/baked convention, computed independently (matches BakedEntityMesh.cpp).
	glm::vec2 bakedFormula(glm::vec2 local, glm::vec2 position, float rotation, float scale) {
		const float sx = local.x * scale;
		const float sy = local.y * scale;
		if (rotation == 0.0F) {
			return {sx + position.x, sy + position.y};
		}
		const float c = std::cos(rotation);
		const float s = std::sin(rotation);
		return {sx * c - sy * s + position.x, sx * s + sy * c + position.y};
	}

} // namespace

TEST(AssetInstanceTransform, StaticForwardMatchesBakedFormula) {
	const glm::vec2 position{12.0F, -7.0F};
	const float		rotation = 0.6F;
	const float		scale	 = 1.3F;
	const auto		t		 = staticInstanceTransform(position, rotation, scale);
	for (glm::vec2 local : {glm::vec2{0, 0}, glm::vec2{1, 0}, glm::vec2{-2, 3}, glm::vec2{0.5F, -0.5F}}) {
		expectVecNear(t.toWorld(local), bakedFormula(local, position, rotation, scale));
	}
}

TEST(AssetInstanceTransform, StaticRoundTrip) {
	const auto t = staticInstanceTransform({12.0F, -7.0F}, 0.6F, 1.3F);
	for (glm::vec2 local : {glm::vec2{0, 0}, glm::vec2{2, -1}, glm::vec2{-3, 4}}) {
		expectVecNear(t.toLocal(t.toWorld(local)), local);
	}
}

TEST(AssetInstanceTransform, DynamicOriginIsPosMinusBoundsCenter) {
	const glm::vec2 pos{5.0F, 9.0F};
	const glm::vec2 boundsCenter{0.5F, -1.5F};
	const auto		t = dynamicInstanceTransform(pos, 1.0F, boundsCenter);
	expectVecNear(t.origin, {pos.x - boundsCenter.x, pos.y - boundsCenter.y});
	EXPECT_EQ(t.rotation, 0.0F);
	// Matches DynamicEntityRenderSystem: world = local*scale + (pos - boundsCenter).
	expectVecNear(t.toWorld({2.0F, 3.0F}), {2.0F + pos.x - boundsCenter.x, 3.0F + pos.y - boundsCenter.y});
}

TEST(AssetInstanceTransform, DynamicNullTemplateCollapsesToPosition) {
	// A null/empty template gives boundsCenter {0,0}: origin == raw position (matches
	// the nullptr guard in DynamicEntityRenderSystem where centerOffset stays 0).
	const glm::vec2 pos{5.0F, 9.0F};
	const auto		t = dynamicInstanceTransform(pos, 1.2F, {0.0F, 0.0F});
	expectVecNear(t.origin, pos);
}

TEST(AssetInstanceTransform, DynamicRoundTrip) {
	const auto t = dynamicInstanceTransform({5.0F, 9.0F}, 1.7F, {0.5F, -1.5F});
	for (glm::vec2 local : {glm::vec2{0, 0}, glm::vec2{2, -1}, glm::vec2{-3, 4}}) {
		expectVecNear(t.toLocal(t.toWorld(local)), local);
	}
}

TEST(AssetInstanceTransform, ZeroScaleToLocalDoesNotDivideByZero) {
	const auto t = staticInstanceTransform({1.0F, 2.0F}, 0.0F, 0.0F);
	const auto l = t.toLocal({3.0F, 4.0F});
	EXPECT_EQ(l.x, 0.0F);
	EXPECT_EQ(l.y, 0.0F);
}

#include "scenes/game/world/WorldPointer.h"

#include <gtest/gtest.h>

namespace {

	using world_sim::PointerTarget;

	constexpr int kWindowWidth = 1280;
	constexpr int kWindowHeight = 720;

	// A panel 100 by 50 ui px at the ui's top-left, whatever the interface scale.
	bool onPanel(glm::vec2 uiPoint) {
		return uiPoint.x < 100.0F && uiPoint.y < 50.0F;
	}

} // namespace

// The panel covers 100 ui px, so 120 window px at 120%, 60 at 60%: a pointer 110 px in is on it
// at 120% and on the world at 100% and 60%.
TEST(WorldPointer, TheInterfaceIsHitInUiPixels) {
	EXPECT_EQ(world_sim::pointerTarget({110.0F, 10.0F}, kWindowWidth, kWindowHeight, 1.2F, onPanel), PointerTarget::Interface);
	EXPECT_EQ(world_sim::pointerTarget({110.0F, 10.0F}, kWindowWidth, kWindowHeight, 1.0F, onPanel), PointerTarget::World);
	EXPECT_EQ(world_sim::pointerTarget({70.0F, 10.0F}, kWindowWidth, kWindowHeight, 0.6F, onPanel), PointerTarget::World);
	EXPECT_EQ(world_sim::pointerTarget({50.0F, 10.0F}, kWindowWidth, kWindowHeight, 0.6F, onPanel), PointerTarget::Interface);
}

TEST(WorldPointer, PastTheWindowEdgeIsOutside) {
	for (const glm::vec2 pointer : {glm::vec2{-1.0F, 300.0F}, glm::vec2{1280.0F, 300.0F}, glm::vec2{600.0F, -0.5F}, glm::vec2{600.0F, 720.0F}}) {
		EXPECT_EQ(world_sim::pointerTarget(pointer, kWindowWidth, kWindowHeight, 1.0F, onPanel), PointerTarget::OutsideWindow);
	}
}

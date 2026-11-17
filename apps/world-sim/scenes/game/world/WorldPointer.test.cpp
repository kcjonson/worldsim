#include "scenes/game/world/WorldPointer.h"

#include "scenes/game/world/construction/DrawingSystem.h"

#include <gtest/gtest.h>

namespace {

	using foundation::MouseButton;
	using world_sim::PointerAction;
	using world_sim::PointerInput;
	using world_sim::PointerTarget;
	using world_sim::ToolKind;

	constexpr int kWindowWidth = 1280;
	constexpr int kWindowHeight = 720;

	// A panel 100 by 50 ui px at the ui's top-left, whatever the interface scale.
	bool onPanel(glm::vec2 uiPoint) {
		return uiPoint.x < 100.0F && uiPoint.y < 50.0F;
	}

	PointerAction press(MouseButton button, PointerTarget target, std::optional<ToolKind> tool) {
		return world_sim::drawingPointerAction(PointerInput::Press, button, target, tool, false);
	}

	PointerAction release(MouseButton button, PointerTarget target, std::optional<ToolKind> tool) {
		return world_sim::drawingPointerAction(PointerInput::Release, button, target, tool, false);
	}

} // namespace

// The panel covers 100 ui px, so 120 window px at 120%, 60 at 60%: a pointer 110 px in is on it
// at 120% and on the world at 100% and 60%, so the Rectangle tool's press there reaches it only
// at 100% and 60%.
TEST(WorldPointer, TheInterfaceIsHitInUiPixels) {
	EXPECT_EQ(world_sim::pointerTarget({110.0F, 10.0F}, kWindowWidth, kWindowHeight, 1.2F, onPanel), PointerTarget::Interface);
	EXPECT_EQ(world_sim::pointerTarget({110.0F, 10.0F}, kWindowWidth, kWindowHeight, 1.0F, onPanel), PointerTarget::World);
	EXPECT_EQ(world_sim::pointerTarget({70.0F, 10.0F}, kWindowWidth, kWindowHeight, 0.6F, onPanel), PointerTarget::World);
	EXPECT_EQ(world_sim::pointerTarget({50.0F, 10.0F}, kWindowWidth, kWindowHeight, 0.6F, onPanel), PointerTarget::Interface);
	for (const float uiScale : {0.6F, 1.0F, 1.2F}) {
		const PointerTarget target = world_sim::pointerTarget({110.0F, 10.0F}, kWindowWidth, kWindowHeight, uiScale, onPanel);
		EXPECT_EQ(
			press(MouseButton::Left, target, ToolKind::FoundationRectangle),
			uiScale > 1.1F ? PointerAction::Ignore : PointerAction::DeliverPress
		) << uiScale;
	}
}

TEST(WorldPointer, PastTheWindowEdgeIsOutside) {
	for (const glm::vec2 pointer :
		 {glm::vec2{-1.0F, 300.0F}, glm::vec2{1280.0F, 300.0F}, glm::vec2{600.0F, -0.5F}, glm::vec2{600.0F, 720.0F}}) {
		EXPECT_EQ(world_sim::pointerTarget(pointer, kWindowWidth, kWindowHeight, 1.0F, onPanel), PointerTarget::OutsideWindow);
	}
}

// Only the Rectangle tool takes a press, and only a left one on the world.
TEST(WorldPointer, OnlyTheRectangleToolsLeftPressOnTheWorldIsDelivered) {
	EXPECT_EQ(press(MouseButton::Left, PointerTarget::World, ToolKind::FoundationRectangle), PointerAction::DeliverPress);
	EXPECT_EQ(press(MouseButton::Left, PointerTarget::Interface, ToolKind::FoundationRectangle), PointerAction::Ignore);
	EXPECT_EQ(press(MouseButton::Left, PointerTarget::OutsideWindow, ToolKind::FoundationRectangle), PointerAction::Ignore);
	EXPECT_EQ(press(MouseButton::Right, PointerTarget::World, ToolKind::FoundationRectangle), PointerAction::Ignore);
	EXPECT_EQ(press(MouseButton::Middle, PointerTarget::World, ToolKind::FoundationRectangle), PointerAction::Ignore);
	for (const ToolKind tool : {ToolKind::Foundation, ToolKind::FoundationEdit, ToolKind::Wall, ToolKind::Opening}) {
		EXPECT_EQ(press(MouseButton::Left, PointerTarget::World, tool), PointerAction::Ignore) << static_cast<int>(tool);
	}
	EXPECT_EQ(press(MouseButton::Left, PointerTarget::World, std::nullopt), PointerAction::Ignore);
}

// A left release is every drawing tool's click, except the Rectangle tool's off the world, which
// cancels its press; a right release is Esc; with no drawing tool on, placement or selection takes it.
TEST(WorldPointer, ReleasesGoToTheToolOnOrPassOn) {
	EXPECT_EQ(release(MouseButton::Left, PointerTarget::World, ToolKind::FoundationRectangle), PointerAction::DeliverClick);
	EXPECT_EQ(release(MouseButton::Left, PointerTarget::Interface, ToolKind::FoundationRectangle), PointerAction::CancelPress);
	EXPECT_EQ(release(MouseButton::Left, PointerTarget::OutsideWindow, ToolKind::FoundationRectangle), PointerAction::CancelPress);
	for (const ToolKind tool : {ToolKind::Foundation, ToolKind::FoundationEdit, ToolKind::Wall, ToolKind::Opening}) {
		EXPECT_EQ(release(MouseButton::Left, PointerTarget::World, tool), PointerAction::DeliverClick) << static_cast<int>(tool);
		EXPECT_EQ(release(MouseButton::Right, PointerTarget::World, tool), PointerAction::CancelTool) << static_cast<int>(tool);
		EXPECT_EQ(release(MouseButton::Middle, PointerTarget::World, tool), PointerAction::Ignore) << static_cast<int>(tool);
	}
	EXPECT_EQ(release(MouseButton::Right, PointerTarget::World, ToolKind::FoundationRectangle), PointerAction::CancelTool);
	EXPECT_EQ(release(MouseButton::Left, PointerTarget::World, std::nullopt), PointerAction::PassOn);
	EXPECT_EQ(release(MouseButton::Right, PointerTarget::World, std::nullopt), PointerAction::PassOn);
}

// A frame with the left button up while the Rectangle tool's press waits: its release went to the
// interface or was lost with the focus, so the press is cancelled.
TEST(WorldPointer, AFrameWithTheButtonUpCancelsAPendingPress) {
	EXPECT_EQ(
		world_sim::drawingPointerAction(
			PointerInput::LeftButtonUpThisFrame, MouseButton::Left, PointerTarget::Interface, ToolKind::FoundationRectangle, true
		),
		PointerAction::CancelPress
	);
	EXPECT_EQ(
		world_sim::drawingPointerAction(
			PointerInput::LeftButtonUpThisFrame, MouseButton::Left, PointerTarget::World, ToolKind::FoundationRectangle, false
		),
		PointerAction::Ignore
	);
}

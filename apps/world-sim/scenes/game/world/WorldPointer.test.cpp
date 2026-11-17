#include "scenes/game/world/WorldPointer.h"

#include "scenes/game/world/construction/DrawingSystem.h"

#include <gtest/gtest.h>

#include <optional>
#include <vector>

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
		return world_sim::drawingPointerAction(PointerInput::Press, button, target, true, tool, false);
	}

	PointerAction release(MouseButton button, PointerTarget target, std::optional<ToolKind> tool) {
		return world_sim::drawingPointerAction(PointerInput::Release, button, target, true, tool, false);
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

// A left release on the world is every drawing tool's click; the Rectangle tool's off the world
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
			PointerInput::LeftButtonUpThisFrame, MouseButton::Left, PointerTarget::Interface, true, ToolKind::FoundationRectangle, true
		),
		PointerAction::CancelPress
	);
	EXPECT_EQ(
		world_sim::drawingPointerAction(
			PointerInput::LeftButtonUpThisFrame, MouseButton::Left, PointerTarget::World, true, ToolKind::FoundationRectangle, false
		),
		PointerAction::Ignore
	);
}

// In the window and focused, the tool follows the pointer, over the interface too (where update()
// moves nothing). Out of the window, by the window system's word or past its edge (an injected move
// keeps the flag), it does not. Unfocused it does not, unless the launch was --background.
TEST(WorldPointer, TheToolFollowsThePointerInTheFocusedWindow) {
	for (const PointerTarget target : {PointerTarget::World, PointerTarget::Interface}) {
		for (const bool background : {false, true}) {
			EXPECT_TRUE(world_sim::pointerWithTool(target, true, true, background));
			EXPECT_EQ(world_sim::pointerWithTool(target, true, false, background), background);
		}
	}
	for (const PointerTarget target : {PointerTarget::World, PointerTarget::Interface, PointerTarget::OutsideWindow}) {
		for (const bool focused : {false, true}) {
			for (const bool background : {false, true}) {
				EXPECT_FALSE(world_sim::pointerWithTool(target, false, focused, background));
				EXPECT_FALSE(world_sim::pointerWithTool(PointerTarget::OutsideWindow, true, focused, background));
			}
		}
	}
}

// A left release places a point only on the world with the pointer with the tool: off the world, or
// away from the tool, the Rectangle tool's press is cancelled and every other tool ignores it. A press
// away from the tool is ignored; a right release still ends the tool's shape.
TEST(WorldPointer, AReleaseOffTheWorldOrAwayFromTheToolPlacesNothing) {
	const auto action = [](PointerInput input, MouseButton button, PointerTarget target, bool withTool, ToolKind tool) {
		return world_sim::drawingPointerAction(input, button, target, withTool, tool, false);
	};
	for (const ToolKind tool : {ToolKind::Foundation, ToolKind::FoundationEdit, ToolKind::Wall, ToolKind::Opening}) {
		EXPECT_EQ(action(PointerInput::Release, MouseButton::Left, PointerTarget::Interface, true, tool), PointerAction::Ignore);
		EXPECT_EQ(action(PointerInput::Release, MouseButton::Left, PointerTarget::OutsideWindow, true, tool), PointerAction::Ignore);
		EXPECT_EQ(action(PointerInput::Release, MouseButton::Left, PointerTarget::World, false, tool), PointerAction::Ignore);
		EXPECT_EQ(action(PointerInput::Release, MouseButton::Right, PointerTarget::World, false, tool), PointerAction::CancelTool);
	}
	EXPECT_EQ(
		action(PointerInput::Release, MouseButton::Left, PointerTarget::World, false, ToolKind::FoundationRectangle), PointerAction::CancelPress
	);
	EXPECT_EQ(action(PointerInput::Press, MouseButton::Left, PointerTarget::World, false, ToolKind::FoundationRectangle), PointerAction::Ignore);
}

namespace {

	constexpr double kLimitSeconds = 0.375; // exact in binary, so a release at the limit lands on it
	constexpr float	 kLimitPixels = 4.0F;

	bool doubleClick(std::optional<world_sim::PointerRelease> previous, world_sim::PointerRelease release) {
		return world_sim::secondClickOfDoubleClick(previous, release, kLimitSeconds, kLimitPixels);
	}

	const world_sim::PointerRelease kFirstClick{.timeSeconds = 2.0, .position = {100.0F, 100.0F}};

} // namespace

TEST(WorldPointer, TwoReleasesWithinBothLimitsAreADoubleClick) {
	EXPECT_TRUE(doubleClick(kFirstClick, {.timeSeconds = 2.25, .position = {102.0F, 101.0F}}));
}

TEST(WorldPointer, ASecondReleaseTooLateIsAClick) {
	EXPECT_FALSE(doubleClick(kFirstClick, {.timeSeconds = 2.5, .position = {100.0F, 100.0F}}));
}

TEST(WorldPointer, ASecondReleaseTooFarIsAClick) {
	EXPECT_FALSE(doubleClick(kFirstClick, {.timeSeconds = 2.125, .position = {100.0F, 104.5F}}));
}

// "Within" both limits: a release exactly at either one is still the second click.
TEST(WorldPointer, ADoubleClickIncludesBothLimits) {
	EXPECT_TRUE(doubleClick(kFirstClick, {.timeSeconds = 2.375, .position = {100.0F, 100.0F}}));
	EXPECT_TRUE(doubleClick(kFirstClick, {.timeSeconds = 2.125, .position = {100.0F, 104.0F}}));
	EXPECT_TRUE(doubleClick(kFirstClick, {.timeSeconds = 2.375, .position = {104.0F, 100.0F}}));
}

// The pair is consumed: GameScene forgets the first click once the second is seen, so a third
// release as quick and as close is a first click again, and a fourth completes the next pair.
TEST(WorldPointer, AThirdReleaseStartsOver) {
	std::optional<world_sim::PointerRelease> previous;
	std::vector<bool>						 seconds;
	for (const double time : {2.0, 2.125, 2.25, 2.375}) {
		const world_sim::PointerRelease release{.timeSeconds = time, .position = {100.0F, 100.0F}};
		const bool						second = doubleClick(previous, release);
		seconds.push_back(second);
		previous = second ? std::nullopt : std::optional(release);
	}
	EXPECT_EQ(seconds, (std::vector<bool>{false, true, false, true}));
}

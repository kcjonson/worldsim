#include "scenes/game/world/WorldPointer.h"

#include "scenes/game/world/construction/DrawingSystem.h"

#include <gtest/gtest.h>

#include <initializer_list>
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


// With no drawing tool on, a release goes to placement or selection wherever the pointer is.
TEST(WorldPointer, WithNoDrawingToolAReleaseAwayFromTheToolPassesOn) {
	for (const PointerTarget target : {PointerTarget::World, PointerTarget::Interface, PointerTarget::OutsideWindow}) {
		for (const MouseButton button : {MouseButton::Left, MouseButton::Right}) {
			EXPECT_EQ(
				world_sim::drawingPointerAction(PointerInput::Release, button, target, false, std::nullopt, false), PointerAction::PassOn
			);
		}
	}
}

namespace {

	using Event = foundation::InputEvent::Type;

	// The wall tool's action for each left release on the world in `events`, judged as GameScene
	// judges it: by the window as the events before it left it.
	std::vector<PointerAction> releasesAfter(world_sim::WindowPresence presence, std::initializer_list<Event> events) {
		std::vector<PointerAction> actions;
		for (const Event event : events) {
			presence.observe(event);
			if (event == Event::PointerUp) {
				const bool withTool =
					world_sim::pointerWithTool(PointerTarget::World, presence.pointerInWindow, presence.windowFocused, false);
				actions.push_back(world_sim::drawingPointerAction(
					PointerInput::Release, MouseButton::Left, PointerTarget::World, withTool, ToolKind::Wall, false
				));
			}
		}
		return actions;
	}

} // namespace

// A release the window saw before the pointer left in the same frame is a click; one after the
// leave is not. An injected click leads with an enter, so the first one after a real leave lands.
TEST(WorldPointer, AReleaseIsJudgedByTheEventsBeforeIt) {
	const world_sim::WindowPresence inWindow{.pointerInWindow = true};
	const std::vector				clicked{PointerAction::DeliverClick};
	EXPECT_EQ(releasesAfter(inWindow, {Event::PointerDown, Event::PointerUp, Event::PointerLeave}), clicked);
	EXPECT_EQ(releasesAfter(inWindow, {Event::PointerDown, Event::PointerLeave, Event::PointerUp}), std::vector{PointerAction::Ignore});
	EXPECT_EQ(
		releasesAfter(inWindow, {Event::PointerLeave, Event::PointerEnter, Event::PointerMove, Event::PointerDown, Event::PointerUp}), clicked
	);
	EXPECT_EQ(releasesAfter({}, {Event::PointerEnter, Event::PointerMove, Event::PointerDown, Event::PointerUp}), clicked);
}

// The focus follows FocusLost and FocusGained, and a key, a character, or a button pressed proves
// it, so a FocusGained lost while the app was paused does not leave the tools paused.
TEST(WorldPointer, InputThatReachesOnlyAFocusedWindowProvesTheFocus) {
	world_sim::WindowPresence presence;
	EXPECT_TRUE(presence.windowFocused);
	presence.observe(Event::FocusLost);
	EXPECT_FALSE(presence.windowFocused);
	presence.observe(Event::FocusGained);
	EXPECT_TRUE(presence.windowFocused);
	for (const Event proof : {Event::KeyDown, Event::Char, Event::PointerDown}) {
		presence.observe(Event::FocusLost);
		for (const Event other : {Event::KeyUp, Event::PointerMove, Event::PointerUp, Event::Scroll, Event::PointerEnter}) {
			presence.observe(other);
		}
		ASSERT_FALSE(presence.windowFocused) << static_cast<int>(proof);
		presence.observe(proof);
		EXPECT_TRUE(presence.windowFocused) << static_cast<int>(proof);
	}
}

namespace {

	// Exact in binary, so a release at the limit lands on it and the inclusive comparison cannot flake.
	constexpr world_sim::DoubleClickLimits kLimits{.seconds = 0.375, .pixels = 4.0F};

	bool doubleClick(std::optional<world_sim::PointerRelease> previous, world_sim::PointerRelease release) {
		return world_sim::secondClickOfDoubleClick(previous, release, kLimits);
	}

	const world_sim::PointerRelease kFirstClick{.timeSeconds = 2.0, .position = {100.0F, 100.0F}};

	world_sim::PointerRelease releaseAt(double timeSeconds) {
		return {.timeSeconds = timeSeconds, .position = kFirstClick.position};
	}

} // namespace

TEST(WorldPointer, TwoReleasesWithinBothLimitsAreADoubleClick) {
	EXPECT_TRUE(doubleClick(kFirstClick, {.timeSeconds = 2.25, .position = {102.0F, 101.0F}}));
}

TEST(WorldPointer, ASecondReleaseTooLateIsAClick) {
	EXPECT_FALSE(doubleClick(kFirstClick, {.timeSeconds = 2.5, .position = {100.0F, 100.0F}}));
	EXPECT_FALSE(doubleClick(kFirstClick, {.timeSeconds = 2.375 + (1.0 / 1024.0), .position = {100.0F, 100.0F}}));
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

// The distance is the straight line, as the Rectangle tool's click-or-drag test measures it: 2 px
// across and 3 px down is 3.6 px, inside 4; 3 px each way is 4.2 px, outside, though neither
// offset alone reaches 4.
TEST(WorldPointer, TheDistanceIsTheStraightLine) {
	EXPECT_TRUE(doubleClick(kFirstClick, {.timeSeconds = 2.125, .position = {102.0F, 103.0F}}));
	EXPECT_FALSE(doubleClick(kFirstClick, {.timeSeconds = 2.125, .position = {103.0F, 103.0F}}));
}

// The pair is consumed, so a third release as quick and as close is a first click again, and a
// fourth completes the next pair.
TEST(WorldPointer, AThirdReleaseStartsOver) {
	world_sim::WallDoubleClick		   record;
	std::vector<world_sim::ReleaseAction> actions;
	for (const double time : {2.0, 2.125, 2.25, 2.375}) {
		actions.push_back(record.release(releaseAt(time), ToolKind::Wall, kLimits));
	}
	using world_sim::ReleaseAction;
	EXPECT_EQ(actions, (std::vector{ReleaseAction::Click, ReleaseAction::EndChain, ReleaseAction::Click, ReleaseAction::EndChain}));
}

// Once forgotten, the release that would have been the second click is a first click.
TEST(WorldPointer, AForgottenReleaseStartsNoPair) {
	world_sim::WallDoubleClick record;
	ASSERT_EQ(record.release(releaseAt(2.0), ToolKind::Wall, kLimits), world_sim::ReleaseAction::Click);
	record.forget();
	EXPECT_EQ(record.release(releaseAt(2.125), ToolKind::Wall, kLimits), world_sim::ReleaseAction::Click);
	EXPECT_EQ(record.release(releaseAt(2.25), ToolKind::Wall, kLimits), world_sim::ReleaseAction::EndChain);
}

// Only the wall tool has a double-click: under any other tool every release is a click, and one
// between two wall releases breaks their pair.
TEST(WorldPointer, OnlyTheWallToolsReleasesMakeADoubleClick) {
	for (const ToolKind tool : {ToolKind::Foundation, ToolKind::FoundationRectangle, ToolKind::FoundationEdit, ToolKind::Opening}) {
		world_sim::WallDoubleClick record;
		EXPECT_EQ(record.release(releaseAt(2.0), tool, kLimits), world_sim::ReleaseAction::Click) << static_cast<int>(tool);
		EXPECT_EQ(record.release(releaseAt(2.125), tool, kLimits), world_sim::ReleaseAction::Click) << static_cast<int>(tool);
		ASSERT_EQ(record.release(releaseAt(2.25), ToolKind::Wall, kLimits), world_sim::ReleaseAction::Click);
		EXPECT_EQ(record.release(releaseAt(2.3125), tool, kLimits), world_sim::ReleaseAction::Click) << static_cast<int>(tool);
		EXPECT_EQ(record.release(releaseAt(2.375), ToolKind::Wall, kLimits), world_sim::ReleaseAction::Click) << static_cast<int>(tool);
	}
}

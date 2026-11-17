#pragma once

// Where the pointer is for the game scene, on the world, on the interface, or outside the
// window, and what the scene does with a press or release for the drawing tools. The pointer
// and the world are in the window's logical pixels; the interface lays out in ui px, each
// `uiScale` logical pixels (the Interface size), so its hit test takes the pointer divided by
// the scale. The one place that division happens for world input.

#include <input/InputTypes.h>

#include <glm/vec2.hpp>

#include <functional>
#include <optional>

namespace world_sim {

	enum class ToolKind;

	enum class PointerTarget {
		World,
		Interface,
		OutsideWindow,
	};

	/// `windowPointer` in logical pixels, the window `windowWidth` by `windowHeight` of them;
	/// `interfaceHit` answers whether a point in ui px lies on the interface.
	[[nodiscard]] PointerTarget pointerTarget(
		glm::vec2									  windowPointer,
		int											  windowWidth,
		int											  windowHeight,
		float										  uiScale,
		const std::function<bool(glm::vec2 uiPoint)>& interfaceHit
	);

	/// The window as the game scene's events tell it, one event at a time, so a release is judged by
	/// the events before it and not by the frame's end: whether the pointer is in the window
	/// (PointerEnter, PointerLeave) and whether the window has the focus (FocusGained, FocusLost).
	/// No event is dispatched while the app is paused, so a FocusGained can be lost; a key pressed, a
	/// character typed, or a button pressed reaches only a focused window, so each proves the focus.
	struct WindowPresence {
		bool pointerInWindow = false;
		bool windowFocused = true; // no event comes when the window opens

		void observe(foundation::InputEvent::Type type);
	};

	/// Whether the drawing tools follow the pointer: it is in the window (`pointerInWindow`, the
	/// window system's word, and `target` inside the window's bounds) and the window has the focus.
	/// A background launch's window opens unfocused and is driven by injected input, so there the
	/// focus does not count.
	[[nodiscard]] bool pointerWithTool(PointerTarget target, bool pointerInWindow, bool windowFocused, bool backgroundLaunch);

	/// What reaches the game scene: a button pressed or released over the window (handleEvent),
	/// or a frame with the left button up (update(), which also sees a release the interface
	/// took and a button let go while the window had no focus).
	enum class PointerInput {
		Press,
		Release,
		LeftButtonUpThisFrame,
	};

	/// What the game scene does with it.
	enum class PointerAction {
		DeliverPress, // DrawingSystem::handlePress
		DeliverClick, // DrawingSystem::handleClick
		CancelPress,  // DrawingSystem::cancelPress
		CancelTool,	  // DrawingSystem::cancel
		Ignore,		  // nothing takes it
		PassOn,		  // no drawing tool is on: placement or selection takes the release
	};

	/// The drawing tools' routing. The Rectangle tool alone takes a press, a left one on the
	/// world; a left release off the world, or a frame with the left button up while a press
	/// waits, cancels its press. Any drawing tool takes a left release on the world as a click,
	/// and a right release as Esc. A press or a left release while the pointer is not with the
	/// tool (pointerWithTool) places nothing. `activeTool` is the drawing tool on, if any.
	[[nodiscard]] PointerAction drawingPointerAction(
		PointerInput			input,
		foundation::MouseButton button,
		PointerTarget			target,
		bool					withTool,
		std::optional<ToolKind> activeTool,
		bool					pressPending
	);

	/// A left release, by the app's clock (seconds) and where it was (logical pixels).
	struct PointerRelease {
		double	  timeSeconds = 0.0;
		glm::vec2 position{};
	};

	/// How close two left releases are to make a double-click (snapping.xml): in time, and in
	/// straight-line distance in logical pixels, each limit inclusive.
	struct DoubleClickLimits {
		double seconds = 0.0;
		float  pixels = 0.0F;
	};

	/// Whether `release` is the second click of a double-click: within `limits` of `previous`.
	/// With no previous release, it is not.
	[[nodiscard]] bool secondClickOfDoubleClick(
		const std::optional<PointerRelease>& previous, const PointerRelease& release, const DoubleClickLimits& limits
	);

	/// What a left release on the world does with a drawing tool on (WallDoubleClick::release).
	enum class ReleaseAction {
		Click,	  // DrawingSystem::handleClick
		EndChain, // the second click of a wall double-click: it places nothing, DrawingSystem::finishChain
	};

	/// The wall tool's double-click: the last left release it was handed, the first click of a
	/// possible pair. GameScene keeps one and forgets it wherever the chain ends or the pointer's
	/// continuity breaks (Backspace, Enter, Esc, a right-click, a tool starting or ending, a pause).
	class WallDoubleClick {
	  public:
		/// A left release on the world with `tool` on. With the wall tool, the second click of a
		/// double-click ends the chain, as Enter does, and consumes the pair, so a third release is a
		/// first click again; any other release is a click, remembered as the first of a pair. Under
		/// any other tool a release is a click and remembers nothing.
		[[nodiscard]] ReleaseAction release(const PointerRelease& release, ToolKind tool, const DoubleClickLimits& limits);

		/// The next release is a first click.
		void forget() { lastRelease.reset(); }

	  private:
		std::optional<PointerRelease> lastRelease;
	};

} // namespace world_sim

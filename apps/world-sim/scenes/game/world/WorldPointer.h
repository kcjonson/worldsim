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
	/// waits, cancels its press. Any drawing tool takes a left release as a click and a right
	/// release as Esc. `activeTool` is the drawing tool on, if any.
	[[nodiscard]] PointerAction drawingPointerAction(
		PointerInput			input,
		foundation::MouseButton button,
		PointerTarget			target,
		std::optional<ToolKind> activeTool,
		bool					pressPending
	);

} // namespace world_sim

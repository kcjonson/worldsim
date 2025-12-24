#pragma once

namespace UI {
	struct InputEvent;
}

namespace engine {

/// @brief Interface for application-level overlays
///
/// Overlays are cross-scene UI elements that persist across scene transitions.
/// They render on top of scenes and receive input events before scenes.
///
/// Use cases:
/// - Navigation menus (scene switcher)
/// - ESC menu (quit, save, settings)
/// - Future: resilient UI that stays responsive if game hangs
///
/// NOT for scene-specific UI (panels, modals within a scene).
struct IOverlay {
	virtual ~IOverlay() = default;

	/// @brief Handle input event
	/// @param event The input event to handle
	/// @return true if event was consumed (prevents scene from receiving it)
	virtual bool handleEvent(UI::InputEvent& event) = 0;

	/// @brief Update per frame
	/// @param dt Delta time in seconds
	virtual void update(float dt) = 0;

	/// @brief Render the overlay (called after scene render)
	virtual void render() = 0;

	/// @brief Window resize notification (optional)
	/// Override to reposition overlay elements when window size changes
	virtual void onWindowResize() {}
};

} // namespace engine

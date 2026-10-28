#pragma once

#include "CoordinateSystem/CoordinateSystem.h"
#include "components/button/Button.h"
#include "input/InputEvent.h"
#include "math/Types.h"
#include "shapes/Shapes.h"

#include <app/UiOverlay.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

// NavigationMenu - Cross-Scene Overlay
//
// Collapsible scene navigation menu for the UI sandbox.
// Displays as a small toggle button in the bottom-right corner.
// Expands upward to show a list of available scenes when clicked.
//
// An overlay the App owns (engine::UiOverlay):
// - Receives the bridge's input events before scenes
// - Persists across scene transitions
// - Renders on top of scenes

namespace UI {

	struct NavigationMenu : public engine::UiOverlay {
		// Constructor arguments struct (C++20 designated initializers)
		struct Args {
			std::vector<std::string>			  sceneNames;
			std::function<void(const std::string&)> onSceneSelected;
			Renderer::CoordinateSystem*			  coordinateSystem = nullptr;
		};

		// --- Public Methods ---

		explicit NavigationMenu(const Args& args);
		~NavigationMenu() override;

		// The buttons' callbacks hold this, so it stays where it was made
		NavigationMenu(const NavigationMenu&) = delete;
		NavigationMenu& operator=(const NavigationMenu&) = delete;
		NavigationMenu(NavigationMenu&&) = delete;
		NavigationMenu& operator=(NavigationMenu&&) = delete;

		bool handleInput(InputEvent& event) override;
		void update(const app::FrameContext& frame) override;
		void render() override;
		void onResize(const Renderer::Viewport& viewport) override;

	  private:
		// Layout constants
		static constexpr float kToggleSize = 30.0F;
		static constexpr float kMargin = 10.0F;
		static constexpr float kMenuWidth = 150.0F;
		static constexpr float kLineHeight = 25.0F;
		static constexpr float kHeaderHeight = 30.0F;
		static constexpr float kMenuToggleGap = 5.0F;

		// State
		bool								expanded = false;
		std::vector<std::string>			sceneNames;
		std::function<void(const std::string&)> onSceneSelected;
		Renderer::CoordinateSystem*			coordinateSystem = nullptr;

		// Sub-components
		std::optional<Button>		 toggleButton;
		std::vector<Button>			 menuButtons;
		std::vector<std::string>	 buttonIds; // Store button IDs to avoid dangling pointers
		Text						 headerText;

		// Cached layout values (updated on resize)
		float menuX = 0.0F;
		float menuY = 0.0F;
		float menuHeight = 0.0F;

		// Initialize/reinitialize all sub-components with current window size
		void initializeComponents();
	};

} // namespace UI

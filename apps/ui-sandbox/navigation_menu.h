#pragma once

#include "components/button/button.h"
#include "coordinate_system/coordinate_system.h"
#include "math/types.h"
#include "shapes/shapes.h"
#include <functional>
#include <optional>
#include <string>
#include <vector>

// NavigationMenu Component
//
// Collapsible scene navigation menu for the UI sandbox.
// Displays as a small toggle button in the bottom-right corner.
// Expands upward to show a list of available scenes when clicked.
// Uses standard lifecycle: HandleInput() → Update() → Render()

namespace UI {

	struct NavigationMenu {
		// Constructor arguments struct (C++20 designated initializers)
		struct Args {
			std::vector<std::string>			  sceneNames;
			std::function<void(const std::string&)> onSceneSelected;
			Renderer::CoordinateSystem*			  coordinateSystem = nullptr;
		};

		// --- Public Methods ---

		explicit NavigationMenu(const Args& args);
		~NavigationMenu();

		// Disable copy
		NavigationMenu(const NavigationMenu&) = delete;
		NavigationMenu& operator=(const NavigationMenu&) = delete;

		// Allow move
		NavigationMenu(NavigationMenu&& other) noexcept;
		NavigationMenu& operator=(NavigationMenu&& other) noexcept;

		// Standard lifecycle methods
		void HandleInput();
		void Update(float deltaTime);
		void Render();

		// Window resize handling - recalculates all positions
		void OnWindowResize();

	  private:
		// Layout constants
		static constexpr float kToggleSize = 30.0F;
		static constexpr float kMargin = 10.0F;
		static constexpr float kMenuWidth = 150.0F;
		static constexpr float kLineHeight = 25.0F;
		static constexpr float kHeaderHeight = 30.0F;
		static constexpr float kMenuToggleGap = 5.0F;

		// State
		bool								m_expanded = false;
		std::vector<std::string>			m_sceneNames;
		std::function<void(const std::string&)> m_onSceneSelected;
		Renderer::CoordinateSystem*			m_coordinateSystem = nullptr;

		// Sub-components
		std::optional<Button>		 m_toggleButton;
		std::vector<Button>			 m_menuButtons;
		std::vector<std::string>	 m_buttonIds; // Store button IDs to avoid dangling pointers
		Text						 m_headerText;

		// Cached layout values (updated on resize)
		float m_menuX = 0.0F;
		float m_menuY = 0.0F;
		float m_menuHeight = 0.0F;

		// Initialize/reinitialize all sub-components with current window size
		void InitializeComponents();
	};

} // namespace UI

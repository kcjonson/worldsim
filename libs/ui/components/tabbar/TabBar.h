#pragma once

#include "components/tabbar/TabBarStyle.h"
#include "component/Component.h"
#include "focus/Focusable.h"
#include "math/Types.h"
#include "shapes/Shapes.h"
#include <functional>
#include <string>
#include <vector>

// TabBar Component
//
// Horizontal tab bar for switching between content panels.
// Supports 5 visual states: Normal, Hover, Active (selected), Disabled, Focused
// Extends Component (for child management) and IFocusable (for keyboard navigation).
//
// Usage:
//   TabBar tabBar({
//       .position = {50.0F, 50.0F},
//       .width = 300.0F,
//       .tabs = {{"status", "Status"}, {"inventory", "Inventory"}},
//       .selectedId = "status",
//       .onSelect = [](const std::string& id) { /* handle tab change */ }
//   });
//
// See: /docs/technical/ui-framework/architecture.md

namespace UI {

	// Forward declarations
	class FocusManager;

	// TabBar component - extends Component and implements IFocusable
	class TabBar : public Component, public IFocusable {
	  public:
		// Individual tab definition
		struct Tab {
			std::string id;			  // Unique identifier for this tab
			std::string label;		  // Display text
			bool		disabled = false;
		};

		// Constructor arguments struct (C++20 designated initializers)
		struct Args {
			Foundation::Vec2					   position{0.0F, 0.0F};
			float								   width = 200.0F;
			std::vector<Tab>					   tabs;
			std::string							   selectedId;	// Initially selected tab ID
			std::function<void(const std::string&)> onSelect = nullptr;
			TabBarAppearance					   appearance = TabBarStyles::defaultStyle();
			const char*							   id = nullptr;
			int									   tabIndex = -1;  // Tab order (-1 for auto-assign)
		};

		// --- Public Members ---

		// Geometry
		Foundation::Vec2 position{0.0F, 0.0F};
		float			 width{200.0F};

		// Properties
		bool		visible{true};
		const char* id = nullptr;

		// --- Public Methods ---

		// Constructor & Destructor
		explicit TabBar(const Args& args);
		~TabBar() override;

		// Disable copy (TabBar owns arena memory and registers with FocusManager)
		TabBar(const TabBar&) = delete;
		TabBar& operator=(const TabBar&) = delete;

		// Allow move
		TabBar(TabBar&& other) noexcept;
		TabBar& operator=(TabBar&& other) noexcept;

		// ILayer implementation (overrides Component)
		void handleInput() override;
		void update(float deltaTime) override;
		void render() override;

		// IFocusable implementation
		void onFocusGained() override;
		void onFocusLost() override;
		void handleKeyInput(engine::Key key, bool shift, bool ctrl, bool alt) override;
		void handleCharInput(char32_t codepoint) override;
		bool canReceiveFocus() const override;

		// Tab API
		void					   setSelected(const std::string& tabId);
		[[nodiscard]] const std::string& getSelected() const { return m_selectedId; }

		// Query methods
		[[nodiscard]] float getHeight() const { return m_height; }
		[[nodiscard]] size_t getTabCount() const { return m_tabs.size(); }

	  private:
		// Tab visual state (for rendering)
		enum class TabState { Normal, Hover, Active, Disabled };

		// Tab data
		std::vector<Tab> m_tabs;
		std::string		 m_selectedId;
		int				 m_selectedIndex = 0;  // Index of selected tab
		int				 m_hoveredIndex = -1;  // Index of hovered tab (-1 = none)
		bool			 m_focused = false;

		// Appearance
		TabBarAppearance m_appearance;

		// Callback
		std::function<void(const std::string&)> m_onSelect;

		// Computed geometry
		float			  m_height{0.0F};
		std::vector<float> m_tabWidths;	 // Width of each tab
		std::vector<float> m_tabOffsets; // X offset of each tab from bar start

		// Focus management
		int m_tabIndex{-1};

		// Internal state
		bool m_mouseDown{false};

		// Get style for a specific tab based on its state
		[[nodiscard]] const TabStyle& getTabStyle(int tabIndex) const;

		// Get the visual state of a specific tab
		[[nodiscard]] TabState getTabState(int tabIndex) const;

		// Hit test - returns tab index at position, or -1 if none
		[[nodiscard]] int getTabIndexAtPosition(Foundation::Vec2 pos) const;

		// Get bounds of a specific tab
		[[nodiscard]] Foundation::Rect getTabBounds(int tabIndex) const;

		// Recompute tab widths and offsets based on current labels
		void recomputeLayout();

		// Find index of tab by ID (-1 if not found)
		[[nodiscard]] int findTabIndex(const std::string& tabId) const;

		// Select tab by index (validates and fires callback)
		void selectTabByIndex(int index);
	};

}  // namespace UI

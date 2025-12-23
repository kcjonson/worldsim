#include "components/tabbar/TabBar.h"
#include "focus/FocusManager.h"
#include "font/FontRenderer.h"
#include <glm/glm.hpp>
#include <input/InputTypes.h>
#include <primitives/Primitives.h>

namespace UI {

	TabBar::TabBar(const Args& args)
		: position(args.position),
		  width(args.width),
		  id(args.id),
		  m_tabs(args.tabs),
		  m_selectedId(args.selectedId),
		  m_appearance(args.appearance),
		  m_onSelect(args.onSelect),
		  m_tabIndex(args.tabIndex) {

		// Find initial selected index
		m_selectedIndex = findTabIndex(m_selectedId);
		if (m_selectedIndex < 0) {
			// Provided selectedId not found (or empty), try to find first non-disabled tab
			if (!m_tabs.empty()) {
				for (size_t i = 0; i < m_tabs.size(); ++i) {
					if (!m_tabs[i].disabled) {
						m_selectedIndex = static_cast<int>(i);
						m_selectedId = m_tabs[i].id;
						break;
					}
				}
			}

			// If still no valid selection (all tabs disabled or empty), clear the ID
			if (m_selectedIndex < 0) {
				m_selectedId.clear();
			}
		}

		// Compute layout
		recomputeLayout();

		// Register with global FocusManager singleton
		FocusManager::Get().registerFocusable(this, m_tabIndex);
	}

	TabBar::~TabBar() {
		// Unregister from FocusManager
		FocusManager::Get().unregisterFocusable(this);
	}

	TabBar::TabBar(TabBar&& other) noexcept
		: Component(std::move(other)),
		  position(other.position),
		  width(other.width),
		  id(other.id),
		  m_tabs(std::move(other.m_tabs)),
		  m_selectedId(std::move(other.m_selectedId)),
		  m_selectedIndex(other.m_selectedIndex),
		  m_hoveredIndex(other.m_hoveredIndex),
		  m_focused(other.m_focused),
		  m_appearance(other.m_appearance),
		  m_onSelect(std::move(other.m_onSelect)),
		  m_height(other.m_height),
		  m_tabWidths(std::move(other.m_tabWidths)),
		  m_tabOffsets(std::move(other.m_tabOffsets)),
		  m_tabIndex(other.m_tabIndex),
		  m_mouseDown(other.m_mouseDown) {
		// Unregister other from its old address, register this at new address
		FocusManager::Get().unregisterFocusable(&other);
		FocusManager::Get().registerFocusable(this, m_tabIndex);
		other.m_tabIndex = -2;	// Mark as moved-from to prevent double-unregister
	}

	TabBar& TabBar::operator=(TabBar&& other) noexcept {
		if (this != &other) {
			// Unregister this from FocusManager
			FocusManager::Get().unregisterFocusable(this);

			// Move base class
			Component::operator=(std::move(other));

			// Move data
			position = other.position;
			width = other.width;
			id = other.id;
			m_tabs = std::move(other.m_tabs);
			m_selectedId = std::move(other.m_selectedId);
			m_selectedIndex = other.m_selectedIndex;
			m_hoveredIndex = other.m_hoveredIndex;
			m_focused = other.m_focused;
			m_appearance = other.m_appearance;
			m_onSelect = std::move(other.m_onSelect);
			m_height = other.m_height;
			m_tabWidths = std::move(other.m_tabWidths);
			m_tabOffsets = std::move(other.m_tabOffsets);
			m_tabIndex = other.m_tabIndex;
			m_mouseDown = other.m_mouseDown;

			// Unregister other from its old address, register this at new address
			FocusManager::Get().unregisterFocusable(&other);
			FocusManager::Get().registerFocusable(this, m_tabIndex);
			other.m_tabIndex = -2;	// Mark as moved-from
		}
		return *this;
	}

	bool TabBar::handleEvent(InputEvent& event) {
		if (!visible) {
			return false;
		}

		// Update hover state on mouse move
		if (event.type == InputEvent::Type::MouseMove) {
			m_hoveredIndex = getTabIndexAtPosition(event.position);
			// Don't consume mouse move - let it propagate for other components
			return false;
		}

		// Handle mouse down - start tracking
		if (event.type == InputEvent::Type::MouseDown && event.button == engine::MouseButton::Left) {
			int tabIndex = getTabIndexAtPosition(event.position);
			if (tabIndex >= 0) {
				m_mouseDown = true;
				event.consume();
				return true;
			}
			return false;
		}

		// Handle mouse up - select tab if still over it
		if (event.type == InputEvent::Type::MouseUp && event.button == engine::MouseButton::Left) {
			if (m_mouseDown) {
				int tabIndex = getTabIndexAtPosition(event.position);
				if (tabIndex >= 0) {
					selectTabByIndex(tabIndex);
				}
				m_mouseDown = false;
				event.consume();
				return true;
			}
		}

		return false;
	}

	void TabBar::update(float /*deltaTime*/) {
		// Nothing to update per-frame currently
		// Could add animation support here later
	}

	void TabBar::render() {
		if (!visible) {
			return;
		}

		// Draw bar background
		Foundation::Rect barBounds{
			position.x,
			position.y,
			width,
			m_height};
		Renderer::Primitives::drawRect({.bounds = barBounds, .style = m_appearance.barBackground, .id = id});

		// Get font renderer for text measurements
		ui::FontRenderer* fontRenderer = Renderer::Primitives::getFontRenderer();

		// Draw each tab
		for (size_t i = 0; i < m_tabs.size(); ++i) {
			const Tab&		 tab = m_tabs[i];
			const TabStyle&	 style = getTabStyle(static_cast<int>(i));
			Foundation::Rect tabBounds = getTabBounds(static_cast<int>(i));

			// Draw tab background
			Renderer::Primitives::drawRect({.bounds = tabBounds, .style = style.background, .id = id});

			// Calculate text scale from fontSize (16px base = 1.0 scale)
			constexpr float kBaseFontSize = 16.0F;
			float			scale = style.fontSize / kBaseFontSize;

			// Calculate centered text position
			Foundation::Vec2 textPos{
				tabBounds.x + tabBounds.width * 0.5F,
				tabBounds.y + tabBounds.height * 0.5F};

			// Adjust for center/middle alignment using font measurements
			if (fontRenderer != nullptr) {
				glm::vec2 textSize = fontRenderer->MeasureText(tab.label, scale);
				float	  ascent = fontRenderer->getAscent(scale);
				textPos.x -= textSize.x * 0.5F;	 // Center horizontally
				textPos.y -= ascent * 0.5F;		 // Center vertically
			}

			Renderer::Primitives::drawText({
				.text = tab.label,
				.position = textPos,
				.scale = scale,
				.color = style.textColor,
				.id = id});
		}
	}

	// IFocusable implementation

	void TabBar::onFocusGained() {
		m_focused = true;
	}

	void TabBar::onFocusLost() {
		m_focused = false;
	}

	void TabBar::handleKeyInput(engine::Key key, bool /*shift*/, bool /*ctrl*/, bool /*alt*/) {
		if (m_tabs.empty()) {
			return;
		}

		if (key == engine::Key::Left) {
			// Move to previous non-disabled tab
			int newIndex = m_selectedIndex;
			for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
				newIndex = (newIndex - 1 + static_cast<int>(m_tabs.size())) % static_cast<int>(m_tabs.size());
				if (!m_tabs[static_cast<size_t>(newIndex)].disabled) {
					selectTabByIndex(newIndex);
					break;
				}
			}
		} else if (key == engine::Key::Right) {
			// Move to next non-disabled tab
			int newIndex = m_selectedIndex;
			for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
				newIndex = (newIndex + 1) % static_cast<int>(m_tabs.size());
				if (!m_tabs[static_cast<size_t>(newIndex)].disabled) {
					selectTabByIndex(newIndex);
					break;
				}
			}
		} else if (key == engine::Key::Enter || key == engine::Key::Space) {
			// Confirm current selection - fires callback if valid tab selected
			// Uses selectTabByIndex for consistency, but note that it won't fire
			// if the tab is already selected (same behavior as arrow keys)
			if (m_selectedIndex >= 0) {
				selectTabByIndex(m_selectedIndex);
			}
		}
	}

	void TabBar::handleCharInput(char32_t /*codepoint*/) {
		// TabBar doesn't use character input
	}

	bool TabBar::canReceiveFocus() const {
		// Can receive focus if there's at least one non-disabled tab
		for (const auto& tab : m_tabs) {
			if (!tab.disabled) {
				return true;
			}
		}
		return false;
	}

	// Tab API

	void TabBar::setSelected(const std::string& tabId) {
		int index = findTabIndex(tabId);
		if (index >= 0) {
			selectTabByIndex(index);
		}
	}

	// Private methods

	const TabStyle& TabBar::getTabStyle(int tabIndex) const {
		TabState state = getTabState(tabIndex);

		switch (state) {
			case TabState::Disabled:
				return m_appearance.disabled;
			case TabState::Active:
				// If focused and this is the active tab, use focused style
				if (m_focused) {
					return m_appearance.focused;
				}
				return m_appearance.active;
			case TabState::Hover:
				return m_appearance.hover;
			case TabState::Normal:
			default:
				return m_appearance.normal;
		}
	}

	TabBar::TabState TabBar::getTabState(int tabIndex) const {
		if (tabIndex < 0 || tabIndex >= static_cast<int>(m_tabs.size())) {
			return TabState::Normal;
		}

		// Priority: Disabled > Active > Hover > Normal
		if (m_tabs[static_cast<size_t>(tabIndex)].disabled) {
			return TabState::Disabled;
		}
		if (tabIndex == m_selectedIndex) {
			return TabState::Active;
		}
		if (tabIndex == m_hoveredIndex) {
			return TabState::Hover;
		}
		return TabState::Normal;
	}

	int TabBar::getTabIndexAtPosition(Foundation::Vec2 pos) const {
		for (size_t i = 0; i < m_tabs.size(); ++i) {
			Foundation::Rect bounds = getTabBounds(static_cast<int>(i));
			if (pos.x >= bounds.x && pos.x <= bounds.x + bounds.width && pos.y >= bounds.y &&
				pos.y <= bounds.y + bounds.height) {
				// Don't return disabled tabs as hoverable
				if (!m_tabs[i].disabled) {
					return static_cast<int>(i);
				}
			}
		}
		return -1;
	}

	Foundation::Rect TabBar::getTabBounds(int tabIndex) const {
		if (tabIndex < 0) {
			return {0.0F, 0.0F, 0.0F, 0.0F};
		}

		auto index = static_cast<size_t>(tabIndex);

		// Check all vectors for bounds safety (they're resized together in recomputeLayout)
		if (index >= m_tabs.size() || index >= m_tabOffsets.size() || index >= m_tabWidths.size()) {
			return {0.0F, 0.0F, 0.0F, 0.0F};
		}

		float tabHeight = m_height - 2.0F * m_appearance.barPadding;

		return {
			position.x + m_appearance.barPadding + m_tabOffsets[index],
			position.y + m_appearance.barPadding,
			m_tabWidths[index],
			tabHeight};
	}

	void TabBar::recomputeLayout() {
		m_tabWidths.clear();
		m_tabOffsets.clear();

		if (m_tabs.empty()) {
			m_height = m_appearance.barPadding * 2.0F + 24.0F;	// Minimum height
			return;
		}

		// Calculate width for each tab based on text + padding
		// For simplicity, we'll distribute width evenly for now
		float availableWidth = width - 2.0F * m_appearance.barPadding;
		float totalSpacing = m_appearance.tabSpacing * static_cast<float>(m_tabs.size() - 1);
		float tabWidth = (availableWidth - totalSpacing) / static_cast<float>(m_tabs.size());

		float currentOffset = 0.0F;
		for (size_t i = 0; i < m_tabs.size(); ++i) {
			m_tabWidths.push_back(tabWidth);
			m_tabOffsets.push_back(currentOffset);
			currentOffset += tabWidth + m_appearance.tabSpacing;
		}

		// Height: padding + text height + padding
		m_height = m_appearance.barPadding * 2.0F + m_appearance.normal.paddingY * 2.0F + m_appearance.normal.fontSize;
	}

	int TabBar::findTabIndex(const std::string& tabId) const {
		for (size_t i = 0; i < m_tabs.size(); ++i) {
			if (m_tabs[i].id == tabId) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	void TabBar::selectTabByIndex(int index) {
		if (index < 0 || index >= static_cast<int>(m_tabs.size())) {
			return;
		}

		// Don't select disabled tabs
		if (m_tabs[static_cast<size_t>(index)].disabled) {
			return;
		}

		// Don't fire callback if already selected (unless Enter key pressed explicitly)
		if (index == m_selectedIndex) {
			return;
		}

		m_selectedIndex = index;
		m_selectedId = m_tabs[static_cast<size_t>(index)].id;

		if (m_onSelect) {
			m_onSelect(m_selectedId);
		}
	}

}  // namespace UI

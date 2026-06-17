#include "components/tabbar/TabBar.h"

#include "font/FontRenderer.h"
#include "graphics/PrimitiveStyles.h"
#include "graphics/Rect.h"
#include "primitives/Primitives.h"
#include "theme/Tokens.h"
#include "theme/Variants.h"

#include <glm/glm.hpp>
#include <input/InputTypes.h>

namespace UI {

	namespace {

		// drawText scale is relative to a 16px base.
		constexpr float kTextBasePx = 16.0F;

		float textScale(float sizePx) { return sizePx / kTextBasePx; }

		// Salvage tab metrics: 34px bar, fs_sm labels in fontDisplay, letter-spaced
		// (ls_wide) and uppercased; the cell adds space_3 padding on each side.
		constexpr float kBarHeight = 34.0F;
		constexpr float kLabelPx = fs_sm;
		constexpr float kLabelSpacing = kLabelPx * ls_wide;
		constexpr float kUnderline = 2.0F; // active underline thickness

	} // namespace

	TabBar::TabBar(const Args& args)
		: FocusableBase<TabBar>(args.tabIndex),
		  id(args.id),
		  m_tabs(args.tabs),
		  m_selectedId(args.selectedId),
		  m_appearance(args.appearance),
		  m_onSelect(args.onSelect) {

		// Initialize base class members
		position = args.position;
		size.x = args.width;  // width stored in size.x
		margin = args.margin;

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
		// FocusManager registration handled by FocusableBase constructor
	}

	// Destructor and move operations are = default in header.
	// FocusableBase handles FocusManager registration/unregistration.

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

		using Renderer::Primitives::drawLine;
		using Renderer::Primitives::drawRect;
		using Renderer::Primitives::drawText;

		// Anchor at raw position to match getTabBounds() (which hit-testing reads);
		// the baseline and cells share the same origin.
		const float scale = textScale(kLabelPx);
		const float baselineY = position.y + kBarHeight;

		// Hairline baseline under the whole bar.
		drawLine({.start = {position.x, baselineY},
				  .end = {position.x + size.x, baselineY},
				  .style = {.color = line_hairline, .width = bw},
				  .id = id});

		for (size_t i = 0; i < m_tabs.size(); ++i) {
			const Tab&			   tab = m_tabs[i];
			const Foundation::Rect cell = getTabBounds(static_cast<int>(i));
			const bool			   active = static_cast<int>(i) == m_selectedIndex;

			// Active label is bright, disabled is faint, everything else dims.
			Foundation::Color labelColor = text_dim;
			if (tab.disabled) {
				labelColor = text_faint;
			} else if (active) {
				labelColor = text_bright;
			}

			// Label centered in the cell; the text primitive owns the uppercase,
			// the letter-spacing, and the alignment.
			drawText({.text = tab.label,
					  .position = {cell.x, cell.y},
					  .scale = scale,
					  .color = labelColor,
					  .font = fontDisplay,
					  .hAlign = Foundation::HorizontalAlign::Center,
					  .vAlign = Foundation::VerticalAlign::Middle,
					  .boxWidth = cell.width,
					  .boxHeight = cell.height,
					  .letterSpacing = kLabelSpacing,
					  .transform = Foundation::TextTransform::Uppercase,
					  .id = id});

			// Active underline: a 2px accent bar flush on the baseline.
			if (active && !tab.disabled) {
				drawRect({.bounds = {cell.x, baselineY - kUnderline, cell.width, kUnderline},
						  .style = {.fill = accent},
						  .id = id});
			}
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

		// Salvage tabs sit flush against the bar's edges with no container padding
		// or inter-tab gap. getTabBounds() derives cell rects from these fields, so
		// zero them to keep hit-testing aligned with what render() draws.
		m_appearance.barPadding = 0.0F;
		m_appearance.tabSpacing = 0.0F;

		m_height = kBarHeight;

		if (m_tabs.empty()) {
			return;
		}

		// Per-tab width: measured label (display font, fs_sm scale, ls_wide spacing)
		// plus space_3 of padding on each side.
		const float			scale = textScale(kLabelPx);
		ui::FontRenderer*	fontRenderer = Renderer::Primitives::getFontRenderer();

		float currentOffset = 0.0F;
		for (const Tab& tab : m_tabs) {
			float labelWidth = 0.0F;
			if (fontRenderer != nullptr) {
				// render() uppercases the label, so measure the uppercased form or the
				// cell widths (and hit regions) drift from what's drawn.
				std::string measured = tab.label;
				for (char& ch : measured) {
					if (ch >= 'a' && ch <= 'z') {
						ch = static_cast<char>(ch - ('a' - 'A'));
					}
				}
				labelWidth = fontRenderer->MeasureText(measured, scale, fontDisplay, kLabelSpacing).x;
			}
			const float cellWidth = labelWidth + (space_3 * 2.0F);

			m_tabWidths.push_back(cellWidth);
			m_tabOffsets.push_back(currentOffset);
			currentOffset += cellWidth;
		}
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

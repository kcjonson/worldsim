#include "ContextMenu.h"

#include "focus/FocusManager.h"
#include "graphics/PrimitiveStyles.h"
#include "primitives/Primitives.h"

#include <algorithm>

namespace UI {

	// drawText scale is relative to a 16px base.
	constexpr float kTextBasePx = 16.0F;

	ContextMenu::ContextMenu(const Args& args)
		: FocusableBase<ContextMenu>(args.tabIndex),
		  items(args.items),
		  onClose(args.onClose) {
		visible = false;
		zIndex = 400; // Above most UI, below dialogs (500)
	}

	void ContextMenu::openAt(Foundation::Vec2 pos, float screenWidth, float screenHeight) {
		if (state == State::Open) {
			return;
		}

		screenW = screenWidth;
		screenH = screenHeight;

		// Calculate clamped position
		position = calculatePosition(pos);
		size = {getMenuWidth(), getMenuHeight()};

		state = State::Open;
		visible = true;
		hoveredIndex = -1;
		ignoreNextMouseUp = true; // Ignore the MouseUp from the opening click

		// Take focus to receive keyboard input
		FocusManager::Get().setFocus(this);
	}

	void ContextMenu::close() {
		if (state == State::Closed) {
			return;
		}

		state = State::Closed;
		visible = false;
		hoveredIndex = -1;

		if (onClose) {
			onClose();
		}
	}

	float ContextMenu::getMenuWidth() const {
		// Minimum width; could expand for longer labels.
		return kMinWidth;
	}

	float ContextMenu::getMenuHeight() const {
		if (items.empty()) {
			return kPadding * 2;
		}
		return static_cast<float>(items.size()) * kItemHeight + kPadding * 2;
	}

	Foundation::Rect ContextMenu::getItemBounds(size_t index) const {
		float itemY = position.y + kPadding + static_cast<float>(index) * kItemHeight;
		return {
			position.x + kPadding,
			itemY,
			getMenuWidth() - kPadding * 2,
			kItemHeight
		};
	}

	int ContextMenu::getItemAtPoint(Foundation::Vec2 point) const {
		if (items.empty()) {
			return -1;
		}

		// Check if point is within menu bounds
		float menuWidth = getMenuWidth();
		float menuHeight = getMenuHeight();

		if (point.x < position.x || point.x >= position.x + menuWidth || point.y < position.y || point.y >= position.y + menuHeight) {
			return -1;
		}

		float relativeY = point.y - position.y - kPadding;
		int	  index = static_cast<int>(relativeY / kItemHeight);

		if (index < 0 || static_cast<size_t>(index) >= items.size()) {
			return -1;
		}

		return index;
	}

	Foundation::Vec2 ContextMenu::calculatePosition(Foundation::Vec2 cursor) const {
		float menuWidth = getMenuWidth();
		float menuHeight = getMenuHeight();

		float x = cursor.x;
		float y = cursor.y;

		// Clamp to right edge
		if (x + menuWidth > screenW) {
			x = screenW - menuWidth;
		}

		// Clamp to bottom edge
		if (y + menuHeight > screenH) {
			y = screenH - menuHeight;
		}

		// Clamp to left/top edges
		x = std::max(0.0F, x);
		y = std::max(0.0F, y);

		return {x, y};
	}

	void ContextMenu::selectItem(size_t index) {
		if (index >= items.size()) {
			return;
		}

		const ContextMenuItem& item = items[index];
		if (item.enabled && item.onSelect) {
			item.onSelect();
		}

		close();
	}

	bool ContextMenu::handleEvent(InputEvent& event) {
		if (state != State::Open) {
			return false;
		}

		switch (event.type) {
			case InputEvent::Type::MouseMove: {
				hoveredIndex = getItemAtPoint(event.position);
				return false; // Don't consume mouse move
			}

			case InputEvent::Type::MouseDown: {
				// Check if click is outside menu
				int itemIndex = getItemAtPoint(event.position);
				if (itemIndex < 0) {
					// Click outside - close menu
					close();
					event.consume();
					return true;
				}
				// Click inside - consume but wait for MouseUp
				event.consume();
				return true;
			}

			case InputEvent::Type::MouseUp: {
				// Ignore the MouseUp from the click that opened the menu
				if (ignoreNextMouseUp) {
					ignoreNextMouseUp = false;
					event.consume();
					return true;
				}

				int itemIndex = getItemAtPoint(event.position);
				if (itemIndex >= 0 && items[static_cast<size_t>(itemIndex)].enabled) {
					selectItem(static_cast<size_t>(itemIndex));
					event.consume();
					return true;
				}
				break;
			}

			default:
				break;
		}

		return false;
	}

	// IFocusable implementation

	void ContextMenu::onFocusGained() {
		// Context menu doesn't need visual focus state
	}

	void ContextMenu::onFocusLost() {
		// Close when focus is lost (e.g., user tabs away)
		if (state == State::Open) {
			close();
		}
	}

	void ContextMenu::handleKeyInput(engine::Key key, bool /*shift*/, bool /*ctrl*/, bool /*alt*/) {
		if (state != State::Open) {
			return;
		}

		if (key == engine::Key::Escape) {
			close();
			return;
		}

		// No keyboard navigation if there are no items
		if (items.empty()) {
			return;
		}

		if (key == engine::Key::Up) {
			// Move selection up
			if (hoveredIndex <= 0) {
				hoveredIndex = static_cast<int>(items.size()) - 1;
			} else {
				hoveredIndex--;
			}
			// Skip disabled items
			int startIndex = hoveredIndex;
			while (!items[static_cast<size_t>(hoveredIndex)].enabled) {
				hoveredIndex--;
				if (hoveredIndex < 0) {
					hoveredIndex = static_cast<int>(items.size()) - 1;
				}
				if (hoveredIndex == startIndex) {
					// All items are disabled - reset to no selection
					hoveredIndex = -1;
					break;
				}
			}
			return;
		}

		if (key == engine::Key::Down) {
			// Move selection down
			if (hoveredIndex < 0 || static_cast<size_t>(hoveredIndex) >= items.size() - 1) {
				hoveredIndex = 0;
			} else {
				hoveredIndex++;
			}
			// Skip disabled items
			int startIndex = hoveredIndex;
			while (!items[static_cast<size_t>(hoveredIndex)].enabled) {
				hoveredIndex++;
				if (static_cast<size_t>(hoveredIndex) >= items.size()) {
					hoveredIndex = 0;
				}
				if (hoveredIndex == startIndex) {
					// All items are disabled - reset to no selection
					hoveredIndex = -1;
					break;
				}
			}
			return;
		}

		if (key == engine::Key::Enter) {
			if (hoveredIndex >= 0 && static_cast<size_t>(hoveredIndex) < items.size()) {
				selectItem(static_cast<size_t>(hoveredIndex));
			}
		}
	}

	void ContextMenu::handleCharInput(char32_t /*codepoint*/) {
		// Context menu doesn't handle character input
	}

	bool ContextMenu::canReceiveFocus() const {
		return state == State::Open;
	}

	void ContextMenu::update(float /*deltaTime*/) {
		// No animation for now
	}

	void ContextMenu::render() {
		if (state != State::Open || !visible) {
			return;
		}

		float menuWidth = getMenuWidth();
		float menuHeight = getMenuHeight();

		// Raised Salvage surface: edge border, small radius, soft drop shadow.
		Renderer::Primitives::drawRect(
			Renderer::Primitives::RectArgs{
				.bounds = {position.x, position.y, menuWidth, menuHeight},
				.style =
					{.fill = bg_panel_raised,
					 .border = Foundation::BorderStyle{.color = line_edge,
													  .width = bw,
													  .cornerRadius = r_sm,
													  .position = Foundation::BorderPosition::Inside},
					 .boxShadow = Foundation::BoxShadow{.color = withAlpha(bg_void, 0.5F), .blur = 16.0F, .offset = {0.0F, 6.0F}}},
				.zIndex = zIndex,
			}
		);

		// Menu items
		for (size_t i = 0; i < items.size(); ++i) {
			Foundation::Rect	   itemBounds = getItemBounds(i);
			const ContextMenuItem& item = items[i];

			const bool hovered = static_cast<int>(i) == hoveredIndex && item.enabled;

			// Hover wash.
			if (hovered) {
				Renderer::Primitives::drawRect(
					Renderer::Primitives::RectArgs{
						.bounds = itemBounds,
						.style = {.fill = bg_hover},
						.zIndex = zIndex + 1,
					}
				);
			}

			// Label: hovered = bright, normal = body, disabled = muted (no hover).
			Foundation::Color textColor = !item.enabled ? text_disabled : (hovered ? text_bright : text);

			Renderer::Primitives::drawText(
				Renderer::Primitives::TextArgs{
					.text = item.label,
					.position = {itemBounds.x + space_3, itemBounds.y},
					.scale = fs_sm / kTextBasePx,
					.color = textColor,
					.font = fontUi,
					.vAlign = Foundation::VerticalAlign::Middle,
					.boxHeight = kItemHeight,
					.zIndex = static_cast<float>(zIndex + 2),
				}
			);
		}
	}

} // namespace UI

#include "Menu.h"

#include "primitives/Primitives.h"
#include "theme/PanelStyle.h"

namespace UI {

Menu::Menu(const Args& args)
	: items(args.items), menuWidth(args.width), hoveredItemIndex(args.hoveredIndex) {
	position = args.position;
	// Size is calculated dynamically based on items
	size = {menuWidth, getMenuHeight()};
}

void Menu::setItems(std::vector<MenuItem> newItems) {
	items = std::move(newItems);
	size = {menuWidth, getMenuHeight()};
}

void Menu::setWidth(float newWidth) {
	menuWidth = newWidth;
	size.x = menuWidth;
}

float Menu::getMenuHeight() const {
	if (items.empty()) {
		return kMenuPadding * 2;
	}
	return static_cast<float>(items.size()) * kMenuItemHeight + kMenuPadding * 2;
}

Foundation::Rect Menu::getBounds() const {
	Foundation::Vec2 contentPos = getContentPosition();
	return {contentPos.x, contentPos.y, menuWidth, getMenuHeight()};
}

Foundation::Rect Menu::getItemBounds(size_t index) const {
	Foundation::Rect bounds = getBounds();
	float			 itemY = bounds.y + kMenuPadding + static_cast<float>(index) * kMenuItemHeight;
	return {bounds.x + kMenuPadding, itemY, bounds.width - kMenuPadding * 2, kMenuItemHeight};
}

int Menu::getItemAtPoint(Foundation::Vec2 point) const {
	if (items.empty()) {
		return -1;
	}

	Foundation::Rect bounds = getBounds();
	if (!containsPoint(point)) {
		return -1;
	}

	float relativeY = point.y - bounds.y - kMenuPadding;
	int	  index = static_cast<int>(relativeY / kMenuItemHeight);

	if (index < 0 || static_cast<size_t>(index) >= items.size()) {
		return -1;
	}

	return index;
}

bool Menu::containsPoint(Foundation::Vec2 point) const {
	Foundation::Rect bounds = getBounds();
	return point.x >= bounds.x && point.x < bounds.x + bounds.width && point.y >= bounds.y &&
		   point.y < bounds.y + bounds.height;
}

void Menu::selectItem(size_t index) {
	if (index >= items.size()) {
		return;
	}

	const MenuItem& item = items[index];
	if (item.enabled && item.onSelect) {
		item.onSelect();
	}
}

bool Menu::handleEvent(InputEvent& event) {
	if (!visible || items.empty()) {
		return false;
	}

	switch (event.type) {
		case InputEvent::Type::MouseMove: {
			hoveredItemIndex = getItemAtPoint(event.position);
			// Don't consume mouse move - let parent see it too
			return false;
		}

		case InputEvent::Type::MouseDown: {
			if (containsPoint(event.position)) {
				// Consume click inside menu but don't select yet
				event.consume();
				return true;
			}
			break;
		}

		case InputEvent::Type::MouseUp: {
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

void Menu::update(float /*deltaTime*/) {
	// No animation for now
}

void Menu::render() {
	if (!visible || items.empty()) {
		return;
	}

	Foundation::Rect menuBounds = getBounds();

	// Menu background with floating panel style
	Renderer::Primitives::drawRect(Renderer::Primitives::RectArgs{
		.bounds = menuBounds,
		.style = PanelStyles::floating(),
		.zIndex = zIndex,
	});

	// Menu items
	for (size_t i = 0; i < items.size(); ++i) {
		Foundation::Rect itemBounds = getItemBounds(i);
		const MenuItem&	 item = items[i];

		// Hover highlight
		if (static_cast<int>(i) == hoveredItemIndex && item.enabled) {
			Renderer::Primitives::drawRect(Renderer::Primitives::RectArgs{
				.bounds = itemBounds,
				.style = {.fill = Theme::Dropdown::menuItemHover},
				.zIndex = zIndex + 1,
			});
		}

		// Item text
		Foundation::Color textColor = item.enabled ? Theme::Colors::textBody : Theme::Colors::textMuted;

		Renderer::Primitives::drawText(Renderer::Primitives::TextArgs{
			.text = item.label,
			.position = {itemBounds.x + 8.0F, itemBounds.y + (kMenuItemHeight - 12.0F) / 2.0F},
			.scale = 12.0F / 16.0F,
			.color = textColor,
			.zIndex = static_cast<float>(zIndex + 2),
		});
	}
}

} // namespace UI

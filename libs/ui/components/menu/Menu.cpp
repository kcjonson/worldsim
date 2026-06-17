#include "Menu.h"

#include "graphics/PrimitiveStyles.h"
#include "primitives/Primitives.h"

namespace UI {

namespace {
	// drawText scale is relative to a 16px base.
	constexpr float kTextBasePx = 16.0F;
} // namespace

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

	// Raised Salvage surface: edge border, small radius, soft drop shadow.
	Renderer::Primitives::drawRect(Renderer::Primitives::RectArgs{
		.bounds = menuBounds,
		.style = {.fill = bg_panel_raised,
				  .border = Foundation::BorderStyle{.color = line_edge,
												   .width = bw,
												   .cornerRadius = r_sm,
												   .position = Foundation::BorderPosition::Inside},
				  .boxShadow = Foundation::BoxShadow{.color = withAlpha(bg_void, 0.5F), .blur = 16.0F, .offset = {0.0F, 6.0F}}},
		.zIndex = zIndex,
	});

	// Menu items
	for (size_t i = 0; i < items.size(); ++i) {
		Foundation::Rect itemBounds = getItemBounds(i);
		const MenuItem&	 item = items[i];

		const bool hovered = static_cast<int>(i) == hoveredItemIndex && item.enabled;

		// Hover wash.
		if (hovered) {
			Renderer::Primitives::drawRect(Renderer::Primitives::RectArgs{
				.bounds = itemBounds,
				.style = {.fill = bg_hover},
				.zIndex = zIndex + 1,
			});
		}

		// Label: hovered = bright, normal = body, disabled = muted (no hover).
		Foundation::Color textColor = !item.enabled ? text_disabled : (hovered ? text_bright : text);

		Renderer::Primitives::drawText(Renderer::Primitives::TextArgs{
			.text = item.label,
			.position = {itemBounds.x + space_3, itemBounds.y},
			.scale = fs_sm / kTextBasePx,
			.color = textColor,
			.font = fontUi,
			.vAlign = Foundation::VerticalAlign::Middle,
			.boxHeight = kMenuItemHeight,
			.zIndex = static_cast<float>(zIndex + 2),
		});
	}
}

} // namespace UI

#include "Select.h"

#include "focus/FocusManager.h"
#include "graphics/PrimitiveStyles.h"
#include "primitives/Primitives.h"
#include "theme/Tokens.h"
#include "theme/Variants.h"

namespace UI {

	namespace {
		// drawText scale is relative to a 16px base.
		constexpr float kTextBasePx = 16.0F;

		float textScale(float sizePx) { return sizePx / kTextBasePx; }
	} // namespace

Select::Select(const Args& args)
	: FocusableBase<Select>(args.tabIndex),
	  options(args.options),
	  value(args.value),
	  placeholder(args.placeholder),
	  onChange(args.onChange),
	  disabled(args.disabled) {
	position = args.position;
	size = args.size;
	margin = args.margin;

	// Create Menu child component (initially hidden)
	menuHandle = addChild(Menu(Menu::Args{
		.position = {0.0F, 0.0F}, // Will be updated in updateMenuPosition
		.width = size.x,
		.items = convertToMenuItems(),
	}));

	// Hide menu initially
	if (auto* menu = getChild<Menu>(menuHandle)) {
		menu->visible = false;
		// Set high z-index for menu to render above other content
		menu->zIndex = 1000;
	}

	updateMenuPosition();
}

void Select::setValue(const std::string& newValue) {
	value = newValue;
}

void Select::setDisabled(bool newDisabled) {
	disabled = newDisabled;
	if (disabled) {
		closeMenu();
		buttonHovered = false;
		buttonPressed = false;
	}
}

void Select::setOptions(std::vector<SelectOption> newOptions) {
	options = std::move(newOptions);

	// Update menu items
	if (auto* menu = getChild<Menu>(menuHandle)) {
		menu->setItems(convertToMenuItems());
	}

	// If current value is no longer valid, clear it
	if (findSelectedIndex() < 0 && !value.empty()) {
		// Keep value but it won't match any option
		// Parent can decide what to do via onChange
	}

	if (open && options.empty()) {
		closeMenu();
	}
}

std::string Select::getSelectedLabel() const {
	int index = findSelectedIndex();
	if (index >= 0) {
		return options[static_cast<size_t>(index)].label;
	}
	return placeholder;
}

int Select::findSelectedIndex() const {
	for (size_t i = 0; i < options.size(); ++i) {
		if (options[i].value == value) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

std::vector<MenuItem> Select::convertToMenuItems() {
	std::vector<MenuItem> menuItems;
	menuItems.reserve(options.size());

	for (const auto& option : options) {
		// Capture value by copy to avoid stale index if options are modified
		std::string capturedValue = option.value;
		menuItems.push_back(MenuItem{
			.label = option.label,
			.onSelect = [this, capturedValue]() { selectOptionByValue(capturedValue); },
			.enabled = true,
		});
	}

	return menuItems;
}

void Select::updateMenuPosition() {
	if (auto* menu = getChild<Menu>(menuHandle)) {
		Foundation::Vec2 contentPos = getContentPosition();
		// Position menu directly below the button
		menu->setPosition(contentPos.x, contentPos.y + size.y);
	}
}

void Select::openMenu() {
	if (!open && !options.empty()) {
		open = true;
		// Highlight currently selected item
		hoveredItemIndex = findSelectedIndex();

		if (auto* menu = getChild<Menu>(menuHandle)) {
			menu->visible = true;
			menu->setHoveredIndex(hoveredItemIndex);
		}
	}
}

void Select::closeMenu() {
	open = false;
	hoveredItemIndex = -1;

	if (auto* menu = getChild<Menu>(menuHandle)) {
		menu->visible = false;
		menu->setHoveredIndex(-1);
	}
}

void Select::toggle() {
	if (open) {
		closeMenu();
	} else {
		openMenu();
	}
}

void Select::selectOption(size_t index) {
	if (index >= options.size()) {
		return;
	}

	const std::string& newValue = options[index].value;

	// Only fire onChange if value actually changed
	if (newValue != value) {
		value = newValue;
		if (onChange) {
			onChange(value);
		}
	}

	closeMenu();
}

void Select::selectOptionByValue(const std::string& optionValue) {
	// Only fire onChange if value actually changed
	if (optionValue != value) {
		value = optionValue;
		if (onChange) {
			onChange(value);
		}
	}

	closeMenu();
}

void Select::setPosition(float x, float y) {
	position = {x, y};
	updateMenuPosition();
}

bool Select::containsPoint(Foundation::Vec2 point) const {
	if (isPointInButton(point)) {
		return true;
	}
	if (open) {
		if (auto* menu = getChild<Menu>(menuHandle)) {
			return menu->containsPoint(point);
		}
	}
	return false;
}

Foundation::Rect Select::getButtonBounds() const {
	Foundation::Vec2 contentPos = getContentPosition();
	return {contentPos.x, contentPos.y, size.x, size.y};
}

bool Select::isPointInButton(Foundation::Vec2 point) const {
	Foundation::Rect bounds = getButtonBounds();
	return point.x >= bounds.x && point.x < bounds.x + bounds.width && point.y >= bounds.y &&
		   point.y < bounds.y + bounds.height;
}

bool Select::handleEvent(InputEvent& event) {
	if (!visible || disabled) {
		return false;
	}

	auto* menu = getChild<Menu>(menuHandle);

	switch (event.type) {
		case InputEvent::Type::MouseMove: {
			buttonHovered = isPointInButton(event.position);

			if (open && menu) {
				menu->handleEvent(event);
				hoveredItemIndex = menu->getHoveredIndex();
			}

			return false;
		}

		case InputEvent::Type::MouseDown: {
			if (isPointInButton(event.position)) {
				buttonPressed = true;
				FocusManager::Get().setFocus(this);
				event.consume();
				return true;
			}

			if (open && menu && menu->containsPoint(event.position)) {
				menu->handleEvent(event);
				event.consume();
				return true;
			}

			if (open) {
				closeMenu();
				event.consume();
				return true;
			}

			break;
		}

		case InputEvent::Type::MouseUp: {
			if (buttonPressed) {
				buttonPressed = false;
				if (isPointInButton(event.position)) {
					toggle();
				}
				event.consume();
				return true;
			}

			if (open && menu && menu->containsPoint(event.position)) {
				int itemIndex = menu->getItemAtPoint(event.position);
				if (itemIndex >= 0) {
					selectOption(static_cast<size_t>(itemIndex));
					event.consume();
					return true;
				}
			}

			break;
		}

		default:
			break;
	}

	return false;
}

void Select::update(float deltaTime) {
	if (auto* menu = getChild<Menu>(menuHandle)) {
		menu->update(deltaTime);
	}
}

void Select::render() {
	if (!visible) {
		return;
	}
	using Renderer::Primitives::drawRect;
	using Renderer::Primitives::drawText;

	const Foundation::Rect bounds = getButtonBounds();

	// Inset field on a hairline border; focus lifts the border to accent. The
	// open/pressed state borrows the brighter edge tint so the field reads as
	// "armed" while the popup is up.
	const bool		  active = open || buttonPressed;
	Foundation::Color borderColor = line_hairline;
	if (focused && !disabled) {
		borderColor = accent;
	} else if (active) {
		borderColor = line_edge;
	}

	drawRect(Renderer::Primitives::RectArgs{
		.bounds = bounds,
		.style = {.fill = bg_inset,
				  .border = Foundation::BorderStyle{
					  .color = borderColor, .width = bw, .cornerRadius = r_sm, .position = Foundation::BorderPosition::Inside}},
		.zIndex = zIndex,
	});

	// Hover wash on the closed field.
	if (buttonHovered && !disabled && !active) {
		drawRect(Renderer::Primitives::RectArgs{
			.bounds = bounds,
			.style = {.fill = bg_hover,
					  .border = Foundation::BorderStyle{
						  .color = bg_hover, .width = 0.0F, .cornerRadius = r_sm, .position = Foundation::BorderPosition::Inside}},
			.zIndex = zIndex,
		});
	}

	// Selected value (or placeholder). A real selection reads bright; placeholder
	// and disabled drop to dim/disabled tokens.
	const std::string displayText = getSelectedLabel();
	const bool		  hasValue = !value.empty() && findSelectedIndex() >= 0;
	Foundation::Color textColor;
	if (disabled) {
		textColor = text_disabled;
	} else if (hasValue) {
		textColor = text_bright;
	} else {
		textColor = text_dim;
	}

	const float chevronGutter = space_5; // room for the chevron on the right
	drawText(Renderer::Primitives::TextArgs{
		.text = displayText,
		.position = {bounds.x + space_3, bounds.y},
		.scale = textScale(fs_sm),
		.color = textColor,
		.font = fontUi,
		.hAlign = Foundation::HorizontalAlign::Left,
		.vAlign = Foundation::VerticalAlign::Middle,
		.boxWidth = bounds.width - space_3 - chevronGutter,
		.boxHeight = bounds.height,
		.zIndex = static_cast<float>(zIndex) + 0.1F,
	});

	// Chevron glyph on the right; brightens to accent while open/focused.
	const Foundation::Color chevronColor = disabled ? text_disabled : ((active || focused) ? accent_bright : text_dim);
	const float				chevronBoxX = bounds.x + bounds.width - chevronGutter;
	drawText(Renderer::Primitives::TextArgs{
		.text = open ? "^" : "v", // caret: up when open, down when closed
		.position = {chevronBoxX, bounds.y},
		.scale = textScale(fs_sm),
		.color = chevronColor,
		.font = fontMono,
		.hAlign = Foundation::HorizontalAlign::Center,
		.vAlign = Foundation::VerticalAlign::Middle,
		.boxWidth = chevronGutter,
		.boxHeight = bounds.height,
		.zIndex = static_cast<float>(zIndex) + 0.1F,
	});

	// Render menu if open
	if (open) {
		if (auto* menu = getChild<Menu>(menuHandle)) {
			menu->render();
		}
	}
}

// IFocusable implementation
void Select::onFocusGained() {
	focused = true;
}

void Select::onFocusLost() {
	focused = false;
	closeMenu();
}

void Select::handleKeyInput(engine::Key key, bool /*shift*/, bool /*ctrl*/, bool /*alt*/) {
	if (disabled) {
		return;
	}

	auto* menu = getChild<Menu>(menuHandle);

	if (key == engine::Key::Enter || key == engine::Key::Space) {
		if (open && hoveredItemIndex >= 0) {
			selectOption(static_cast<size_t>(hoveredItemIndex));
		} else {
			toggle();
		}
	} else if (key == engine::Key::Escape) {
		closeMenu();
	} else if (key == engine::Key::Down) {
		if (!open) {
			openMenu();
		} else if (hoveredItemIndex < static_cast<int>(options.size()) - 1) {
			hoveredItemIndex++;
			if (menu) {
				menu->setHoveredIndex(hoveredItemIndex);
			}
		}
	} else if (key == engine::Key::Up) {
		if (open && hoveredItemIndex > 0) {
			hoveredItemIndex--;
			if (menu) {
				menu->setHoveredIndex(hoveredItemIndex);
			}
		}
	}
}

void Select::handleCharInput(char32_t /*codepoint*/) {
	// No text input handling
}

bool Select::canReceiveFocus() const {
	return visible && !disabled;
}

} // namespace UI

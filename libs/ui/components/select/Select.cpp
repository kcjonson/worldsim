#include "Select.h"

#include "focus/FocusManager.h"
#include "primitives/Primitives.h"
#include "theme/PanelStyle.h"

namespace UI {

Select::Select(const Args& args)
	: FocusableBase<Select>(args.tabIndex),
	  options(args.options),
	  value(args.value),
	  placeholder(args.placeholder),
	  onChange(args.onChange) {
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

	for (size_t i = 0; i < options.size(); ++i) {
		const auto& option = options[i];
		menuItems.push_back(MenuItem{
			.label = option.label,
			.onSelect = [this, i]() { selectOption(i); },
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
	if (!visible) {
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

	Foundation::Rect bounds = getButtonBounds();

	// Determine button style based on state
	Foundation::Color buttonBg;
	Foundation::Color buttonBorder;
	if (open || buttonPressed) {
		buttonBg = Foundation::Color(0.25F, 0.35F, 0.50F, 0.95F);
		buttonBorder = Foundation::Color(0.40F, 0.55F, 0.75F, 1.0F);
	} else if (buttonHovered) {
		buttonBg = Foundation::Color(0.20F, 0.30F, 0.45F, 0.95F);
		buttonBorder = Foundation::Color(0.35F, 0.50F, 0.70F, 1.0F);
	} else {
		buttonBg = Foundation::Color(0.15F, 0.20F, 0.30F, 0.95F);
		buttonBorder = Foundation::Color(0.30F, 0.40F, 0.55F, 1.0F);
	}

	// Focus ring
	if (focused) {
		Renderer::Primitives::drawRect(Renderer::Primitives::RectArgs{
			.bounds = {bounds.x - 2, bounds.y - 2, bounds.width + 4, bounds.height + 4},
			.style = {.fill = Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F),
					  .border = Foundation::BorderStyle{.color = Foundation::Color(0.4F, 0.6F, 1.0F, 1.0F), .width = 2.0F}},
			.zIndex = zIndex,
		});
	}

	// Draw button background
	Renderer::Primitives::drawRect(Renderer::Primitives::RectArgs{
		.bounds = bounds,
		.style = {.fill = buttonBg, .border = Foundation::BorderStyle{.color = buttonBorder, .width = 1.0F}},
		.zIndex = zIndex,
	});

	// Draw selected label + dropdown indicator
	std::string displayText = getSelectedLabel();
	bool		hasValue = !value.empty() && findSelectedIndex() >= 0;
	Foundation::Color textColor = hasValue ? Foundation::Color::white() : Theme::Colors::textMuted;

	// Calculate text position (left-aligned with padding)
	float textX = bounds.x + 10.0F;
	float textY = bounds.y + (bounds.height - 12.0F) / 2.0F;

	// Draw selected value / placeholder
	Renderer::Primitives::drawText(Renderer::Primitives::TextArgs{
		.text = displayText,
		.position = {textX, textY},
		.scale = 12.0F / 16.0F,
		.color = textColor,
		.zIndex = static_cast<float>(zIndex) + 0.1F,
	});

	// Draw dropdown indicator on the right
	float indicatorX = bounds.x + bounds.width - 20.0F;
	Renderer::Primitives::drawText(Renderer::Primitives::TextArgs{
		.text = "v",
		.position = {indicatorX, textY},
		.scale = 12.0F / 16.0F,
		.color = Foundation::Color::white(),
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
	return visible;
}

} // namespace UI

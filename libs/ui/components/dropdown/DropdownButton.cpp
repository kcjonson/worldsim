#include "DropdownButton.h"

#include "components/button/ButtonStyle.h"
#include "focus/FocusManager.h"
#include "primitives/Primitives.h"
#include "theme/PanelStyle.h"

namespace UI {

	namespace {
		// Approximate average character width for simple text layout calculations.
		// This is a rough estimate - for precise layout, use FontRenderer::measureText().
		constexpr float kApproxCharWidth = 7.0F;

		// Chevron icon size for dropdown indicator
		constexpr float kChevronSize = 12.0F;
	}  // namespace

	DropdownButton::DropdownButton(const Args& args)
		: FocusableBase<DropdownButton>(args.tabIndex),
		  label(args.label),
		  buttonSize(args.buttonSize),
		  items(args.items),
		  openUpward(args.openUpward) {
		position = args.position;
		size = args.buttonSize;
		margin = args.margin;

		// Create Menu child component (initially hidden)
		menuHandle = addChild(Menu(
			Menu::Args{
				.position = {0.0F, 0.0F}, // Will be updated in updateMenuPosition
				.width = buttonSize.x,
				.items = convertToMenuItems(),
			}
		));

		// Hide menu initially
		if (auto* menu = getChild<Menu>(menuHandle)) {
			menu->visible = false;
			// Set high z-index for menu to render above other content
			menu->zIndex = 1000;
		}

		// Create chevron icon
		std::string chevronPath = openUpward ? "assets/ui/icons/chevron_up.svg" : "assets/ui/icons/chevron_down.svg";
		chevronHandle = addChild(Icon(
			Icon::Args{
				.position = {0.0F, 0.0F}, // Will be updated in updateChevronPosition
				.size = kChevronSize,
				.svgPath = chevronPath,
				.tint = Foundation::Color::white(),
			}
		));

		updateMenuPosition();
		updateChevronPosition();
	}

	std::vector<MenuItem> DropdownButton::convertToMenuItems() const {
		std::vector<MenuItem> menuItems;
		menuItems.reserve(items.size());

		for (const auto& dropdownItem : items) {
			// Capture callback by value to avoid stale index if items are modified
			auto callback = dropdownItem.onSelect;
			menuItems.push_back(
				MenuItem{
					.label = dropdownItem.label,
					.onSelect =
						[callback]() {
							if (callback) {
								callback();
							}
						},
					.enabled = dropdownItem.enabled,
				}
			);
		}

		return menuItems;
	}

	void DropdownButton::updateMenuPosition() {
		if (auto* menu = getChild<Menu>(menuHandle)) {
			Foundation::Vec2 contentPos = getContentPosition();
			if (openUpward) {
				// Position menu above the button
				float menuHeight = menu->getMenuHeight();
				menu->setPosition(contentPos.x, contentPos.y - menuHeight);
			} else {
				// Position menu directly below the button
				menu->setPosition(contentPos.x, contentPos.y + buttonSize.y);
			}
		}
	}

	void DropdownButton::updateChevronPosition() {
		if (auto* chevron = getChild<Icon>(chevronHandle)) {
			Foundation::Vec2 contentPos = getContentPosition();
			// Position chevron on the right side of the button, vertically centered
			constexpr float kRightPadding = 8.0F;
			float chevronX = contentPos.x + buttonSize.x - kChevronSize - kRightPadding;
			float chevronY = contentPos.y + (buttonSize.y - kChevronSize) / 2.0F;
			chevron->setPosition(chevronX, chevronY);
		}
	}

	void DropdownButton::openMenu() {
		if (!open && !items.empty()) {
			open = true;
			hoveredItemIndex = -1;

			// Update menu position before showing (height may have changed)
			updateMenuPosition();

			if (auto* menu = getChild<Menu>(menuHandle)) {
				menu->visible = true;
				menu->setHoveredIndex(-1);
			}
		}
	}

	void DropdownButton::closeMenu() {
		open = false;
		hoveredItemIndex = -1;

		if (auto* menu = getChild<Menu>(menuHandle)) {
			menu->visible = false;
			menu->setHoveredIndex(-1);
		}
	}

	void DropdownButton::toggle() {
		if (open) {
			closeMenu();
		} else {
			openMenu();
		}
	}

	void DropdownButton::setItems(std::vector<DropdownItem> newItems) {
		items = std::move(newItems);

		// Update menu items
		if (auto* menu = getChild<Menu>(menuHandle)) {
			menu->setItems(convertToMenuItems());
		}

		if (open && items.empty()) {
			closeMenu();
		}
	}

	void DropdownButton::setPosition(float x, float y) {
		position = {x, y};
		updateMenuPosition();
		updateChevronPosition();
	}

	bool DropdownButton::containsPoint(Foundation::Vec2 point) const {
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

	Foundation::Rect DropdownButton::getButtonBounds() const {
		Foundation::Vec2 contentPos = getContentPosition();
		return {contentPos.x, contentPos.y, buttonSize.x, buttonSize.y};
	}

	bool DropdownButton::isPointInButton(Foundation::Vec2 point) const {
		Foundation::Rect bounds = getButtonBounds();
		return point.x >= bounds.x && point.x < bounds.x + bounds.width && point.y >= bounds.y && point.y < bounds.y + bounds.height;
	}

	void DropdownButton::selectItem(size_t index) {
		if (index >= items.size()) {
			return;
		}

		const DropdownItem& item = items[index];
		if (item.enabled && item.onSelect) {
			item.onSelect();
		}

		closeMenu();
	}

	bool DropdownButton::handleEvent(InputEvent& event) {
		if (!visible) {
			return false;
		}

		// Get menu pointer for delegation
		auto* menu = getChild<Menu>(menuHandle);

		switch (event.type) {
			case InputEvent::Type::MouseMove: {
				buttonHovered = isPointInButton(event.position);

				if (open && menu) {
					// Delegate hover tracking to menu
					menu->handleEvent(event);
					hoveredItemIndex = menu->getHoveredIndex();
				}

				// Don't consume mouse move
				return false;
			}

			case InputEvent::Type::MouseDown: {
				// Check if clicking on button
				if (isPointInButton(event.position)) {
					buttonPressed = true;
					// Request focus - this will close other dropdowns via onFocusLost
					FocusManager::Get().setFocus(this);
					event.consume();
					return true;
				}

				// Check if clicking on menu
				if (open && menu && menu->containsPoint(event.position)) {
					// Let menu handle it (consume on MouseDown, select on MouseUp)
					menu->handleEvent(event);
					event.consume();
					return true;
				}

				// Click outside both button and menu - close menu
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
					if (itemIndex >= 0 && items[static_cast<size_t>(itemIndex)].enabled) {
						selectItem(static_cast<size_t>(itemIndex));
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

	void DropdownButton::update(float deltaTime) {
		// Update menu child
		if (auto* menu = getChild<Menu>(menuHandle)) {
			menu->update(deltaTime);
		}
	}

	void DropdownButton::render() {
		if (!visible) {
			return;
		}

		// Get button bounds
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
			Renderer::Primitives::drawRect(
				Renderer::Primitives::RectArgs{
					.bounds = {bounds.x - 2, bounds.y - 2, bounds.width + 4, bounds.height + 4},
					.style =
						{.fill = Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F),
						 .border = Foundation::BorderStyle{.color = Foundation::Color(0.4F, 0.6F, 1.0F, 1.0F), .width = 2.0F}},
					.zIndex = zIndex,
				}
			);
		}

		// Draw button background
		Renderer::Primitives::drawRect(
			Renderer::Primitives::RectArgs{
				.bounds = bounds,
				.style = {.fill = buttonBg, .border = Foundation::BorderStyle{.color = buttonBorder, .width = 1.0F}},
				.zIndex = zIndex,
			}
		);

		// Draw label (centered, leaving space for chevron icon on right)
		constexpr float kChevronSpace = 20.0F;  // Space for chevron icon on right
		float labelWidth = static_cast<float>(label.length()) * kApproxCharWidth;
		float textX = bounds.x + (bounds.width - kChevronSpace - labelWidth) / 2.0F;
		float textY = bounds.y + (bounds.height - 12.0F) / 2.0F;

		Renderer::Primitives::drawText(
			Renderer::Primitives::TextArgs{
				.text = label,
				.position = {textX, textY},
				.scale = 12.0F / 16.0F,
				.color = Foundation::Color::white(),
				.zIndex = static_cast<float>(zIndex) + 0.1F,
			}
		);

		// Render chevron icon
		if (auto* chevron = getChild<Icon>(chevronHandle)) {
			chevron->zIndex = zIndex + 1;
			chevron->render();
		}

		// Render menu if open (Menu handles its own rendering)
		if (open) {
			if (auto* menu = getChild<Menu>(menuHandle)) {
				menu->render();
			}
		}
	}

	// IFocusable implementation
	void DropdownButton::onFocusGained() {
		focused = true;
	}

	void DropdownButton::onFocusLost() {
		focused = false;
		// Close menu when losing focus
		closeMenu();
	}

	void DropdownButton::handleKeyInput(engine::Key key, bool /*shift*/, bool /*ctrl*/, bool /*alt*/) {
		auto* menu = getChild<Menu>(menuHandle);

		if (key == engine::Key::Enter || key == engine::Key::Space) {
			if (open && hoveredItemIndex >= 0) {
				selectItem(static_cast<size_t>(hoveredItemIndex));
			} else {
				toggle();
			}
		} else if (key == engine::Key::Escape) {
			closeMenu();
		} else if (key == engine::Key::Down) {
			if (!open) {
				openMenu();
				hoveredItemIndex = 0;
				if (menu) {
					menu->setHoveredIndex(0);
				}
			} else if (hoveredItemIndex < static_cast<int>(items.size()) - 1) {
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

	void DropdownButton::handleCharInput(char32_t /*codepoint*/) {
		// No text input handling
	}

	bool DropdownButton::canReceiveFocus() const {
		return visible;
	}

} // namespace UI

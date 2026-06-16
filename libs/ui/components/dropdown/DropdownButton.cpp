#include "DropdownButton.h"

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
				.tint = text_dim,
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
		using Renderer::Primitives::drawRect;
		using Renderer::Primitives::drawText;

		const Foundation::Rect bounds = getButtonBounds();
		const bool			   active = open || buttonPressed;

		// Secondary-style action button: transparent fill on an edge border. Open
		// arms the border to accent so it pairs with the popup; focus brightens it.
		Foundation::Color borderColor = line_edge;
		if (active) {
			borderColor = accent;
		} else if (focused) {
			borderColor = accent_bright;
		}

		drawRect(Renderer::Primitives::RectArgs{
			.bounds = bounds,
			.style = {.fill = Foundation::Color::transparent(),
					  .border = Foundation::BorderStyle{
						  .color = borderColor, .width = bw, .cornerRadius = r_sm, .position = Foundation::BorderPosition::Inside}},
			.zIndex = zIndex,
		});

		// State wash, rounded to match the corners.
		const auto overlay = [&](Foundation::Color c) {
			drawRect(Renderer::Primitives::RectArgs{
				.bounds = bounds,
				.style = {.fill = c,
						  .border = Foundation::BorderStyle{
							  .color = c, .width = 0.0F, .cornerRadius = r_sm, .position = Foundation::BorderPosition::Inside}},
				.zIndex = zIndex,
			});
		};
		if (active) {
			overlay(bg_active);
		} else if (buttonHovered) {
			overlay(bg_hover);
		}

		// Label: display font, uppercase, letter-spaced, brightening while active.
		const Foundation::Color textColor = active ? text_bright : text;
		const float				chevronGutter = space_5; // room for the chevron on the right
		drawText(Renderer::Primitives::TextArgs{
			.text = label,
			.position = {bounds.x, bounds.y},
			.scale = textScale(fs_sm),
			.color = textColor,
			.font = fontDisplay,
			.hAlign = Foundation::HorizontalAlign::Center,
			.vAlign = Foundation::VerticalAlign::Middle,
			.boxWidth = bounds.width - chevronGutter,
			.boxHeight = bounds.height,
			.letterSpacing = fs_sm * ls_wide,
			.transform = Foundation::TextTransform::Uppercase,
			.zIndex = static_cast<float>(zIndex) + 0.1F,
		});

		// Render chevron icon (tint follows the label color).
		if (auto* chevron = getChild<Icon>(chevronHandle)) {
			chevron->setTint(active ? accent_bright : text_dim);
			chevron->zIndex = zIndex + 1;
			chevron->render();
		}

		// Render menu if open. Defer it to the top overlay layer so it paints above
		// sibling widgets regardless of scene draw order (the menu's high zIndex orders
		// it within the overlay).
		if (open) {
			if (auto* menu = getChild<Menu>(menuHandle)) {
				Renderer::Primitives::submitOverlay(menu->zIndex, [menu]() { menu->render(); });
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

#include "BuildMenu.h"

#include <primitives/Primitives.h>
#include <theme/PanelStyle.h>
#include <theme/Theme.h>

namespace world_sim {

	namespace {
		constexpr float kPadding = 10.0F;
		constexpr float kTitleHeight = 24.0F;
		constexpr float kButtonHeight = 32.0F;
		constexpr float kButtonSpacing = 4.0F;
		constexpr float kFontSize = 14.0F;
	} // namespace

	BuildMenu::BuildMenu(const Args& args)
		: m_position(args.position),
		  m_onSelect(args.onSelect),
		  m_onClose(args.onClose) {
		// Create title text
		m_titleText = std::make_unique<UI::Text>(UI::Text::Args{
			.position = {m_position.x + m_menuWidth * 0.5F, m_position.y + kPadding + kTitleHeight * 0.5F},
			.text = "Build",
			.style =
				{
					.color = UI::Theme::Colors::textTitle,
					.fontSize = 16.0F,
					.hAlign = Foundation::HorizontalAlign::Center,
					.vAlign = Foundation::VerticalAlign::Middle,
				},
			.id = "build_menu_title"
		});

		rebuildButtons();
	}

	void BuildMenu::setItems(const std::vector<BuildMenuItem>& items) {
		m_items = items;
		rebuildButtons();
	}

	void BuildMenu::rebuildButtons() {
		float buttonWidth = m_menuWidth - 2 * kPadding;
		float layoutY = m_position.y + kPadding + kTitleHeight + kPadding;

		// Create layout container for buttons
		m_buttonLayout = std::make_unique<UI::LayoutContainer>(UI::LayoutContainer::Args{
			.position = {m_position.x + kPadding, layoutY},
			.size = {buttonWidth, 0.0F},  // Height determined by children
			.direction = UI::Direction::Vertical,
			.hAlign = UI::HAlign::Left
		});

		for (const auto& item : m_items) {
			// Capture defName by value for the callback
			std::string defName = item.defName;
			auto onClick = [this, defName]() {
				if (m_onSelect) {
					m_onSelect(defName);
				}
			};

			// Add button with bottom margin for spacing (half on each side = kButtonSpacing/2)
			m_buttonLayout->addChild(UI::Button(UI::Button::Args{
				.label = item.label,
				.size = {buttonWidth, kButtonHeight},
				.type = UI::Button::Type::Secondary,
				.margin = kButtonSpacing * 0.5F,
				.onClick = onClick,
				.id = "build_item"
			}));
		}

		// Calculate menu height: title area + button layout height + padding
		float buttonAreaHeight = static_cast<float>(m_items.size()) * (kButtonHeight + kButtonSpacing);
		m_menuHeight = kPadding + kTitleHeight + kPadding + buttonAreaHeight + kPadding;

		// Update title position
		if (m_titleText) {
			m_titleText->position = {m_position.x + m_menuWidth * 0.5F, m_position.y + kPadding + kTitleHeight * 0.5F};
		}
	}

	void BuildMenu::setPosition(Foundation::Vec2 newPosition) {
		if (m_position.x == newPosition.x && m_position.y == newPosition.y) {
			return;
		}
		m_position = newPosition;
		rebuildButtons();
	}

	bool BuildMenu::handleEvent(UI::InputEvent& event) {
		// Dispatch to button layout
		if (m_buttonLayout && m_buttonLayout->handleEvent(event)) {
			return true;
		}

		// Consume clicks within the menu bounds to prevent click-through to game world
		const auto& pos = event.position;
		if ((event.type == UI::InputEvent::Type::MouseDown || event.type == UI::InputEvent::Type::MouseUp) && pos.x >= m_position.x &&
			pos.x <= m_position.x + m_menuWidth && pos.y >= m_position.y && pos.y <= m_position.y + m_menuHeight) {
			event.consume();
			return true;
		}

		return false;
	}

	void BuildMenu::render() {
		// Draw background panel
		Foundation::Rect menuRect{m_position.x, m_position.y, m_menuWidth, m_menuHeight};

		Renderer::Primitives::drawRect(
			Renderer::Primitives::RectArgs{
				.bounds = menuRect,
				.style = UI::PanelStyles::floating(),
				.id = "build_menu_bg"
			}
		);

		// Draw title
		if (m_titleText) {
			m_titleText->render();
		}

		// Draw buttons via layout
		if (m_buttonLayout) {
			m_buttonLayout->render();
		}
	}

	Foundation::Rect BuildMenu::bounds() const {
		return {m_position.x, m_position.y, m_menuWidth, m_menuHeight};
	}

} // namespace world_sim

#include "BuildMenu.h"

#include <primitives/Primitives.h>

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
					.color = Foundation::Color::white(),
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
		m_itemButtons.clear();

		float buttonWidth = m_menuWidth - 2 * kPadding;
		float y = m_position.y + kPadding + kTitleHeight + kPadding;

		for (size_t i = 0; i < m_items.size(); ++i) {
			const auto& item = m_items[i];

			// Capture defName by value for the callback
			std::string defName = item.defName;
			auto		onClick = [this, defName]() {
				   if (m_onSelect) {
					   m_onSelect(defName);
				   }
			};

			auto button = std::make_unique<UI::Button>(UI::Button::Args{
				.label = item.label,
				.position = {m_position.x + kPadding, y},
				.size = {buttonWidth, kButtonHeight},
				.type = UI::Button::Type::Secondary,
				.onClick = onClick,
				.id = "build_item" // ID doesn't need to be unique for MVP
			});

			m_itemButtons.push_back(std::move(button));
			y += kButtonHeight + kButtonSpacing;
		}

		// Calculate menu height based on content
		m_menuHeight =
			kPadding + kTitleHeight + kPadding + static_cast<float>(m_items.size()) * (kButtonHeight + kButtonSpacing) + kPadding;

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

	void BuildMenu::handleInput() {
		for (auto& button : m_itemButtons) {
			if (button) {
				button->handleInput();
			}
		}
	}

	void BuildMenu::render() {
		// Draw background panel
		Foundation::Rect menuRect{m_position.x, m_position.y, m_menuWidth, m_menuHeight};

		Renderer::Primitives::drawRect(
			Renderer::Primitives::RectArgs{
				.bounds = menuRect,
				.style =
					Foundation::RectStyle{
						.fill = Foundation::Color{0.15F, 0.15F, 0.2F, 0.95F},
						.border = Foundation::BorderStyle{.color = Foundation::Color{0.4F, 0.4F, 0.5F, 1.0F}, .width = 1.0F}
					},
				.id = "build_menu_bg"
			}
		);

		// Draw title
		if (m_titleText) {
			m_titleText->render();
		}

		// Draw buttons
		for (auto& button : m_itemButtons) {
			if (button) {
				button->render();
			}
		}
	}

	bool BuildMenu::isPointOver(Foundation::Vec2 point) const {
		return point.x >= m_position.x && point.x <= m_position.x + m_menuWidth && point.y >= m_position.y &&
			   point.y <= m_position.y + m_menuHeight;
	}

	Foundation::Rect BuildMenu::bounds() const {
		return {m_position.x, m_position.y, m_menuWidth, m_menuHeight};
	}

} // namespace world_sim

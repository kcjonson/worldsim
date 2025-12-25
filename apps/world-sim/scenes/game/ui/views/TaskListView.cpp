#include "TaskListView.h"

#include <ecs/components/Action.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/DecisionTrace.h>
#include <ecs/components/Task.h>
#include <theme/PanelStyle.h>
#include <theme/Theme.h>

namespace world_sim {

namespace {
	// Status indicator for decision trace display
	std::string statusIndicator(ecs::OptionStatus status) {
		switch (status) {
			case ecs::OptionStatus::Selected:
				return "> "; // Current task (arrow)
			case ecs::OptionStatus::Available:
				return "  "; // Could do this (indented)
			case ecs::OptionStatus::NoSource:
				return "x "; // Can't fulfill
			case ecs::OptionStatus::Satisfied:
				return ""; // Don't display
		}
		return "";
	}

} // namespace

TaskListView::TaskListView(const Args& args)
	: m_onClose(args.onClose),
	  m_panelWidth(args.width),
	  m_maxHeight(args.maxHeight) {

	// Background panel
	m_backgroundHandle = addChild(
		UI::Rectangle(
			UI::Rectangle::Args{
				.position = {0.0F, 0.0F},
				.size = {m_panelWidth, m_panelHeight},
				.style = UI::PanelStyles::floating(),
				.zIndex = 0,
				.id = (args.id + "_bg").c_str()
			}
		)
	);

	// Close button background
	m_closeButtonBgHandle = addChild(
		UI::Rectangle(
			UI::Rectangle::Args{
				.position = {0.0F, 0.0F},
				.size = {kCloseButtonSize, kCloseButtonSize},
				.style = UI::PanelStyles::closeButton(),
				.zIndex = 2,
				.id = (args.id + "_close_bg").c_str()
			}
		)
	);

	// Close button text
	m_closeButtonTextHandle = addChild(
		UI::Text(
			UI::Text::Args{
				.position = {0.0F, 0.0F},
				.text = "X",
				.style =
					{
						.color = UI::Theme::Colors::closeButtonText,
						.fontSize = 10.0F,
						.hAlign = Foundation::HorizontalAlign::Center,
						.vAlign = Foundation::VerticalAlign::Middle,
					},
				.zIndex = 3,
				.id = (args.id + "_close_text").c_str()
			}
		)
	);

	// Panel title
	m_titleHandle = addChild(
		UI::Text(
			UI::Text::Args{
				.position = {0.0F, 0.0F},
				.text = "Task Queue",
				.style =
					{
						.color = UI::Theme::Colors::textTitle,
						.fontSize = kTitleFontSize,
						.hAlign = Foundation::HorizontalAlign::Left,
						.vAlign = Foundation::VerticalAlign::Top,
					},
				.zIndex = 1,
				.id = (args.id + "_title").c_str()
			}
		)
	);

	// Section headers
	m_currentTaskHeader = addChild(
		UI::Text(
			UI::Text::Args{
				.position = {0.0F, 0.0F},
				.text = "Current",
				.style =
					{
						.color = UI::Theme::Colors::textHeader,
						.fontSize = kHeaderFontSize,
						.hAlign = Foundation::HorizontalAlign::Left,
						.vAlign = Foundation::VerticalAlign::Top,
					},
				.zIndex = 1,
				.id = (args.id + "_current_header").c_str()
			}
		)
	);

	m_upNextHeader = addChild(
		UI::Text(
			UI::Text::Args{
				.position = {0.0F, 0.0F},
				.text = "Task Queue",
				.style =
					{
						.color = UI::Theme::Colors::textHeader,
						.fontSize = kHeaderFontSize,
						.hAlign = Foundation::HorizontalAlign::Left,
						.vAlign = Foundation::VerticalAlign::Top,
					},
				.zIndex = 1,
				.id = (args.id + "_upnext_header").c_str()
			}
		)
	);

	// Content text pool
	m_textHandles.reserve(kMaxTextLines);
	for (size_t i = 0; i < kMaxTextLines; ++i) {
		m_textHandles.push_back(addChild(
			UI::Text(
				UI::Text::Args{
					.position = {0.0F, 0.0F},
					.text = "",
					.style =
						{
							.color = UI::Theme::Colors::textBody,
							.fontSize = kTextFontSize,
							.hAlign = Foundation::HorizontalAlign::Left,
							.vAlign = Foundation::VerticalAlign::Top,
						},
					.zIndex = 1,
					.id = (args.id + "_text_" + std::to_string(i)).c_str()
				}
			)
		));
	}

	// Disable child sorting
	childrenNeedSorting = false;

	// Start hidden
	visible = false;
	hideContent();
}

void TaskListView::update(const ecs::World& world, ecs::EntityID colonistId) {
	// Rebuild content
	renderContent(world, colonistId);
}

bool TaskListView::handleEvent(UI::InputEvent& event) {
	if (!visible) {
		return false;
	}

	// Only handle mouse up (click) events
	if (event.type != UI::InputEvent::Type::MouseUp) {
		return false;
	}

	if (event.button != engine::MouseButton::Left) {
		return false;
	}

	auto pos = event.position;

	// Check close button
	float closeX = m_panelX + m_panelWidth - kPadding - kCloseButtonSize;
	float closeY = m_panelY + kPadding;

	if (pos.x >= closeX && pos.x <= closeX + kCloseButtonSize &&
		pos.y >= closeY && pos.y <= closeY + kCloseButtonSize) {
		if (m_onClose) {
			m_onClose();
		}
		event.consume();
		return true;
	}

	// Check if click is within panel bounds - consume to prevent world click
	if (pos.x >= m_panelX && pos.x <= m_panelX + m_panelWidth &&
		pos.y >= m_panelY && pos.y <= m_panelY + m_panelHeight) {
		event.consume();
		return true;
	}

	return false;
}

void TaskListView::setPosition(float x, float bottomY) {
	m_panelX = x;
	m_panelY = bottomY - m_panelHeight; // Panel grows upward from bottomY
}

void TaskListView::renderContent(const ecs::World& world, ecs::EntityID colonistId) {
	m_usedTextLines = 0;
	hideContent();

	// Get colonist name for title
	std::string title = "Task Queue";
	if (auto* colonist = world.getComponent<ecs::Colonist>(colonistId)) {
		title = colonist->name + " - Tasks";
	}

	// Calculate content height
	float contentHeight = kPadding + kTitleFontSize + kLineSpacing; // Title

	// Current task section
	contentHeight += kSectionSpacing + kHeaderFontSize + kLineSpacing; // "Current" header
	contentHeight += kTextFontSize + kLineSpacing;					   // Task text
	contentHeight += kTextFontSize + kLineSpacing;					   // Action text

	// Task queue section
	size_t queueItems = 0;
	if (auto* trace = world.getComponent<ecs::DecisionTrace>(colonistId)) {
		for (const auto& option : trace->options) {
			if (option.status != ecs::OptionStatus::Satisfied) {
				++queueItems;
			}
		}
	}
	contentHeight += kSectionSpacing + kHeaderFontSize + kLineSpacing;			   // "Task Queue" header
	contentHeight += static_cast<float>(queueItems) * (kTextFontSize + kLineSpacing); // Queue items

	contentHeight += kPadding; // Bottom padding

	m_panelHeight = std::min(contentHeight, m_maxHeight);
	m_panelY = m_panelY + m_panelHeight - contentHeight; // Adjust for new height (grow upward)

	// Position background
	if (auto* bg = getChild<UI::Rectangle>(m_backgroundHandle)) {
		bg->visible = true;
		bg->position = {m_panelX, m_panelY};
		bg->size = {m_panelWidth, m_panelHeight};
	}

	// Position close button (top-right)
	float closeX = m_panelX + m_panelWidth - kPadding - kCloseButtonSize;
	float closeY = m_panelY + kPadding;
	if (auto* closeBg = getChild<UI::Rectangle>(m_closeButtonBgHandle)) {
		closeBg->visible = true;
		closeBg->position = {closeX, closeY};
	}
	if (auto* closeText = getChild<UI::Text>(m_closeButtonTextHandle)) {
		closeText->visible = true;
		closeText->position = {closeX + kCloseButtonSize * 0.5F, closeY + kCloseButtonSize * 0.5F - 1.0F};
	}

	// Position title
	float yOffset = m_panelY + kPadding;
	if (auto* titleText = getChild<UI::Text>(m_titleHandle)) {
		titleText->visible = true;
		titleText->position = {m_panelX + kPadding, yOffset};
		titleText->text = title;
	}
	yOffset += kTitleFontSize + kLineSpacing;

	// Current task section
	yOffset += kSectionSpacing;
	if (auto* header = getChild<UI::Text>(m_currentTaskHeader)) {
		header->visible = true;
		header->position = {m_panelX + kPadding, yOffset};
	}
	yOffset += kHeaderFontSize + kLineSpacing;

	// Current task
	if (auto* task = world.getComponent<ecs::Task>(colonistId)) {
		if (m_usedTextLines < m_textHandles.size()) {
			if (auto* text = getChild<UI::Text>(m_textHandles[m_usedTextLines])) {
				text->visible = true;
				text->position = {m_panelX + kPadding + 8.0F, yOffset};
				if (task->isActive()) {
					text->text = "> " + task->reason;
					text->style.color = UI::Theme::Colors::statusActive;
				} else {
					text->text = "  (No active task)";
					text->style.color = UI::Theme::Colors::statusIdle;
				}
			}
			++m_usedTextLines;
		}
	}
	yOffset += kTextFontSize + kLineSpacing;

	// Current action
	if (auto* action = world.getComponent<ecs::Action>(colonistId)) {
		if (m_usedTextLines < m_textHandles.size()) {
			if (auto* text = getChild<UI::Text>(m_textHandles[m_usedTextLines])) {
				text->visible = true;
				text->position = {m_panelX + kPadding + 8.0F, yOffset};
				if (action->isActive()) {
					int progress = static_cast<int>(action->progress() * 100.0F);
					text->text = std::string("  ") + ecs::actionTypeName(action->type) + " (" + std::to_string(progress) + "%)";
					text->style.color = UI::Theme::Colors::statusPending;
				} else {
					text->text = "  Idle";
					text->style.color = UI::Theme::Colors::statusIdle;
				}
			}
			++m_usedTextLines;
		}
	}
	yOffset += kTextFontSize + kLineSpacing;

	// Task queue section
	yOffset += kSectionSpacing;
	if (auto* header = getChild<UI::Text>(m_upNextHeader)) {
		header->visible = true;
		header->position = {m_panelX + kPadding, yOffset};
	}
	yOffset += kHeaderFontSize + kLineSpacing;

	// Queue items from DecisionTrace
	if (auto* trace = world.getComponent<ecs::DecisionTrace>(colonistId)) {
		for (const auto& option : trace->options) {
			if (option.status == ecs::OptionStatus::Satisfied) {
				continue; // Skip satisfied needs
			}

			if (m_usedTextLines >= m_textHandles.size()) {
				break;
			}

			if (auto* text = getChild<UI::Text>(m_textHandles[m_usedTextLines])) {
				text->visible = true;
				text->position = {m_panelX + kPadding + 8.0F, yOffset};
				text->text = statusIndicator(option.status) + option.reason;

				// Color by status
				switch (option.status) {
					case ecs::OptionStatus::Selected:
						text->style.color = UI::Theme::Colors::statusActive;
						break;
					case ecs::OptionStatus::Available:
						text->style.color = UI::Theme::Colors::textBody;
						break;
					case ecs::OptionStatus::NoSource:
						text->style.color = UI::Theme::Colors::statusBlocked;
						break;
					default:
						text->style.color = UI::Theme::Colors::statusIdle;
						break;
				}
			}
			++m_usedTextLines;
			yOffset += kTextFontSize + kLineSpacing;
		}
	}
}

void TaskListView::hideContent() {
	for (auto* child : children) {
		child->visible = false;
	}
}

} // namespace world_sim

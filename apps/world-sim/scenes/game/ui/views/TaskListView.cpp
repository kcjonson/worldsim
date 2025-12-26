#include "TaskListView.h"

#include <ecs/components/Action.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/DecisionTrace.h>
#include <ecs/components/Task.h>
#include <theme/PanelStyle.h>
#include <theme/Theme.h>

namespace world_sim {

	namespace {
		// Convert OptionStatus to LineStatus
		UI::LineStatus toLineStatus(ecs::OptionStatus status) {
			switch (status) {
				case ecs::OptionStatus::Selected:
					return UI::LineStatus::Active;
				case ecs::OptionStatus::Available:
					return UI::LineStatus::Available;
				case ecs::OptionStatus::NoSource:
					return UI::LineStatus::Blocked;
				case ecs::OptionStatus::Satisfied:
					return UI::LineStatus::Idle;
			}
			return UI::LineStatus::Available;
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

		// Disable child sorting
		childrenNeedSorting = false;

		// Start hidden
		visible = false;
		hideContent();
	}

	void TaskListView::update(const ecs::World& world, ecs::EntityID colonistId) {
		// Only rebuild if colonist changed or content hasn't been built yet
		if (contentBuilt && lastColonistId == colonistId) {
			return;
		}
		lastColonistId = colonistId;
		contentBuilt = true;
		rebuildContent(world, colonistId);
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

		if (pos.x >= closeX && pos.x <= closeX + kCloseButtonSize && pos.y >= closeY && pos.y <= closeY + kCloseButtonSize) {
			if (m_onClose) {
				m_onClose();
			}
			event.consume();
			return true;
		}

		// Check if click is within panel bounds - consume to prevent world click
		if (pos.x >= m_panelX && pos.x <= m_panelX + m_panelWidth && pos.y >= m_panelY && pos.y <= m_panelY + m_panelHeight) {
			event.consume();
			return true;
		}

		return false;
	}

	void TaskListView::setPosition(float x, float bottomY) {
		m_panelX = x;
		m_bottomY = bottomY;
		m_panelY = bottomY - m_panelHeight; // Panel grows upward from bottomY
		Component::setPosition(m_panelX, m_panelY);
	}

	void TaskListView::rebuildContent(const ecs::World& world, ecs::EntityID colonistId) {
		hideContent();

		// Get colonist name for title
		std::string title = "Task Queue";
		if (auto* colonist = world.getComponent<ecs::Colonist>(colonistId)) {
			title = colonist->name + " - Tasks";
		}

		// Create content layout container
		float contentWidth = m_panelWidth - kPadding * 2;
		float contentY = kPadding + kTitleFontSize + kLineSpacing;

		m_contentLayout = std::make_unique<UI::LayoutContainer>(UI::LayoutContainer::Args{
			.position = {m_panelX + kPadding, 0.0F}, // Y will be set after height calculation
			.size = {contentWidth, 0.0F},
			.direction = UI::Direction::Vertical,
			.hAlign = UI::HAlign::Left
		});

		// --- Current Task Section ---
		m_contentLayout->addChild(
			UI::SectionHeader(
				UI::SectionHeader::Args{
					.text = "Current", .fontSize = kHeaderFontSize, .margin = kSectionSpacing * 0.5F, .id = "current_header"
				}
			)
		);

		// Current task
		if (auto* task = world.getComponent<ecs::Task>(colonistId)) {
			if (task->isActive()) {
				m_contentLayout->addChild(
					UI::StatusTextLine(
						UI::StatusTextLine::Args{
							.text = task->reason,
							.status = UI::LineStatus::Active,
							.fontSize = kTextFontSize,
							.margin = kLineSpacing * 0.5F,
							.id = "current_task"
						}
					)
				);
			} else {
				m_contentLayout->addChild(
					UI::StatusTextLine(
						UI::StatusTextLine::Args{
							.text = "(No active task)",
							.status = UI::LineStatus::Idle,
							.fontSize = kTextFontSize,
							.margin = kLineSpacing * 0.5F,
							.id = "current_task"
						}
					)
				);
			}
		}

		// Current action
		if (auto* action = world.getComponent<ecs::Action>(colonistId)) {
			if (action->isActive()) {
				int			progress = static_cast<int>(action->progress() * 100.0F);
				std::string actionText = std::string(ecs::actionTypeName(action->type)) + " (" + std::to_string(progress) + "%)";
				m_contentLayout->addChild(
					UI::StatusTextLine(
						UI::StatusTextLine::Args{
							.text = actionText,
							.status = UI::LineStatus::Pending,
							.fontSize = kTextFontSize,
							.margin = kLineSpacing * 0.5F,
							.id = "current_action"
						}
					)
				);
			} else {
				m_contentLayout->addChild(
					UI::StatusTextLine(
						UI::StatusTextLine::Args{
							.text = "Idle",
							.status = UI::LineStatus::Idle,
							.fontSize = kTextFontSize,
							.margin = kLineSpacing * 0.5F,
							.id = "current_action"
						}
					)
				);
			}
		}

		// --- Task Queue Section ---
		m_contentLayout->addChild(
			UI::SectionHeader(
				UI::SectionHeader::Args{
					.text = "Task Queue", .fontSize = kHeaderFontSize, .margin = kSectionSpacing * 0.5F, .id = "queue_header"
				}
			)
		);

		// Queue items from DecisionTrace
		if (auto* trace = world.getComponent<ecs::DecisionTrace>(colonistId)) {
			size_t itemIndex = 0;
			for (const auto& option : trace->options) {
				if (option.status == ecs::OptionStatus::Satisfied) {
					continue; // Skip satisfied needs
				}

				m_contentLayout->addChild(
					UI::StatusTextLine(
						UI::StatusTextLine::Args{
							.text = option.reason,
							.status = toLineStatus(option.status),
							.fontSize = kTextFontSize,
							.margin = kLineSpacing * 0.5F,
							.id = "queue_item_" + std::to_string(itemIndex++)
						}
					)
				);
			}
		}

		// Calculate panel height from content layout
		// Header area: title + padding
		float headerHeight = kPadding + kTitleFontSize + kLineSpacing;
		// Content height computed from children (no render needed)
		float contentHeight = m_contentLayout->getHeight();
		// Total height
		float totalHeight = headerHeight + contentHeight + kPadding;

		m_panelHeight = std::min(totalHeight, m_maxHeight);
		m_panelY = m_bottomY - m_panelHeight;

		// Set content layout position (layout will be computed on first render)
		m_contentLayout->setPosition(m_panelX + kPadding, m_panelY + headerHeight);

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
		if (auto* titleText = getChild<UI::Text>(m_titleHandle)) {
			titleText->visible = true;
			titleText->position = {m_panelX + kPadding, m_panelY + kPadding};
			titleText->text = title;
		}
	}

	void TaskListView::render() {
		if (!visible) {
			return;
		}

		// Render fixed children (background, close button, title)
		Component::render();

		// Render content layout
		if (m_contentLayout) {
			m_contentLayout->render();
		}
	}

	void TaskListView::hideContent() {
		for (auto* child : children) {
			child->visible = false;
		}
		m_contentLayout.reset();
		contentBuilt = false;
	}

} // namespace world_sim

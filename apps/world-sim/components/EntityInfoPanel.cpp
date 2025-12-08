#include "EntityInfoPanel.h"

#include <ecs/components/Action.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Needs.h>
#include <ecs/components/Task.h>
#include <input/InputManager.h>

#include <iomanip>
#include <sstream>

namespace world_sim {

	namespace {
		// Need labels matching NeedType order
		constexpr std::array<const char*, 4> kNeedLabels = {"Hunger", "Thirst", "Energy", "Bladder"};

		// Format action description with progress
		std::string formatAction(const ecs::Action& action) {
			if (!action.isActive()) {
				return "Idle";
			}

			std::ostringstream oss;
			oss << ecs::actionTypeName(action.type);

			// Add progress percentage
			int progressPercent = static_cast<int>(action.progress() * 100.0F);
			oss << " (" << progressPercent << "%)";

			return oss.str();
		}

		// Format task description
		std::string formatTask(const ecs::Task& task) {
			if (!task.isActive()) {
				return "No task";
			}

			if (!task.reason.empty()) {
				return task.reason;
			}

			// Fallback to task type name
			switch (task.type) {
				case ecs::TaskType::None:
					return "None";
				case ecs::TaskType::FulfillNeed:
					return "Fulfilling need";
				case ecs::TaskType::Wander:
					return "Wandering";
			}
			return "Unknown";
		}

		// Format position for display
		std::string formatPosition(Foundation::Vec2 pos) {
			std::ostringstream oss;
			oss << std::fixed << std::setprecision(1);
			oss << "(" << pos.x << ", " << pos.y << ")";
			return oss.str();
		}

		// Y position to "hide" elements offscreen
		constexpr float kHiddenY = -10000.0F;
	} // namespace

	EntityInfoPanel::EntityInfoPanel(const Args& args)
		: panelWidth(args.width),
		  panelPosition(args.position),
		  onCloseCallback(args.onClose) {

		float contentWidth = panelWidth - (2.0F * kPadding);
		float yOffset = args.position.y + kPadding;

		// Calculate total panel height for colonist view (max height)
		float headerHeight = kHeaderFontSize + kSectionSpacing;
		float needsHeight = (kNeedBarHeight + kNeedBarSpacing) * kNeedCount;
		float statusHeight = kSectionSpacing + (kStatusFontSize + 4.0F) * 2.0F; // Task + Action
		panelHeight = kPadding + headerHeight + needsHeight + statusHeight + kPadding;

		// Add background panel (semi-transparent dark)
		backgroundHandle = addChild(
			UI::Rectangle(
				UI::Rectangle::Args{
					.position = args.position,
					.size = {panelWidth, panelHeight},
					.style =
						{.fill = Foundation::Color(0.1F, 0.1F, 0.15F, 0.85F),
						 .border = Foundation::BorderStyle{.color = Foundation::Color(0.3F, 0.3F, 0.4F, 1.0F), .width = 1.0F}},
					.zIndex = 0,
					.id = (args.id + "_bg").c_str()
				}
			)
		);

		// Add close button background [X] in top-right corner
		float closeX = args.position.x + panelWidth - kPadding - kCloseButtonSize;
		float closeY = args.position.y + kPadding;
		closeButtonBgHandle = addChild(
			UI::Rectangle(
				UI::Rectangle::Args{
					.position = {closeX, closeY},
					.size = {kCloseButtonSize, kCloseButtonSize},
					.style =
						{.fill = Foundation::Color(0.3F, 0.2F, 0.2F, 0.9F),
						 .border = Foundation::BorderStyle{.color = Foundation::Color(0.5F, 0.3F, 0.3F, 1.0F), .width = 1.0F}},
					.zIndex = 2,
					.id = (args.id + "_close_bg").c_str()
				}
			)
		);

		// Add close button text
		closeButtonTextHandle = addChild(
			UI::Text(
				UI::Text::Args{
					.position = {closeX + kCloseButtonSize * 0.5F, closeY + kCloseButtonSize * 0.5F - 1.0F},
					.text = "X",
					.style =
						{
							.color = Foundation::Color(0.9F, 0.6F, 0.6F, 1.0F),
							.fontSize = 10.0F,
							.hAlign = Foundation::HorizontalAlign::Center,
							.vAlign = Foundation::VerticalAlign::Middle,
						},
					.zIndex = 3,
					.id = (args.id + "_close_text").c_str()
				}
			)
		);

		// Add entity name header
		nameHandle = addChild(
			UI::Text(
				UI::Text::Args{
					.position = {args.position.x + kPadding, yOffset},
					.text = "Select Entity",
					.style =
						{
							.color = Foundation::Color(0.9F, 0.9F, 0.95F, 1.0F),
							.fontSize = kHeaderFontSize,
							.hAlign = Foundation::HorizontalAlign::Left,
							.vAlign = Foundation::VerticalAlign::Top,
						},
					.zIndex = 1,
					.id = (args.id + "_name").c_str()
				}
			)
		);

		yOffset += kHeaderFontSize + kSectionSpacing;

		// Store base Y offset for colonist-specific elements
		colonistContentY = yOffset;

		// Add need bars (colonist only)
		for (size_t i = 0; i < kNeedCount; ++i) {
			needBarHandles[i] = addChild(NeedBar(
				NeedBar::Args{
					.position = {args.position.x + kPadding, yOffset},
					.width = contentWidth,
					.height = kNeedBarHeight,
					.label = kNeedLabels[i],
					.id = args.id + "_need_" + std::to_string(i)
				}
			));

			yOffset += kNeedBarHeight + kNeedBarSpacing;
		}

		yOffset += kSectionSpacing;

		// Add task text (colonist only)
		taskHandle = addChild(
			UI::Text(
				UI::Text::Args{
					.position = {args.position.x + kPadding, yOffset},
					.text = "Task: None",
					.style =
						{
							.color = Foundation::Color(0.7F, 0.7F, 0.75F, 1.0F),
							.fontSize = kStatusFontSize,
							.hAlign = Foundation::HorizontalAlign::Left,
							.vAlign = Foundation::VerticalAlign::Top,
						},
					.zIndex = 1,
					.id = (args.id + "_task").c_str()
				}
			)
		);

		yOffset += kStatusFontSize + 4.0F;

		// Add action text (colonist only)
		actionHandle = addChild(
			UI::Text(
				UI::Text::Args{
					.position = {args.position.x + kPadding, yOffset},
					.text = "Action: Idle",
					.style =
						{
							.color = Foundation::Color(0.7F, 0.7F, 0.75F, 1.0F),
							.fontSize = kStatusFontSize,
							.hAlign = Foundation::HorizontalAlign::Left,
							.vAlign = Foundation::VerticalAlign::Top,
						},
					.zIndex = 1,
					.id = (args.id + "_action").c_str()
				}
			)
		);

		// World entity info elements (position below header)
		float worldInfoY = args.position.y + kPadding + kHeaderFontSize + kSectionSpacing;
		worldEntityContentY = worldInfoY;

		positionHandle = addChild(
			UI::Text(
				UI::Text::Args{
					.position = {args.position.x + kPadding, worldInfoY},
					.text = "Position: (0, 0)",
					.style =
						{
							.color = Foundation::Color(0.7F, 0.7F, 0.75F, 1.0F),
							.fontSize = kStatusFontSize,
							.hAlign = Foundation::HorizontalAlign::Left,
							.vAlign = Foundation::VerticalAlign::Top,
						},
					.zIndex = 1,
					.id = (args.id + "_position").c_str()
				}
			)
		);

		worldInfoY += kStatusFontSize + kSectionSpacing;

		capabilitiesHeaderHandle = addChild(
			UI::Text(
				UI::Text::Args{
					.position = {args.position.x + kPadding, worldInfoY},
					.text = "Capabilities:",
					.style =
						{
							.color = Foundation::Color(0.8F, 0.8F, 0.85F, 1.0F),
							.fontSize = kStatusFontSize,
							.hAlign = Foundation::HorizontalAlign::Left,
							.vAlign = Foundation::VerticalAlign::Top,
						},
					.zIndex = 1,
					.id = (args.id + "_cap_header").c_str()
				}
			)
		);

		worldInfoY += kStatusFontSize + 2.0F;

		// Capability text elements
		for (size_t i = 0; i < capabilityHandles.size(); ++i) {
			capabilityHandles[i] = addChild(
				UI::Text(
					UI::Text::Args{
						.position = {args.position.x + kPadding + 8.0F, worldInfoY + static_cast<float>(i) * (kStatusFontSize + 2.0F)},
						.text = "",
						.style =
							{
								.color = Foundation::Color(0.6F, 0.8F, 0.6F, 1.0F),
								.fontSize = kStatusFontSize,
								.hAlign = Foundation::HorizontalAlign::Left,
								.vAlign = Foundation::VerticalAlign::Top,
							},
						.zIndex = 1,
						.id = (args.id + "_cap_" + std::to_string(i)).c_str()
					}
				)
			);
		}

		// Start hidden (no selection) - position elements offscreen
		hideAllElements();

		// IMPORTANT: Disable child sorting to preserve LayerHandle indices.
		// Component::render() sorts children by zIndex, which invalidates all handles.
		// We rely on insertion order for rendering instead.
		childrenNeedSorting = false;
	}

	void EntityInfoPanel::update(const ecs::World& world, const engine::assets::AssetRegistry& registry, const Selection& selection) {
		// Handle close button click
		auto& input = engine::InputManager::Get();
		if (visible && input.isMouseButtonReleased(engine::MouseButton::Left)) {
			auto mousePos = input.getMousePosition();

			// Check if click is within close button bounds
			float closeX = panelPosition.x + panelWidth - kPadding - kCloseButtonSize;
			float closeY = panelPosition.y + kPadding;

			if (mousePos.x >= closeX && mousePos.x <= closeX + kCloseButtonSize && mousePos.y >= closeY &&
				mousePos.y <= closeY + kCloseButtonSize) {
				if (onCloseCallback) {
					onCloseCallback();
				}
				return;
			}
		}

		// Handle selection type
		std::visit(
			[this, &world, &registry](auto&& sel) {
				using T = std::decay_t<decltype(sel)>;
				if constexpr (std::is_same_v<T, NoSelection>) {
					// Hide panel
					visible = false;
					hideAllElements();
				} else if constexpr (std::is_same_v<T, ColonistSelection>) {
					visible = true;
					showColonistUI();
					updateColonistDisplay(world, sel.entityId);
				} else if constexpr (std::is_same_v<T, WorldEntitySelection>) {
					visible = true;
					showWorldEntityUI();
					updateWorldEntityDisplay(registry, sel);
				}
			},
			selection
		);
	}

	void EntityInfoPanel::updateColonistDisplay(const ecs::World& world, ecs::EntityID entityId) {
		if (entityId == 0) {
			return;
		}

		// Get colonist name
		if (auto* colonist = world.getComponent<ecs::Colonist>(entityId)) {
			if (auto* nameText = getChild<UI::Text>(nameHandle)) {
				nameText->text = colonist->name;
			}
		}

		// Get needs and update bars
		if (auto* needs = world.getComponent<ecs::NeedsComponent>(entityId)) {
			for (size_t i = 0; i < kNeedCount; ++i) {
				auto needType = static_cast<ecs::NeedType>(i);
				if (auto* needBar = getChild<NeedBar>(needBarHandles[i])) {
					needBar->setValue(needs->get(needType).value);
				}
			}
		}

		// Get task status
		if (auto* task = world.getComponent<ecs::Task>(entityId)) {
			if (auto* taskText = getChild<UI::Text>(taskHandle)) {
				taskText->text = "Task: " + formatTask(*task);
			}
		}

		// Get action status
		if (auto* action = world.getComponent<ecs::Action>(entityId)) {
			if (auto* actionText = getChild<UI::Text>(actionHandle)) {
				actionText->text = "Action: " + formatAction(*action);
			}
		}
	}

	void EntityInfoPanel::updateWorldEntityDisplay(const engine::assets::AssetRegistry& registry, const WorldEntitySelection& sel) {
		// Set entity name (defName)
		if (auto* nameText = getChild<UI::Text>(nameHandle)) {
			nameText->text = sel.defName;
		}

		// Set position
		if (auto* posText = getChild<UI::Text>(positionHandle)) {
			posText->text = "Position: " + formatPosition(sel.position);
		}

		// Look up asset definition for capabilities
		const auto* def = registry.getDefinition(sel.defName);
		if (def == nullptr) {
			// No definition found - clear capabilities
			for (auto& capHandle : capabilityHandles) {
				if (auto* capText = getChild<UI::Text>(capHandle)) {
					capText->text = "";
				}
			}
			return;
		}

		// Build capability list
		std::vector<std::string> caps;
		const auto&				 capabilities = def->capabilities;

		if (capabilities.edible.has_value()) {
			std::ostringstream oss;
			oss << "Edible (nutrition: " << std::fixed << std::setprecision(1) << capabilities.edible->nutrition << ")";
			caps.push_back(oss.str());
		}
		if (capabilities.drinkable.has_value()) {
			caps.push_back("Drinkable");
		}
		if (capabilities.sleepable.has_value()) {
			std::ostringstream oss;
			oss << "Sleepable (recovery: " << std::fixed << std::setprecision(1) << capabilities.sleepable->recoveryMultiplier << "x)";
			caps.push_back(oss.str());
		}
		if (capabilities.toilet.has_value()) {
			caps.push_back("Toilet");
		}

		// Update capability text elements
		for (size_t i = 0; i < capabilityHandles.size(); ++i) {
			if (auto* capText = getChild<UI::Text>(capabilityHandles[i])) {
				if (i < caps.size()) {
					capText->text = "- " + caps[i];
				} else {
					capText->text = "";
				}
			}
		}
	}

	void EntityInfoPanel::hideAllElements() {
		// Position all elements offscreen by setting their Y to kHiddenY
		if (auto* bg = getChild<UI::Rectangle>(backgroundHandle)) {
			bg->position.y = kHiddenY;
		}
		if (auto* closeBg = getChild<UI::Rectangle>(closeButtonBgHandle)) {
			closeBg->position.y = kHiddenY;
		}
		if (auto* closeText = getChild<UI::Text>(closeButtonTextHandle)) {
			closeText->position.y = kHiddenY;
		}
		if (auto* name = getChild<UI::Text>(nameHandle)) {
			name->position.y = kHiddenY;
		}

		// Colonist-specific
		for (auto& handle : needBarHandles) {
			if (auto* bar = getChild<NeedBar>(handle)) {
				bar->setPosition({panelPosition.x + kPadding, kHiddenY});
			}
		}
		if (auto* task = getChild<UI::Text>(taskHandle)) {
			task->position.y = kHiddenY;
		}
		if (auto* action = getChild<UI::Text>(actionHandle)) {
			action->position.y = kHiddenY;
		}

		// World entity-specific
		if (auto* pos = getChild<UI::Text>(positionHandle)) {
			pos->position.y = kHiddenY;
		}
		if (auto* capHeader = getChild<UI::Text>(capabilitiesHeaderHandle)) {
			capHeader->position.y = kHiddenY;
		}
		for (auto& handle : capabilityHandles) {
			if (auto* cap = getChild<UI::Text>(handle)) {
				cap->position.y = kHiddenY;
			}
		}
	}

	void EntityInfoPanel::showColonistUI() {
		// Start by hiding everything, then show only what we need
		hideAllElements();

		// Reset text content to prevent stale data from previous selection type
		if (auto* nameText = getChild<UI::Text>(nameHandle)) {
			nameText->text = "Colonist";
		}

		// Show common elements at proper positions
		if (auto* bg = getChild<UI::Rectangle>(backgroundHandle)) {
			bg->position.y = panelPosition.y;
		}
		float closeY = panelPosition.y + kPadding;
		if (auto* closeBg = getChild<UI::Rectangle>(closeButtonBgHandle)) {
			closeBg->position.y = closeY;
		}
		if (auto* closeText = getChild<UI::Text>(closeButtonTextHandle)) {
			closeText->position.y = closeY + kCloseButtonSize * 0.5F - 1.0F;
		}
		if (auto* name = getChild<UI::Text>(nameHandle)) {
			name->position.y = panelPosition.y + kPadding;
		}

		// Show colonist-specific at proper positions
		float yOffset = colonistContentY;
		for (size_t i = 0; i < needBarHandles.size(); ++i) {
			if (auto* bar = getChild<NeedBar>(needBarHandles[i])) {
				bar->setPosition({panelPosition.x + kPadding, yOffset});
			}
			yOffset += kNeedBarHeight + kNeedBarSpacing;
		}
		yOffset += kSectionSpacing;
		if (auto* task = getChild<UI::Text>(taskHandle)) {
			task->position.y = yOffset;
		}
		yOffset += kStatusFontSize + 4.0F;
		if (auto* action = getChild<UI::Text>(actionHandle)) {
			action->position.y = yOffset;
		}

		// Hide world entity-specific (move offscreen)
		if (auto* pos = getChild<UI::Text>(positionHandle)) {
			pos->position.y = kHiddenY;
		}
		if (auto* capHeader = getChild<UI::Text>(capabilitiesHeaderHandle)) {
			capHeader->position.y = kHiddenY;
		}
		for (auto& handle : capabilityHandles) {
			if (auto* cap = getChild<UI::Text>(handle)) {
				cap->position.y = kHiddenY;
			}
		}
	}

	void EntityInfoPanel::showWorldEntityUI() {
		// Start by hiding everything, then show only what we need
		hideAllElements();

		// Reset text content to prevent stale data from previous selection type
		if (auto* nameText = getChild<UI::Text>(nameHandle)) {
			nameText->text = "Entity";
		}

		// Show common elements at proper positions
		if (auto* bg = getChild<UI::Rectangle>(backgroundHandle)) {
			bg->position.y = panelPosition.y;
		}
		float closeY = panelPosition.y + kPadding;
		if (auto* closeBg = getChild<UI::Rectangle>(closeButtonBgHandle)) {
			closeBg->position.y = closeY;
		}
		if (auto* closeText = getChild<UI::Text>(closeButtonTextHandle)) {
			closeText->position.y = closeY + kCloseButtonSize * 0.5F - 1.0F;
		}
		if (auto* name = getChild<UI::Text>(nameHandle)) {
			name->position.y = panelPosition.y + kPadding;
		}

		// Hide colonist-specific (move offscreen)
		for (auto& handle : needBarHandles) {
			if (auto* bar = getChild<NeedBar>(handle)) {
				bar->setPosition({panelPosition.x + kPadding, kHiddenY});
			}
		}
		if (auto* task = getChild<UI::Text>(taskHandle)) {
			task->position.y = kHiddenY;
		}
		if (auto* action = getChild<UI::Text>(actionHandle)) {
			action->position.y = kHiddenY;
		}

		// Show world entity-specific at proper positions
		float yOffset = worldEntityContentY;
		if (auto* pos = getChild<UI::Text>(positionHandle)) {
			pos->position.y = yOffset;
		}
		yOffset += kStatusFontSize + kSectionSpacing;
		if (auto* capHeader = getChild<UI::Text>(capabilitiesHeaderHandle)) {
			capHeader->position.y = yOffset;
		}
		yOffset += kStatusFontSize + 2.0F;
		for (size_t i = 0; i < capabilityHandles.size(); ++i) {
			if (auto* cap = getChild<UI::Text>(capabilityHandles[i])) {
				cap->position.y = yOffset + static_cast<float>(i) * (kStatusFontSize + 2.0F);
			}
		}
	}

} // namespace world_sim

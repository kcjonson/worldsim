#include "StorageConfigDialog.h"

#include "scenes/shared/SalalUi.h"

#include <ui/Context.h>
#include <ui/components/Button.h>
#include <ui/components/ListRow.h>

#include <algorithm>
#include <string>
#include <vector>

namespace world_sim {

	namespace {

		constexpr float kDialogWidth = 720.0F;
		constexpr float kDialogHeight = 480.0F;
		constexpr float kColumnGap = 12.0F;
		constexpr float kLeftColumnWidth = 200.0F;
		constexpr float kRightColumnWidth = 200.0F;
		constexpr float kIndentWidth = 16.0F;
		constexpr float kFieldLabelWidth = 80.0F;
		constexpr float kSelectWidth = 120.0F;
		constexpr float kAmountWidth = 80.0F;
		constexpr float kDeleteSize = 24.0F;
		constexpr float kSectionGap = 6.0F;

		ui::Style heading(const ui::Theme& theme) {
			return world_sim::text(theme, {.size = tokens::fontSize::fsXs, .color = tokens::color::textDim});
		}

		ui::Style body(const ui::Theme& theme, ui::ColorToken color = tokens::color::text) {
			return world_sim::text(theme, {.size = tokens::fontSize::fsSm, .color = color});
		}

		ui::Style smallPrint(const ui::Theme& theme, ui::ColorToken color) {
			return world_sim::text(theme, {.size = tokens::fontSize::fs2xs, .color = color});
		}

		std::string priorityValue(ecs::StoragePriority priority) {
			switch (priority) {
				case ecs::StoragePriority::Critical:
					return "critical";
				case ecs::StoragePriority::High:
					return "high";
				case ecs::StoragePriority::Low:
					return "low";
				default:
					return "medium";
			}
		}

		ecs::StoragePriority priorityOf(const std::string& value) {
			if (value == "critical") {
				return ecs::StoragePriority::Critical;
			}
			if (value == "high") {
				return ecs::StoragePriority::High;
			}
			if (value == "low") {
				return ecs::StoragePriority::Low;
			}
			return ecs::StoragePriority::Medium;
		}

		uint32_t amountOf(const std::string& text) {
			try {
				return static_cast<uint32_t>(std::stoul(text));
			} catch (...) {
				return 0;
			}
		}

		// A row of the item list: a category's header, or one of its items.
		struct ItemLine {
			std::string key;
			std::string label;
			std::string trailing;
			bool		selected = false;
			bool		dim = false;
			float		indent = 0.0F;
			int			category = -1; // a header's category index
			std::string defName;	   // an item's
		};

		// One rule: its summary, priority and amounts, and for a specific item whether the colony knows a
		// source, beside a delete.
		class RuleRow : public ui::Component {
		  public:
			struct Args {
				UI_COMMON_ARGS
			};

			[[nodiscard]] const char* typeName() const override { return "RuleRow"; }

			void setRule(const DisplayRule& rule, std::function<void()> onRemove) {
				const ui::Theme& theme = getContext()->getTheme();
				summary.get()->setText(rule.isWildcard ? "* " + rule.label : rule.label);
				std::string amounts = ecs::storagePriorityToString(rule.priority);
				if (rule.minAmount > 0) {
					amounts += ", Min: " + std::to_string(rule.minAmount);
				}
				amounts += rule.maxAmount > 0 ? ", Max: " + std::to_string(rule.maxAmount) : std::string(", Unlimited");
				details.get()->setText(amounts);
				// A wildcard names no single item to source, so it shows nothing.
				source.get()->setVisible(!rule.isWildcard);
				if (!rule.isWildcard && known != rule.knownSource) {
					known = rule.knownSource;
					source.get()->setText(known ? "Source known [OK]" : "No known source [X]");
					source.get()->setStyle(smallPrint(theme, known ? tokens::color::statusOk : tokens::color::statusCrit));
				}
				remove.get()->setOnPress(std::move(onRemove));
			}

		  protected:
			void onMount() override {
				const ui::Theme& theme = getContext()->getTheme();
				ui::Layout		 row = getLayout();
				row.display = ui::Display::Flex;
				row.gap = 4.0F;
				row.alignItems = ui::AlignItems::Start;
				setLayout(row);
				ui::Ref<ui::Component> text = addChild<ui::Component>({
					.size = {0.0F, ui::kAuto},
					.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 1.0F, .flexGrow = 1.0F},
				});
				summary = text.addChild<ui::Text>({.style = body(theme), .wrap = true});
				details = text.addChild<ui::Text>({.style = smallPrint(theme, tokens::color::textDim), .wrap = true});
				source = text.addChild<ui::Text>({.style = smallPrint(theme, tokens::color::statusCrit), .text = "No known source [X]"});
				remove = addChild<ui::Button>({.size = {kDeleteSize, kDeleteSize}, .icon = "close", .controlSize = ui::ControlSize::Sm});
			}

		  private:
			ui::Ref<ui::Text>	summary;
			ui::Ref<ui::Text>	details;
			ui::Ref<ui::Text>	source;
			ui::Ref<ui::Button> remove;
			bool				known = false;
		};

		ui::Ref<ui::Component> fieldRow(const ui::Ref<ui::Component>& parent, const ui::Theme& theme, const char* label) {
			ui::Ref<ui::Component> row = parent.addChild<ui::Component>(
				{.layout = {.display = ui::Display::Flex, .gap = 4.0F, .alignItems = ui::AlignItems::Center}}
			);
			row.addChild<ui::Text>({.size = {kFieldLabelWidth, ui::kAuto}, .style = body(theme), .text = label});
			return row;
		}

	} // namespace

	StorageConfigDialog::StorageConfigDialog(const Args& args)
		: onClose(args.onClose),
		  onNotify(args.onNotify) {}

	void StorageConfigDialog::open(ui::Component& opener, ecs::EntityID containerId, const std::string& containerDefName, int player) {
		model.setContainer(containerId, containerDefName);
		// Every category opens expanded, once the next refresh has them.
		expandedCategories.clear();
		fresh = true;
		const std::string title = model.containerName() + " - Storage Settings";
		if (ui::Dialog* shown = dialog.get()) {
			shown->setTitle(title);
			return;
		}
		dialog = ui::Dialog::open(
			opener,
			{
				.title = title,
				.panelSize = {kDialogWidth, kDialogHeight},
				.onClose =
					[this] {
						closed();
						if (onClose) {
							onClose();
						}
					},
			},
			player
		);
		if (ui::Dialog* shown = dialog.get()) {
			build(*shown);
		}
	}

	void StorageConfigDialog::close() {
		if (ui::Dialog* shown = dialog.get()) {
			ui::Context&			  context = *shown->getContext();
			const ui::Ref<ui::Dialog> entry = dialog;
			closed();
			context.closeOverlay(entry);
		}
	}

	void StorageConfigDialog::closed() {
		dialog = {};
		model.clear();
		world = nullptr;
	}

	void StorageConfigDialog::build(ui::Dialog& shown) {
		const ui::Theme&			 theme = shown.getContext()->getTheme();
		const ui::Ref<ui::Component> columns = shown.content().addChild<ui::Component>({
			.size = {ui::kAuto, 0.0F},
			.layout = {.display = ui::Display::Flex, .gap = kColumnGap, .flexGrow = 1.0F},
		});
		const ui::Layout			 stack{.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 4.0F};

		// The items, by category.
		itemList = columns.addChild<ui::Component>({
			.size = {kLeftColumnWidth, ui::kAuto},
			.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column},
			.overflow = ui::Overflow::Scroll,
		});

		// The new rule's form, under the bulk actions.
		ui::Layout centreLayout = stack;
		centreLayout.flexGrow = 1.0F;
		const ui::Ref<ui::Component> centre =
			columns.addChild<ui::Component>({.size = {0.0F, ui::kAuto}, .layout = centreLayout, .overflow = ui::Overflow::Scroll});
		const ui::Ref<ui::Component> bulk =
			centre.addChild<ui::Component>({.layout = {.display = ui::Display::Flex, .gap = 4.0F, .alignItems = ui::AlignItems::Center}});
		bulk.addChild<ui::Button>({.label = "Select All", .controlSize = ui::ControlSize::Sm, .onPress = [this] { selectAll(); }});
		bulk.addChild<ui::Button>({.label = "None", .controlSize = ui::ControlSize::Sm, .onPress = [this] { selectNone(); }});
		noSelection = centre.addChild<ui::Text>(
			{.margin = {kSectionGap, 0.0F, 0.0F, 0.0F},
			 .style = world_sim::text(theme, {.size = tokens::fontSize::fsBase, .color = tokens::color::textDim}),
			 .text = "Select an item to configure"}
		);
		form = centre.addChild<ui::Component>({.visible = false, .layout = stack});
		itemName = form.addChild<ui::Text>(
			{.margin = {kSectionGap, 0.0F, 0.0F, 0.0F},
			 .style = world_sim::text(theme, {.size = tokens::fontSize::fsMd, .color = tokens::color::textBright})}
		);
		form.addChild<ui::Text>({.margin = {kSectionGap, 0.0F, 0.0F, 0.0F}, .style = heading(theme), .text = "ADD RULE"});
		priority = fieldRow(form, theme, "Priority:")
					   .addChild<ui::Select>({
						   .size = {kSelectWidth, ui::kAuto},
						   .options =
							   {
								   {.label = "Critical", .value = "critical"},
								   {.label = "High", .value = "high"},
								   {.label = "Medium", .value = "medium"},
								   {.label = "Low", .value = "low"},
							   },
						   .value = "medium",
						   .onChange = [this](const std::string& value) { model.setPriority(priorityOf(value)); },
					   });
		minAmount = fieldRow(form, theme, "Min:")
						.addChild<ui::TextInput>({
							.size = {kAmountWidth, ui::kAuto},
							.text = "0",
							.placeholder = "0",
							.onChange = [this](const std::string& value) { model.setMinAmount(amountOf(value)); },
						});
		const ui::Ref<ui::Component> maxRow = fieldRow(form, theme, "Max:");
		maxAmount = maxRow.addChild<ui::TextInput>({
			.size = {kAmountWidth, ui::kAuto},
			.text = "0",
			.placeholder = "0",
			.onChange = [this](const std::string& value) { model.setMaxAmount(amountOf(value)); },
		});
		unlimited = maxRow.addChild<ui::Checkbox>({
			.label = "Unlimited",
			.checked = true,
			.onChange = [this](bool checked) {
				model.setUnlimited(checked);
				if (ui::TextInput* input = maxAmount.get()) {
					input->setEnabled(!checked);
				}
			},
		});
		// Quality isn't simulated yet, so each offers only Any.
		for (const char* label : {"Min Quality:", "Max Quality:"}) {
			form.addChild<ui::Text>(
				{.margin = {kSectionGap, 0.0F, 0.0F, 0.0F}, .style = body(theme, tokens::color::textDim), .text = label}
			);
			form.addChild<ui::Select>({
				.size = {kSelectWidth, ui::kAuto},
				.layout = {.alignSelf = ui::AlignItems::Start},
				.options = {{.label = "Any", .value = "any"}},
				.value = "any",
			});
		}
		const ui::Ref<ui::Component> actions = form.addChild<ui::Component>({
			.margin = {kSectionGap, 0.0F, 0.0F, 0.0F},
			.layout = {.display = ui::Display::Flex, .gap = 8.0F, .alignItems = ui::AlignItems::Center},
		});
		actions.addChild<ui::Button>({.label = "Add Rule", .variant = ui::ButtonVariant::Primary, .onPress = [this] { addRule(); }});
		actions.addChild<ui::Button>({.label = "Add All", .onPress = [this] { addAll(); }});

		// The selected item's rules.
		const ui::Ref<ui::Component> right =
			columns.addChild<ui::Component>({.size = {kRightColumnWidth, ui::kAuto}, .layout = stack, .overflow = ui::Overflow::Scroll});
		rulesHeader = right.addChild<ui::Text>({.style = heading(theme), .text = "RULES"});
		rulesStatus = right.addChild<ui::Text>({.style = body(theme, tokens::color::textDim), .text = "Select an item"});
		ruleList = right.addChild<ui::Component>(
			{.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 2.0F}}
		);
	}

	void StorageConfigDialog::update(ecs::World& newWorld, const assets::AssetRegistry& registry) {
		if (!isOpen()) {
			return;
		}
		world = &newWorld;
		const StorageConfigDialogModel::UpdateType change = model.refresh(newWorld, registry);

		// A toast once per (container, item) whose specific-item rule just lost its known source; the
		// model de-dupes.
		if (onNotify) {
			for (const std::string& defName : model.newlyUnknownSourceItems()) {
				const auto*		  def = registry.getDefinition(defName);
				const std::string label = (def != nullptr && !def->label.empty()) ? def->label : defName;
				onNotify("No known source", label + " - no colonist has seen a source. Take direct control to fetch it.");
			}
		}

		if (fresh) {
			fresh = false;
			for (std::size_t i = 0; i < model.categoryGroups().size(); ++i) {
				expandedCategories.push_back(static_cast<int>(i));
			}
			showItems();
			showForm();
			showRules();
			return;
		}
		using UpdateType = StorageConfigDialogModel::UpdateType;
		const bool rulesMoved = change == UpdateType::Rules || change == UpdateType::Full || stale;
		if (rulesMoved || change == UpdateType::Inventory) {
			showItems();
		}
		if (change == UpdateType::Full) {
			showForm();
		}
		if (rulesMoved || change == UpdateType::Selection) {
			showRules();
		}
		stale = false;
	}

	void StorageConfigDialog::showItems() {
		ui::Component* list = itemList.get();
		if (list == nullptr) {
			return;
		}
		const auto&			  groups = model.categoryGroups();
		const auto&			  items = model.availableItems();
		std::vector<ItemLine> lines;
		for (std::size_t group = 0; group < groups.size(); ++group) {
			const int  index = static_cast<int>(group);
			const bool expanded = std::ranges::find(expandedCategories, index) != expandedCategories.end();
			lines.push_back({
				.key = "category:" + std::to_string(group),
				.label = (expanded ? "v " : "> ") + groups[group].label,
				.category = index,
			});
			if (!expanded) {
				continue;
			}
			for (const std::size_t itemIndex : groups[group].itemIndices) {
				const AvailableItem& item = items[itemIndex];
				std::string			 count = std::to_string(item.currentCount) + "/";
				if (!item.hasRules) {
					count += "0";
				} else {
					count += item.requestedCount == 0 ? "~" : std::to_string(item.requestedCount);
				}
				lines.push_back({
					.key = "item:" + item.defName,
					.label = item.label,
					.trailing = count,
					.selected = item.defName == model.selectedItemDefName(),
					.dim = !item.hasRules,
					.indent = kIndentWidth,
					.defName = item.defName,
				});
			}
		}
		const auto pressed = [this](const ItemLine& line) -> std::function<void()> {
			if (line.category >= 0) {
				return [this, category = line.category] {
					toggleCategory(category);
				};
			}
			return [this, defName = line.defName] {
				selectItem(defName);
			};
		};
		list->reconcileChildren(
			lines,
			[](const ItemLine& line) { return line.key; },
			[&](const ItemLine& line) -> ui::Ref<ui::Component> {
				return list->addChild<ui::ListRow>({
					.label = line.label,
					.trailing = line.trailing,
					.selected = line.selected,
					.dim = line.dim,
					.indent = line.indent,
					.onPress = pressed(line),
				});
			},
			[](ui::Component& child, const ItemLine& line) {
				auto& row = static_cast<ui::ListRow&>(child);
				row.setLabel(line.label);
				row.setTrailing(line.trailing);
				row.setSelected(line.selected);
				row.setDim(line.dim);
			}
		);
	}

	void StorageConfigDialog::showForm() {
		if (form.get() == nullptr) {
			return;
		}
		const AvailableItem* item = model.selectedItemData();
		noSelection.get()->setVisible(item == nullptr);
		form.get()->setVisible(item != nullptr);
		if (item == nullptr) {
			return;
		}
		itemName.get()->setText(item->label);
		priority.get()->setValue(priorityValue(model.pendingRulePriority()));
		minAmount.get()->setText(std::to_string(model.pendingRuleMinAmount()));
		maxAmount.get()->setText(std::to_string(model.pendingRuleMaxAmount()));
		maxAmount.get()->setEnabled(!model.pendingRuleUnlimited());
		unlimited.get()->setChecked(model.pendingRuleUnlimited());
	}

	void StorageConfigDialog::showRules() {
		ui::Component* list = ruleList.get();
		if (list == nullptr) {
			return;
		}
		const AvailableItem*			item = model.selectedItemData();
		const std::vector<DisplayRule>	none;
		const std::vector<DisplayRule>& rules = item != nullptr ? model.rulesForSelectedItem() : none;
		rulesHeader.get()->setText(item != nullptr ? item->label + " RULES" : std::string("RULES"));
		rulesStatus.get()->setVisible(rules.empty());
		rulesStatus.get()->setText(item != nullptr ? "No rules configured" : "Select an item");
		list->reconcileChildren(
			rules,
			[](const DisplayRule& rule) { return std::to_string(rule.ruleIndex); },
			[&](const DisplayRule& rule) -> ui::Ref<ui::Component> {
				ui::Ref<RuleRow> row = list->addChild<RuleRow>({});
				row.get()->setRule(rule, [this, index = rule.ruleIndex] { removeRule(index); });
				return row;
			},
			[this](ui::Component& child, const DisplayRule& rule) {
				static_cast<RuleRow&>(child).setRule(rule, [this, index = rule.ruleIndex] { removeRule(index); });
			}
		);
	}

	void StorageConfigDialog::toggleCategory(int categoryIndex) {
		if (const auto open = std::ranges::find(expandedCategories, categoryIndex); open != expandedCategories.end()) {
			expandedCategories.erase(open);
		} else {
			expandedCategories.push_back(categoryIndex);
		}
		showItems();
	}

	void StorageConfigDialog::selectItem(const std::string& defName) {
		model.selectItem(defName);
		showItems();
		showForm();
		stale = true;
	}

	void StorageConfigDialog::addRule() {
		if (world != nullptr && model.addRule(*world)) {
			stale = true;
		}
	}

	void StorageConfigDialog::addAll() {
		if (world != nullptr && model.addCategoryWildcard(*world)) {
			stale = true;
		}
	}

	void StorageConfigDialog::removeRule(size_t ruleIndex) {
		if (world != nullptr) {
			model.removeRule(*world, ruleIndex);
			stale = true;
		}
	}

	void StorageConfigDialog::selectAll() {
		if (world != nullptr) {
			model.addAllCategories(*world);
			stale = true;
		}
	}

	void StorageConfigDialog::selectNone() {
		if (world != nullptr) {
			model.removeAllRules(*world);
			stale = true;
		}
	}

} // namespace world_sim

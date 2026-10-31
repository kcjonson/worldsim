#include "CraftingDialog.h"

#include "scenes/game/ui/Look.h"

#include <ui/Context.h>
#include <ui/components/ListRow.h>
#include <ui/components/ProgressBar.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace world_sim {

	namespace {

		constexpr float kDialogWidth = 620.0F;
		constexpr float kDialogHeight = 450.0F;
		constexpr float kColumnGap = 12.0F;
		constexpr float kLeftColumnWidth = 160.0F;
		constexpr float kRightColumnWidth = 180.0F;
		constexpr float kQuantityWidth = 40.0F;
		constexpr float kSectionGap = 6.0F;
		constexpr float kPickerGap = 8.0F;

		ui::Style heading(const ui::Theme& theme) {
			return look::text(theme, look::t::color::textDim, look::t::fontSize::fsXs);
		}

		ui::Style body(const ui::Theme& theme, ui::ColorToken color = look::t::color::text) {
			return look::text(theme, color, look::t::fontSize::fsSm);
		}

		ui::Ref<ui::Component> column(const ui::Ref<ui::Component>& parent, float gap, ui::Edges margin = {}) {
			return parent.addChild<ui::Component>(
				{.margin = margin, .layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = gap}}
			);
		}

		// A job in the queue: its label and remaining count, over its progress while it runs or a Cancel
		// while it waits.
		class QueueEntry : public ui::Component {
		  public:
			struct Args {
				UI_COMMON_ARGS
				QueuedJobItem							job;
				std::function<void(const std::string&)> onCancel;
			};

			explicit QueueEntry(const Args& args)
				: job(args.job),
				  onCancel(args.onCancel) {}

			[[nodiscard]] const char* typeName() const override { return "QueueEntry"; }

			void setJob(const QueuedJobItem& value) {
				job = value;
				show();
			}

		  protected:
			void onMount() override {
				const ui::Theme& theme = getContext()->getTheme();
				ui::Layout		 layout = getLayout();
				layout.display = ui::Display::Flex;
				layout.flexDirection = ui::FlexDirection::Column;
				layout.gap = 2.0F;
				setLayout(layout);
				label = addChild<ui::Text>({.style = body(theme)});
				if (job.isInProgress) {
					progress = addChild<ui::ProgressBar>({.tone = ui::Tone::Info, .controlSize = ui::ControlSize::Sm});
				} else {
					addChild<ui::Button>({
						.layout = {.alignSelf = ui::AlignItems::Start},
						.label = "Cancel",
						.controlSize = ui::ControlSize::Sm,
						.onPress = [this] {
							if (onCancel) {
								onCancel(job.recipeDefName);
							}
						},
					});
				}
				show();
			}

		  private:
			void show() {
				if (label.get() == nullptr) {
					return;
				}
				const uint32_t remaining = job.quantity - job.completed;
				label.get()->setText(remaining > 1 ? job.label + " x" + std::to_string(remaining) : job.label);
				if (ui::ProgressBar* bar = progress.get()) {
					bar->setValue(job.progress);
				}
			}

			QueuedJobItem							job;
			std::function<void(const std::string&)> onCancel;
			ui::Ref<ui::Text>						label;
			ui::Ref<ui::ProgressBar>				progress;
		};

		// A line of the queue column: a section heading, or a job.
		struct QueueLine {
			std::string			 key;
			std::string			 heading;
			const QueuedJobItem* job = nullptr;
		};

	} // namespace

	CraftingDialog::CraftingDialog(const Args& args)
		: onClose(args.onClose),
		  onQueueRecipe(args.onQueueRecipe),
		  onCancelJob(args.onCancelJob) {}

	void CraftingDialog::open(ui::Component& opener, ecs::EntityID stationId, const std::string& stationDefName, int player) {
		model.setStation(stationId, stationDefName);
		fresh = true;
		const std::string title = "Crafting - " + model.stationName();
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

	void CraftingDialog::close() {
		if (ui::Dialog* shown = dialog.get()) {
			ui::Context&			  context = *shown->getContext();
			const ui::Ref<ui::Dialog> entry = dialog;
			closed();
			context.closeOverlay(entry);
		}
	}

	void CraftingDialog::closed() {
		dialog = {};
		model.clear();
	}

	void CraftingDialog::build(ui::Dialog& shown) {
		const ui::Theme&			 theme = shown.getContext()->getTheme();
		const ui::Ref<ui::Component> columns = shown.content().addChild<ui::Component>({
			.size = {ui::kAuto, 0.0F},
			.layout = {.display = ui::Display::Flex, .gap = kColumnGap, .flexGrow = 1.0F},
		});
		const ui::Layout			 scrolling{.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 4.0F};

		// The recipes.
		const ui::Ref<ui::Component> left =
			columns.addChild<ui::Component>({.size = {kLeftColumnWidth, ui::kAuto}, .layout = scrolling, .overflow = ui::Overflow::Scroll});
		left.addChild<ui::Text>({.style = heading(theme), .text = "RECIPES"});
		recipeList = column(left, 0.0F);

		// The selected recipe.
		ui::Layout centreLayout = scrolling;
		centreLayout.flexGrow = 1.0F;
		const ui::Ref<ui::Component> centre =
			columns.addChild<ui::Component>({.size = {0.0F, ui::kAuto}, .layout = centreLayout, .overflow = ui::Overflow::Scroll});
		noSelection = centre.addChild<ui::Text>(
			{.style = look::text(theme, look::t::color::textDim, look::t::fontSize::fsBase), .text = "Select a recipe"}
		);
		details = column(centre, 2.0F);
		recipeName =
			details.addChild<ui::Text>({.style = look::text(theme, look::t::color::textBright, look::t::fontSize::fsMd), .wrap = true});
		description = details.addChild<ui::Text>({.style = body(theme), .wrap = true});
		requiresSection = column(details, 1.0F, {kSectionGap, 0.0F, 0.0F, 0.0F});
		requiresSection.addChild<ui::Text>({.style = heading(theme), .text = "REQUIRES"});
		materials = column(requiresSection, 1.0F);
		producesSection = column(details, 1.0F, {kSectionGap, 0.0F, 0.0F, 0.0F});
		producesSection.addChild<ui::Text>({.style = heading(theme), .text = "PRODUCES"});
		outputs = column(producesSection, 1.0F);
		details.addChild<ui::Text>({.margin = {kSectionGap, 0.0F, 0.0F, 0.0F}, .style = heading(theme), .text = "WORK TIME"});
		workTime = details.addChild<ui::Text>({.style = body(theme)});

		const ui::Ref<ui::Component> picker = details.addChild<ui::Component>({
			.margin = {kPickerGap, 0.0F, 0.0F, 0.0F},
			.layout = {.display = ui::Display::Flex, .gap = 4.0F, .alignItems = ui::AlignItems::Center},
		});
		const auto					 step = [&](const char* label, int delta) {
			  return picker.addChild<ui::Button>({.label = label, .controlSize = ui::ControlSize::Sm, .onPress = [this, delta] {
													  changeQuantity(delta);
												  }});
		};
		minusTen = step("-10", -10);
		minusOne = step("-1", -1);
		quantity = picker.addChild<ui::Text>({
			.size = {kQuantityWidth, ui::kAuto},
			.style =
				look::text(theme, look::t::color::text, look::t::fontSize::fsMd).extend({.textAlign = Foundation::HorizontalAlign::Center}),
		});
		step("+1", 1);
		step("+10", 10);
		details.addChild<ui::Button>({
			.margin = {kPickerGap, 0.0F, 0.0F, 0.0F},
			.label = "Add to Queue",
			.variant = ui::ButtonVariant::Primary,
			.onPress = [this] { addToQueue(); },
		});

		// The queue.
		const ui::Ref<ui::Component> right = columns.addChild<ui::Component>(
			{.size = {kRightColumnWidth, ui::kAuto}, .layout = scrolling, .overflow = ui::Overflow::Scroll}
		);
		right.addChild<ui::Text>({.style = heading(theme), .text = "QUEUE"});
		emptyQueue = right.addChild<ui::Text>({.style = body(theme, look::t::color::textDim), .text = "No items queued"});
		queueList = column(right, 2.0F);

		details.get()->setVisible(false);
		showQuantity();
	}

	void CraftingDialog::update(ecs::World& world, const engine::assets::RecipeRegistry& registry) {
		if (!isOpen()) {
			return;
		}
		// The model auto-selects the first recipe on its first refresh after setStation.
		const CraftingDialogModel::UpdateType change = model.refresh(world, registry);
		const bool							  full = fresh || change == CraftingDialogModel::UpdateType::Full;
		// A full refresh can flip a recipe's availability.
		if (full || detailsChanged) {
			showRecipes();
			showDetails();
		}
		if (full) {
			showQuantity();
		}
		if (full || change == CraftingDialogModel::UpdateType::Queue) {
			showQueue();
		}
		fresh = false;
		detailsChanged = false;
	}

	void CraftingDialog::showRecipes() {
		ui::Component* list = recipeList.get();
		if (list == nullptr) {
			return;
		}
		const std::string& selected = model.selectedRecipeDefName();
		list->reconcileChildren(
			model.recipes(),
			[](const RecipeListItem& recipe) { return recipe.defName; },
			[&](const RecipeListItem& recipe) -> ui::Ref<ui::Component> {
				return list->addChild<ui::ListRow>({
					.label = recipe.label,
					.selected = recipe.defName == selected,
					.dim = !recipe.canCraft,
					.onPress = [this, defName = recipe.defName] { selectRecipe(defName); },
				});
			},
			[&](ui::Component& child, const RecipeListItem& recipe) {
				auto& row = static_cast<ui::ListRow&>(child);
				row.setLabel(recipe.label);
				row.setSelected(recipe.defName == selected);
				row.setDim(!recipe.canCraft);
			}
		);
	}

	void CraftingDialog::showDetails() {
		if (details.get() == nullptr) {
			return;
		}
		const SelectedRecipeDetails& recipe = model.selectedDetails();
		const bool					 chosen = !recipe.name.empty();
		noSelection.get()->setVisible(!chosen);
		details.get()->setVisible(chosen);
		if (!chosen) {
			return;
		}
		const ui::Theme& theme = details.get()->getContext()->getTheme();
		recipeName.get()->setText(recipe.name);
		description.get()->setText(recipe.description);
		description.get()->setVisible(!recipe.description.empty());

		requiresSection.get()->setVisible(!recipe.materials.empty());
		ui::Component& materialList = *materials.get();
		const auto	   material = [&](const MaterialRequirement& need) {
			const std::string line = std::to_string(need.required) + "x " + need.label + (need.hasEnough ? " [OK]" : " [X]");
			return std::pair{line, body(theme, need.hasEnough ? look::t::color::statusOk : look::t::color::statusCrit)};
		};
		materialList.reconcileChildren(
			recipe.materials,
			[](const MaterialRequirement& need) { return need.defName; },
			[&](const MaterialRequirement& need) -> ui::Ref<ui::Component> {
				const auto [line, style] = material(need);
				return materialList.addChild<ui::Text>({.style = style, .text = line});
			},
			[&](ui::Component& child, const MaterialRequirement& need) {
				const auto [line, style] = material(need);
				auto& text = static_cast<ui::Text&>(child);
				text.setText(line);
				text.setStyle(style);
			}
		);

		producesSection.get()->setVisible(!recipe.outputs.empty());
		ui::Component& outputList = *outputs.get();
		const auto	   produced = [](const RecipeOutputItem& output) {
			return std::to_string(output.count) + "x " + output.label;
		};
		outputList.reconcileChildren(
			recipe.outputs,
			[](const RecipeOutputItem& output) { return output.label; },
			[&](const RecipeOutputItem& output) -> ui::Ref<ui::Component> {
				return outputList.addChild<ui::Text>({.style = body(theme), .text = produced(output)});
			},
			[&](ui::Component& child, const RecipeOutputItem& output) { static_cast<ui::Text&>(child).setText(produced(output)); }
		);

		workTime.get()->setText("~" + std::to_string(static_cast<int>(recipe.workTime)) + " seconds");
	}

	void CraftingDialog::showQuantity() {
		if (quantity.get() == nullptr) {
			return;
		}
		quantity.get()->setText(std::to_string(model.quantity()));
		minusTen.get()->setEnabled(model.quantity() > 10);
		minusOne.get()->setEnabled(model.quantity() > 1);
	}

	void CraftingDialog::showQueue() {
		ui::Component* list = queueList.get();
		if (list == nullptr) {
			return;
		}
		const std::vector<QueuedJobItem>& queue = model.queue();
		emptyQueue.get()->setVisible(queue.empty());

		std::vector<QueueLine> lines;
		for (std::size_t i = 0; i < queue.size(); ++i) {
			const QueuedJobItem& job = queue[i];
			if (i == 0) {
				lines.push_back(
					job.isInProgress ? QueueLine{.key = "heading:running", .heading = "In Progress:"}
									 : QueueLine{.key = "heading:queued", .heading = "Queued:"}
				);
			}
			lines.push_back({.key = (job.isInProgress ? "running:" : "queued:") + job.recipeDefName, .job = &job});
			if (i == 0 && job.isInProgress && queue.size() > 1) {
				lines.push_back({.key = "heading:queued", .heading = "Queued:"});
			}
		}

		const ui::Theme& theme = list->getContext()->getTheme();
		list->reconcileChildren(
			lines,
			[](const QueueLine& line) { return line.key; },
			[&](const QueueLine& line) -> ui::Ref<ui::Component> {
				if (line.job == nullptr) {
					return list->addChild<ui::Text>(
						{.style = look::text(theme, look::t::color::textDim, look::t::fontSize::fs2xs), .text = line.heading}
					);
				}
				return list->addChild<QueueEntry>({
					.job = *line.job,
					.onCancel = [this](const std::string& defName) {
						if (onCancelJob) {
							onCancelJob(defName);
						}
					},
				});
			},
			// A key names a heading or a job, never one then the other.
			[](ui::Component& child, const QueueLine& line) {
				if (line.job != nullptr) {
					static_cast<QueueEntry&>(child).setJob(*line.job);
				}
			}
		);
	}

	void CraftingDialog::selectRecipe(const std::string& defName) {
		model.selectRecipe(defName);
		showRecipes();
		// The details follow the model's next refresh, which reads them from the registry.
		detailsChanged = true;
	}

	void CraftingDialog::changeQuantity(int delta) {
		model.adjustQuantity(delta);
		showQuantity();
	}

	void CraftingDialog::addToQueue() {
		if (model.selectedRecipeDefName().empty()) {
			return;
		}
		// WorkQueue::addJob merges it with a job already there; the queue shows after the next refresh.
		if (onQueueRecipe) {
			onQueueRecipe(model.selectedRecipeDefName(), model.quantity());
		}
		model.setQuantity(1);
		showQuantity();
	}

} // namespace world_sim

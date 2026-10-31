#include "MemoryTabView.h"
#include "TabStyles.h"

#include <ui/Context.h>
#include <ui/components/Badge.h>

#include <algorithm>
#include <array>
#include <string>

namespace world_sim {

	namespace {

		constexpr std::size_t kMaxRowsPerCategory = 3;
		constexpr float		  kColumnGap = 24.0F;
		constexpr float		  kSummaryGap = 24.0F;
		constexpr float		  kMetricWidth = 140.0F;
		constexpr float		  kCategoryGap = 16.0F;
		constexpr float		  kRowHeight = 16.0F;
		constexpr float		  kHeadGap = 6.0F;

		ui::Tone categoryTone(const MemoryCategory& category) {
			if (category.count == 0) {
				return ui::Tone::Default;
			}
			return category.name == "Threats" ? ui::Tone::Danger : ui::Tone::Info;
		}

		// One category: its name and count badge, then up to three entities with their positions, a
		// "+N more" line past those, or "None sighted".
		class CategoryBlock : public ui::Component {
		  public:
			struct Args {
				UI_COMMON_ARGS
			};

			[[nodiscard]] const char* typeName() const override { return "MemoryCategory"; }

			void setCategory(const MemoryCategory& category) {
				name.get()->setText(category.name);
				badge.get()->setLabel(std::to_string(category.count));
				badge.get()->setTone(categoryTone(category));
				const std::size_t shown = std::min(category.entities.size(), kMaxRowsPerCategory);
				for (std::size_t i = 0; i < kMaxRowsPerCategory; ++i) {
					rows[i].get()->setVisible(i < shown);
					if (i < shown) {
						const MemoryEntity& entity = category.entities[i];
						entityNames[i].get()->setText(entity.name);
						entityPositions[i].get()->setText(
							"(" + std::to_string(static_cast<int>(entity.x)) + ", " + std::to_string(static_cast<int>(entity.y)) + ")"
						);
					}
				}
				const bool truncated = !category.entities.empty() && category.count > shown;
				more.get()->setVisible(truncated);
				if (truncated) {
					more.get()->setText("+" + std::to_string(category.count - shown) + " more");
				}
				none.get()->setVisible(category.entities.empty());
			}

		  protected:
			void onMount() override {
				const ui::Theme& theme = getContext()->getTheme();
				ui::Layout		 column = getLayout();
				column.display = ui::Display::Flex;
				column.flexDirection = ui::FlexDirection::Column;
				setLayout(column);
				ui::Ref<ui::Component> head = addChild<ui::Component>({
					.margin = {0.0F, 0.0F, kHeadGap, 0.0F},
					.layout = {.display = ui::Display::Flex, .alignItems = ui::AlignItems::Center},
				});
				name = head.addChild<ui::Text>(
					{.layout = {.flexGrow = 1.0F}, .style = look::text(theme, look::t::color::textBright, look::t::fontSize::fsSm)}
				);
				badge = head.addChild<ui::Badge>({});
				const ui::Style entityStyle = look::text(theme, look::t::color::text, look::t::fontSize::fsXs);
				const ui::Style positionStyle = look::text(theme, look::t::color::dataBright, look::t::fontSize::fs2xs, look::Font::Mono);
				for (std::size_t i = 0; i < kMaxRowsPerCategory; ++i) {
					rows[i] = addChild<ui::Component>({
						.size = {ui::kAuto, kRowHeight},
						.layout = {.display = ui::Display::Flex, .alignItems = ui::AlignItems::Center},
					});
					entityNames[i] = rows[i].addChild<ui::Text>({.layout = {.flexGrow = 1.0F}, .style = entityStyle});
					entityPositions[i] = rows[i].addChild<ui::Text>({.style = positionStyle});
				}
				const ui::Style faint = look::text(theme, look::t::color::textFaint, look::t::fontSize::fs2xs);
				more = addChild<ui::Text>({.size = {ui::kAuto, kRowHeight}, .style = faint});
				none = addChild<ui::Text>(
					{.style = look::text(theme, look::t::color::textFaint, look::t::fontSize::fsXs), .text = "None sighted"}
				);
			}

		  private:
			ui::Ref<ui::Text>										name;
			ui::Ref<ui::Badge>										badge;
			std::array<ui::Ref<ui::Component>, kMaxRowsPerCategory> rows;
			std::array<ui::Ref<ui::Text>, kMaxRowsPerCategory>		entityNames;
			std::array<ui::Ref<ui::Text>, kMaxRowsPerCategory>		entityPositions;
			ui::Ref<ui::Text>										more;
			ui::Ref<ui::Text>										none;
		};

		ui::Ref<ui::Text> addMetric(ui::Component& row, const ui::Theme& theme, const std::string& label) {
			ui::Ref<ui::Component> metric = row.addChild<ui::Component>({
				.size = {kMetricWidth, ui::kAuto},
				.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 2.0F},
			});
			ui::Ref<ui::Text>	   value = metric.addChild<ui::Text>(
				 {.style = look::text(theme, look::t::color::textBright, look::t::fontSize::fsXl, look::Font::Display)}
			 );
			metric.addChild<ui::Text>({.style = look::text(theme, look::t::color::textDim, look::t::fontSize::fs2xs), .text = label});
			return value;
		}

		void showColumn(ui::Component& column, const std::vector<const MemoryCategory*>& categories) {
			column.reconcileChildren(
				categories,
				[](const MemoryCategory* category) { return category->name; },
				[&column](const MemoryCategory* category) -> ui::Ref<ui::Component> {
					ui::Ref<CategoryBlock> block = column.addChild<CategoryBlock>({});
					block.get()->setCategory(*category);
					return block;
				},
				[](ui::Component& block, const MemoryCategory* category) { static_cast<CategoryBlock&>(block).setCategory(*category); }
			);
		}

	} // namespace

	void MemoryTabView::onMount() {
		const ui::Theme& theme = getContext()->getTheme();
		ui::Layout		 layout = getLayout();
		layout.display = ui::Display::Flex;
		layout.flexDirection = ui::FlexDirection::Column;
		layout.gap = kSummaryGap;
		setLayout(layout);

		ui::Ref<ui::Component> summary = addChild<ui::Component>({.layout = {.display = ui::Display::Flex}});
		knownValue = addMetric(*summary.get(), theme, "Locations known");
		sightValue = addMetric(*summary.get(), theme, "Sight range");

		ui::Ref<ui::Component> grid =
			addChild<ui::Component>({.layout = {.display = ui::Display::Flex, .gap = kColumnGap, .alignItems = ui::AlignItems::Start}});
		const ui::Component::Args columnArgs{
			.size = {0.0F, ui::kAuto},
			.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = kCategoryGap, .flexGrow = 1.0F},
		};
		leftColumn = grid.addChild<ui::Component>(columnArgs);
		rightColumn = grid.addChild<ui::Component>(columnArgs);
		update({});
	}

	void MemoryTabView::update(const MemoryData& data) {
		if (knownValue.get() == nullptr) {
			return;
		}
		knownValue.get()->setText(std::to_string(data.totalKnown));
		sightValue.get()->setText(std::to_string(static_cast<int>(data.sightRadius)) + "m");
		// Alternating, left first, as the prototype's two-column grid fills.
		std::vector<const MemoryCategory*> left;
		std::vector<const MemoryCategory*> right;
		for (std::size_t i = 0; i < data.categories.size(); ++i) {
			(i % 2 == 0 ? left : right).push_back(&data.categories[i]);
		}
		showColumn(*leftColumn.get(), left);
		showColumn(*rightColumn.get(), right);
	}

} // namespace world_sim

#include "GearTabView.h"
#include "TabStyles.h"

#include <primitives/Primitives.h>
#include <ui/Context.h>
#include <ui/shapes/Icon.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>

#include <array>
#include <cstdint>
#include <format>
#include <span>
#include <string>

namespace world_sim {

	namespace {

		constexpr float kSlotBoxHeight = 40.0F;
		constexpr float kStackRowHeight = 18.0F;
		constexpr float kBeltCellSize = 40.0F;
		constexpr float kToolIcon = 18.0F;
		constexpr float kPlusIcon = 12.0F;
		constexpr float kQtyColumn = 44.0F;
		constexpr float kKgColumn = 64.0F;
		// The prototype's figure is a 64x150 viewBox, drawn 1.5x.
		constexpr float kFigureWidth = 96.0F;
		constexpr float kFigureHeight = 225.0F;
		constexpr float kEmptyWash = 0.4F; // an empty slot's inset fill
		constexpr float kLoadWarn = 0.85F; // past this share of capacity, a meter turns to warning

		std::string kg1(float kg) {
			return std::format("{:.1f}", kg);
		}

		std::string kg0(float kg) {
			return std::format("{:.0f}", kg);
		}

		Foundation::Color faded(Foundation::Color color, float alpha) {
			color.a *= alpha;
			return color;
		}

		// A text that cuts off at its own box, so a long name never runs under its neighbours.
		ui::Ref<ui::Text> clippedText(ui::Component& row, const ui::Style& style) {
			ui::Ref<ui::Component> clip = row.addChild<ui::Component>({
				.minSize = {0.0F, ui::kAuto},
				.layout = {.flexGrow = 1.0F},
				.overflow = ui::Overflow::Clip,
			});
			return clip.addChild<ui::Text>({.style = style});
		}

	} // namespace

	// A paperdoll slot: a mono kicker over the item's name, or "+ Empty". A filled hand slot takes an
	// accent border, and a two-hand item a "2H" chip.
	class GearSlotBox : public ui::Rectangle {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string label;
		};

		explicit GearSlotBox(const Args& args)
			: label(args.label) {}

		[[nodiscard]] const char* typeName() const override { return "GearSlotBox"; }

		void setItem(const std::string& itemName, const std::string& chipText) {
			filled = true;
			name.get()->setText(itemName);
			chipLabel.get()->setText(chipText);
			chip.get()->setVisible(!chipText.empty());
			show();
		}

		void setEmpty() {
			filled = false;
			show();
		}

	  protected:
		void onMount() override {
			const ui::Theme& theme = getContext()->getTheme();
			if (getSizeSpec().height.isAuto()) {
				setSize({getSizeSpec().width, kSlotBoxHeight});
			}
			ui::Layout column = getLayout();
			column.display = ui::Display::Flex;
			column.flexDirection = ui::FlexDirection::Column;
			column.gap = theme.get(tokens::spacing::space1);
			column.padding = {5.0F, theme.get(tokens::spacing::space2)};
			setLayout(column);
			addChild<ui::Text>({.style = tabs::kickerText(theme, tokens::color::textFaint), .text = label});
			ui::Ref<ui::Component> row = addChild<ui::Component>({
				.layout = {.display = ui::Display::Flex, .gap = theme.get(tokens::spacing::space15), .alignItems = ui::AlignItems::Center},
			});
			empty = row.addChild<ui::Text>(
				{.style = world_sim::text(theme, {.size = tokens::fontSize::fsXs, .color = tokens::color::textFaint}), .text = "+ Empty"}
			);
			name = clippedText(*row.get(), world_sim::text(theme, {.size = tokens::fontSize::fsSm, .color = tokens::color::textBright}));
			nameBox = name.get()->parent();
			chip = row.addChild<ui::Rectangle>({
				.layout = {.display = ui::Display::Flex, .padding = {1.0F, theme.get(tokens::spacing::space1)}},
				.style = {
					.backgroundColor = faded(theme.get(tokens::color::accent), 0.15F),
					.borderColor = faded(theme.get(tokens::color::accent), 0.5F),
					.borderWidth = theme.get(tokens::border::bw),
					.borderRadius = theme.get(tokens::radius::rSm),
				},
			});
			chipLabel = chip.addChild<ui::Text>(
				{.style = world_sim::text(
					 theme, {.family = ui::tokens::font::family::mono, .size = tokens::fontSize::fs2xs, .color = tokens::color::accent}
				 )}
			);
			show();
		}

	  private:
		void show() {
			if (getContext() == nullptr || empty.get() == nullptr) {
				return;
			}
			const ui::Theme&		theme = getContext()->getTheme();
			const Foundation::Color fill = theme.get(tokens::color::bgInset);
			setStyle({
				.backgroundColor = filled ? fill : faded(fill, kEmptyWash),
				.borderColor = theme.get(filled ? tokens::color::accent : tokens::color::lineHairline),
				.borderWidth = theme.get(tokens::border::bw),
				.borderRadius = theme.get(tokens::radius::rSm),
			});
			empty.get()->setVisible(!filled);
			nameBox.get()->setVisible(filled);
			if (!filled) {
				chip.get()->setVisible(false);
			}
		}

		std::string			   label;
		bool				   filled = false;
		ui::Ref<ui::Text>	   empty;
		ui::Ref<ui::Component> nameBox;
		ui::Ref<ui::Text>	   name;
		ui::Ref<ui::Rectangle> chip;
		ui::Ref<ui::Text>	   chipLabel;
	};

	// A row of text at the left and an optional mono value at the right, in the accent: a section's
	// header ("FIELD PACK", "14.3 / 30 kg") or a caption.
	class GearTextRow : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string left;
			std::string right;
			ui::Style	leftStyle;
		};

		explicit GearTextRow(const Args& args)
			: left(args.left),
			  right(args.right),
			  leftStyle(args.leftStyle) {}

		[[nodiscard]] const char* typeName() const override { return "GearTextRow"; }

		void setLeft(const std::string& text) { leftText.get()->setText(text); }
		void setRight(const std::string& text) { rightText.get()->setText(text); }

	  protected:
		void onMount() override {
			const ui::Theme& theme = getContext()->getTheme();
			ui::Layout		 row = getLayout();
			row.display = ui::Display::Flex;
			row.gap = theme.get(tokens::spacing::space2);
			row.alignItems = ui::AlignItems::Center;
			setLayout(row);
			leftText = addChild<ui::Text>({.layout = {.flexGrow = 1.0F}, .style = leftStyle, .text = left});
			ui::Style value = world_sim::text(
				theme, {.family = ui::tokens::font::family::mono, .size = tokens::fontSize::fsSm, .color = tokens::color::accent}
			);
			value.fontSize = leftStyle.fontSize;
			rightText = addChild<ui::Text>({.style = value, .text = right});
		}

	  private:
		std::string		  left;
		std::string		  right;
		ui::Style		  leftStyle;
		ui::Ref<ui::Text> leftText;
		ui::Ref<ui::Text> rightText;
	};

	// One backpack stack: its name, the quantity and the mass, the last two right-aligned in columns.
	class GearStackRow : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
		};

		[[nodiscard]] const char* typeName() const override { return "GearStackRow"; }

		void setItem(const GearItem& item) {
			name.get()->setText(item.name);
			quantity.get()->setText(item.quantity > 1 ? "x" + std::to_string(item.quantity) : std::string());
			mass.get()->setText(kg1(item.totalKg) + " kg");
		}

	  protected:
		void onMount() override {
			const ui::Theme& theme = getContext()->getTheme();
			setSize({getSizeSpec().width, kStackRowHeight});
			ui::Layout row = getLayout();
			row.display = ui::Display::Flex;
			row.alignItems = ui::AlignItems::Center;
			setLayout(row);
			name = clippedText(*this, world_sim::text(theme, {.size = tokens::fontSize::fsSm, .color = tokens::color::text}));
			const ui::Style right{.textAlign = Foundation::HorizontalAlign::Right};
			quantity = addChild<ui::Text>({
				.size = {kQtyColumn, ui::kAuto},
				.style =
					world_sim::text(
						theme, {.family = ui::tokens::font::family::mono, .size = tokens::fontSize::fs2xs, .color = tokens::color::textDim}
					)
						.extend(right),
			});
			mass = addChild<ui::Text>({
				.size = {kKgColumn, ui::kAuto},
				.style = world_sim::text(
							 theme,
							 {.family = ui::tokens::font::family::mono, .size = tokens::fontSize::fs2xs, .color = tokens::color::dataBright}
				)
							 .extend(right),
			});
		}

	  private:
		ui::Ref<ui::Text> name;
		ui::Ref<ui::Text> quantity;
		ui::Ref<ui::Text> mass;
	};

	// A quick-draw belt slot: a bordered square with a tool glyph when filled, a faint plus when not.
	class GearBeltCell : public ui::Rectangle {
	  public:
		struct Args {
			UI_COMMON_ARGS
		};

		[[nodiscard]] const char* typeName() const override { return "GearBeltCell"; }

		void setFilled(bool value) {
			filled = value;
			show();
		}

	  protected:
		void onMount() override {
			const ui::Theme& theme = getContext()->getTheme();
			setSize({kBeltCellSize, kBeltCellSize});
			ui::Layout centre = getLayout();
			centre.display = ui::Display::Flex;
			centre.alignItems = ui::AlignItems::Center;
			centre.justifyContent = ui::JustifyContent::Center;
			setLayout(centre);
			tool = addChild<ui::Icon>(
				{.size = {kToolIcon, kToolIcon}, .style = {.color = theme.get(tokens::color::accent)}, .name = "hammer"}
			);
			plus = addChild<ui::Icon>(
				{.size = {kPlusIcon, kPlusIcon}, .style = {.color = theme.get(tokens::color::textDim)}, .name = "plusBold"}
			);
			show();
		}

	  private:
		void show() {
			if (getContext() == nullptr || tool.get() == nullptr) {
				return;
			}
			const ui::Theme&		theme = getContext()->getTheme();
			const Foundation::Color fill = theme.get(tokens::color::bgInset);
			setStyle({
				.backgroundColor = filled ? fill : faded(fill, kEmptyWash),
				.borderColor = theme.get(filled ? tokens::color::lineEdge : tokens::color::lineHairline),
				.borderWidth = theme.get(tokens::border::bw),
				.borderRadius = theme.get(tokens::radius::rSm),
			});
			tool.get()->setVisible(filled);
			plus.get()->setVisible(!filled);
		}

		bool			  filled = false;
		ui::Ref<ui::Icon> tool;
		ui::Ref<ui::Icon> plus;
	};

	namespace {

		// The body silhouette between the worn slots, traced from the prototype's 64x150 SVG figure.
		// Triangles: there is no component for an arbitrary polygon.
		class GearBodyFigure : public ui::Component {
		  public:
			struct Args {
				UI_COMMON_ARGS
			};

			[[nodiscard]] const char* typeName() const override { return "GearBodyFigure"; }

		  protected:
			void onMount() override { setSize({kFigureWidth, kFigureHeight}); }

			void render() override {
				const float				scale = size.y / 150.0F;
				const Foundation::Vec2	origin{(size.x - 64.0F * scale) * 0.5F, 0.0F};
				const Foundation::Color fill = faded(getContext()->getTheme().get(tokens::color::textFaint), opacity());

				Renderer::Primitives::drawCircle(
					{.center = origin + Foundation::Vec2{32.0F, 18.0F} * scale,
					 .radius = 11.0F * scale,
					 .style = {.fill = fill},
					 .key = drawKey()}
				);

				static constexpr Foundation::Vec2 kTorso[8] = {
					{21, 30}, {43, 30}, {47, 44}, {42, 47}, {42, 78}, {22, 78}, {22, 47}, {17, 44}
				};
				static constexpr uint16_t kTorsoIndices[18] = {0, 1, 2, 0, 2, 3, 0, 3, 6, 0, 6, 7, 6, 3, 4, 6, 4, 5};
				polygon(kTorso, kTorsoIndices, origin, scale, fill);

				static constexpr Foundation::Vec2 kArmLeft[4] = {{19, 31}, {9, 64}, {14, 67}, {23, 42}};
				static constexpr Foundation::Vec2 kArmRight[4] = {{45, 31}, {55, 64}, {50, 67}, {41, 42}};
				static constexpr uint16_t		  kQuadIndices[6] = {0, 1, 2, 0, 2, 3};
				polygon(kArmLeft, kQuadIndices, origin, scale, fill);
				polygon(kArmRight, kQuadIndices, origin, scale, fill);

				static constexpr Foundation::Vec2 kLegLeft[6] = {{24, 78}, {31, 78}, {31, 120}, {29, 146}, {23, 146}, {25, 118}};
				static constexpr Foundation::Vec2 kLegRight[6] = {{40, 78}, {33, 78}, {33, 120}, {35, 146}, {41, 146}, {39, 118}};
				static constexpr uint16_t		  kLegIndices[12] = {0, 1, 2, 0, 2, 5, 5, 2, 3, 5, 3, 4};
				polygon(kLegLeft, kLegIndices, origin, scale, fill);
				polygon(kLegRight, kLegIndices, origin, scale, fill);
			}

		  private:
			// A shape from the figure's viewBox, `scale` px a unit from `origin` in the box; at most 8 points.
			void polygon(
				std::span<const Foundation::Vec2> shape,
				std::span<const uint16_t>		  indices,
				Foundation::Vec2				  origin,
				float							  scale,
				Foundation::Color				  color
			) const {
				std::array<Foundation::Vec2, 8> vertices{};
				for (size_t i = 0; i < shape.size(); ++i) {
					vertices[i] = origin + shape[i] * scale;
				}
				Renderer::Primitives::drawTriangles({
					.vertices = vertices.data(),
					.indices = indices.data(),
					.vertexCount = shape.size(),
					.indexCount = indices.size(),
					.color = color,
					.key = drawKey(),
				});
			}
		};

		ui::Ref<ui::Component> column(ui::Component& parent, float gap, float grow = 0.0F) {
			return parent.addChild<ui::Component>({
				.size = {grow > 0.0F ? ui::Length(0.0F) : ui::kAuto, ui::kAuto},
				.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = gap, .flexGrow = grow},
			});
		}

		std::string itemLabel(const GearItem& item) {
			return item.quantity > 1 ? item.name + " x" + std::to_string(item.quantity) : item.name;
		}

	} // namespace

	void GearTabView::onMount() {
		const ui::Theme& theme = getContext()->getTheme();
		const float		 sectionGap = theme.get(tokens::spacing::space4);
		const float		 slotGap = theme.get(tokens::spacing::space15);
		const float		 rowGap = theme.get(tokens::spacing::space1);
		ui::Layout		 layout = getLayout();
		layout.display = ui::Display::Flex;
		layout.flexDirection = ui::FlexDirection::Column;
		layout.gap = sectionGap;
		setLayout(layout);

		const float		heading = theme.get(tokens::fontSize::fsSm);
		const ui::Style headingStyle =
			world_sim::text(
				theme, {.family = ui::tokens::font::family::display, .size = tokens::fontSize::fsSm, .color = tokens::color::textBright}
			)
				.extend({
					.letterSpacing = heading * theme.get(tokens::letterSpacing::lsWide),
					.textTransform = Foundation::TextTransform::Uppercase,
				});
		const ui::Style captionStyle = world_sim::text(theme, {.size = tokens::fontSize::fs2xs, .color = tokens::color::textFaint});

		// The paperdoll: worn stubs, the figure, back and belt and the hands.
		ui::Ref<ui::Component> paperdoll = addChild<ui::Component>({.layout = {.display = ui::Display::Flex, .gap = sectionGap}});
		ui::Ref<ui::Component> worn = column(*paperdoll.get(), slotGap, 1.0F);
		// No apparel system yet, so the worn slots are empty stubs. ASCII separators: the font atlas has no
		// middot.
		for (const char* slot : {"Head", "Face", "Body - Over", "Body - Under", "Legs", "Feet"}) {
			worn.addChild<GearSlotBox>({.label = slot});
		}
		ui::Ref<ui::Component> figure = paperdoll.addChild<ui::Component>({
			.size = {kFigureWidth, ui::kAuto},
			.layout = {
				.display = ui::Display::Flex,
				.flexDirection = ui::FlexDirection::Column,
				.alignItems = ui::AlignItems::Center,
				.justifyContent = ui::JustifyContent::Center,
			},
		});
		figure.addChild<GearBodyFigure>({});
		ui::Ref<ui::Component> carried = column(*paperdoll.get(), slotGap, 1.0F);
		carried.addChild<GearSlotBox>({.label = "Back"});
		carried.addChild<GearSlotBox>({.label = "Belt"});
		bothHandsBox = carried.addChild<GearSlotBox>({.visible = false, .label = "Both Hands"});
		leftHandBox = carried.addChild<GearSlotBox>({.label = "Left Hand"});
		rightHandBox = carried.addChild<GearSlotBox>({.label = "Right Hand"});

		// The Field Pack: the cargo meter over the stacks.
		ui::Ref<ui::Component> pack = column(*this, rowGap);
		packHeader = pack.addChild<GearTextRow>({.left = "Field Pack", .leftStyle = headingStyle});
		packMeter = pack.addChild<ui::ProgressBar>({.value = 0.0F, .tone = ui::Tone::Info, .controlSize = ui::ControlSize::Sm});
		pack.addChild<GearTextRow>({.left = "Weight-based - cargo in hands & pack - tools ride free", .leftStyle = captionStyle});
		ui::Ref<ui::Component> stacks = column(*pack.get(), theme.get(tokens::spacing::space05));
		for (ui::Ref<GearStackRow>& row : packRows) {
			row = stacks.addChild<GearStackRow>({.visible = false});
		}
		packMoreRow = stacks.addChild<GearTextRow>({.visible = false, .leftStyle = captionStyle});
		packEmptyRow = stacks.addChild<GearTextRow>(
			{.left = "Nothing carried",
			 .leftStyle = world_sim::text(theme, {.size = tokens::fontSize::fsXs, .color = tokens::color::textFaint})}
		);

		// The Tool Belt: two quick-draw slots.
		ui::Ref<ui::Component> belt = column(*this, rowGap);
		beltHeader = belt.addChild<GearTextRow>({.left = "Tool Belt", .leftStyle = headingStyle});
		belt.addChild<GearTextRow>({.left = "Quick-draw slots - one-hand tools only", .leftStyle = captionStyle});
		ui::Ref<ui::Component> cells =
			belt.addChild<ui::Component>({.layout = {.display = ui::Display::Flex, .gap = theme.get(tokens::spacing::space2)}});
		for (ui::Ref<GearBeltCell>& cell : beltCells) {
			cell = cells.addChild<GearBeltCell>({});
		}

		totalMeter =
			addChild<ui::ProgressBar>({.value = 0.0F, .tone = ui::Tone::Info, .label = "Carry Load", .controlSize = ui::ControlSize::Sm});
		update({});
	}

	void GearTabView::update(const GearData& gear) {
		if (totalMeter.get() == nullptr) {
			return;
		}

		// One spanning box for a two-hand armful, otherwise a box per hand.
		bothHandsBox.get()->setVisible(gear.bothHands);
		leftHandBox.get()->setVisible(!gear.bothHands);
		rightHandBox.get()->setVisible(!gear.bothHands);
		if (gear.bothHands && gear.leftHand.has_value()) {
			bothHandsBox.get()->setItem(itemLabel(*gear.leftHand), "2H");
		} else {
			const auto hand = [](GearSlotBox& box, const std::optional<GearItem>& item) {
				if (item.has_value()) {
					box.setItem(itemLabel(*item), "");
				} else {
					box.setEmpty();
				}
			};
			hand(*leftHandBox.get(), gear.leftHand);
			hand(*rightHandBox.get(), gear.rightHand);
		}

		const float cargo = gear.capacityKg > 0.0F ? gear.cargoKg / gear.capacityKg : 0.0F;
		packHeader.get()->setRight(kg1(gear.cargoKg) + " / " + kg0(gear.capacityKg) + " kg");
		packMeter.get()->setValue(cargo);
		packMeter.get()->setTone(cargo > kLoadWarn ? ui::Tone::Warning : ui::Tone::Info);

		for (size_t i = 0; i < kPackRowPool; ++i) {
			GearStackRow& row = *packRows[i].get();
			row.setVisible(i < gear.items.size());
			if (i < gear.items.size()) {
				row.setItem(gear.items[i]);
			}
		}
		const bool more = gear.items.size() > kPackRowPool;
		packMoreRow.get()->setVisible(more);
		if (more) {
			packMoreRow.get()->setLeft("+" + std::to_string(gear.items.size() - kPackRowPool) + " more");
		}
		packEmptyRow.get()->setVisible(gear.items.empty());

		size_t beltUsed = 0;
		for (size_t i = 0; i < beltCells.size(); ++i) {
			const bool filled = gear.belt[i].has_value();
			beltCells[i].get()->setFilled(filled);
			beltUsed += filled ? 1 : 0;
		}
		beltHeader.get()->setRight(
			std::to_string(beltUsed) + " / " + std::to_string(beltCells.size()) + " slots - " + kg1(gear.beltKg) + " kg"
		);

		const float total = gear.capacityKg > 0.0F ? gear.totalKg / gear.capacityKg : 0.0F;
		totalMeter.get()->setValue(total);
		totalMeter.get()->setValueText(kg1(gear.totalKg) + " / " + kg0(gear.capacityKg) + " kg");
		totalMeter.get()->setTone(total > kLoadWarn ? ui::Tone::Warning : ui::Tone::Info);
	}

} // namespace world_sim

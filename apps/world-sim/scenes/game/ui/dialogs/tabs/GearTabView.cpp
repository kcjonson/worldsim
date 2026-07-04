#include "GearTabView.h"
#include "MeterDraw.h"

#include <components/icon/Icon.h>
#include <layout/LayoutContainer.h>
#include <primitives/Primitives.h>
#include <theme/Tokens.h>
#include <theme/Variants.h>

#include <cstdio>
#include <string>

namespace world_sim {

	namespace {

		constexpr float kSlotBoxH = 40.0F;
		constexpr float kStackRowH = 18.0F;
		constexpr float kBeltCellSize = 40.0F;
		// Prototype figure is a 64x150 viewBox; scaled 1.5x here.
		constexpr float kFigureW = 96.0F;
		constexpr float kFigureH = 225.0F;
		constexpr float kBareMeterH = 6.0F;						// track only
		constexpr float kLabeledMeterH = UI::fs_2xs + 4.0F + 6.0F; // label row + track

		std::string fmtKg1(float kg) {
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%.1f", kg);
			return buf;
		}

		std::string fmtKg0(float kg) {
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%.0f", kg);
			return buf;
		}

		// Trim with "..." so `text` renders within maxWidth.
		std::string elide(const std::string& text, float fontPx, float maxWidth, Renderer::FontFamily font) {
			if (tabs::measureText(text, fontPx, font) * tabs::kRenderWidthFudge <= maxWidth) {
				return text;
			}
			std::string t = text;
			while (!t.empty() && tabs::measureText(t + "...", fontPx, font) * tabs::kRenderWidthFudge > maxWidth) {
				t.pop_back();
			}
			return t + "...";
		}

	} // namespace

	// A paperdoll slot: mono kicker label on top, item name (or "+ Empty") below.
	// Filled hand slots get an accent border and an optional "2H" chip.
	struct GearSlotBox : UI::Component {
		std::string slotLabel;
		std::string itemName; // empty = empty slot
		std::string chip;	  // "2H" for a two-hand item
		bool		accent = false;

		explicit GearSlotBox(std::string label)
			: slotLabel(std::move(label)) {
			size = {0.0F, kSlotBoxH};
			widthMode = UI::SizeMode::Fill;
		}

		void setItem(std::string name, std::string chipText, bool accentBorder) {
			itemName = std::move(name);
			chip = std::move(chipText);
			accent = accentBorder;
		}

		void setEmpty() {
			itemName.clear();
			chip.clear();
			accent = false;
		}

		const char* debugTypeName() const override { return "GearSlotBox"; }

		void render() override {
			const bool filled = !itemName.empty();
			const Foundation::Color borderColor = accent ? UI::accent : (filled ? UI::line_edge : UI::line_hairline);
			Renderer::Primitives::drawRect({
				.bounds = {position.x, position.y, size.x, size.y},
				.style = {.fill = filled ? UI::bg_inset : UI::withAlpha(UI::bg_inset, 0.4F),
						  .border = Foundation::BorderStyle{.color = borderColor, .width = UI::bw, .cornerRadius = UI::r_sm, .position = Foundation::BorderPosition::Inside}},
			});

			const float pad = UI::space_2;
			Renderer::Primitives::drawText({
				.text = slotLabel,
				.position = {position.x + pad, position.y + 5.0F},
				.scale = UI::fs_2xs / 16.0F,
				.color = UI::text_faint,
				.font = UI::fontMono,
				.letterSpacing = UI::fs_2xs * UI::ls_wider,
				.transform = Foundation::TextTransform::Uppercase,
			});

			const float rowY = position.y + 5.0F + UI::fs_2xs + 4.0F;
			if (!filled) {
				tabs::drawText("+ Empty", {position.x + pad, rowY}, UI::fs_xs, UI::text_faint);
				return;
			}

			float chipW = 0.0F;
			if (!chip.empty()) {
				chipW = tabs::measureText(chip, UI::fs_2xs, UI::fontMono) + UI::space_2;
				const Foundation::Rect chipRect{position.x + size.x - pad - chipW, rowY - 1.0F, chipW, UI::fs_2xs + 4.0F};
				Renderer::Primitives::drawRect({
					.bounds = chipRect,
					.style = {.fill = UI::withAlpha(UI::accent, 0.15F),
							  .border = Foundation::BorderStyle{.color = UI::withAlpha(UI::accent, 0.5F), .width = UI::bw, .cornerRadius = UI::r_sm, .position = Foundation::BorderPosition::Inside}},
				});
				Renderer::Primitives::drawText({
					.text = chip,
					.position = {chipRect.x, chipRect.y + 2.0F},
					.scale = UI::fs_2xs / 16.0F,
					.color = UI::accent,
					.font = UI::fontMono,
					.hAlign = Foundation::HorizontalAlign::Center,
					.boxWidth = chipRect.width,
				});
				chipW += UI::space_1_5;
			}

			const float nameW = size.x - pad * 2.0F - chipW;
			tabs::drawText(elide(itemName, UI::fs_sm, nameW, UI::fontUi), {position.x + pad, rowY}, UI::fs_sm, UI::text_bright);
		}
	};

	// A full-width row: styled left text plus an optional right-aligned mono value.
	// Used for section headers ("FIELD PACK" / "14.3 / 30 kg") and caption lines.
	struct GearTextRow : UI::Component {
		struct Args {
			std::string			  left;
			std::string			  right;
			float				  fontPx = UI::fs_sm;
			Foundation::Color	  leftColor = UI::text_bright;
			Foundation::Color	  rightColor = UI::accent;
			Renderer::FontFamily  leftFont = UI::fontDisplay;
			bool				  uppercase = false;
			float				  letterSpacing = 0.0F; // em
		};

		std::string			 left;
		std::string			 right;
		float				 fontPx;
		Foundation::Color	 leftColor;
		Foundation::Color	 rightColor;
		Renderer::FontFamily leftFont;
		bool				 uppercase;
		float				 letterSpacing;

		explicit GearTextRow(const Args& args)
			: left(args.left),
			  right(args.right),
			  fontPx(args.fontPx),
			  leftColor(args.leftColor),
			  rightColor(args.rightColor),
			  leftFont(args.leftFont),
			  uppercase(args.uppercase),
			  letterSpacing(args.letterSpacing) {
			size = {0.0F, fontPx + 4.0F};
			widthMode = UI::SizeMode::Fill;
		}

		const char* debugTypeName() const override { return "GearTextRow"; }

		void render() override {
			Renderer::Primitives::drawText({
				.text = left,
				.position = position,
				.scale = fontPx / 16.0F,
				.color = leftColor,
				.font = leftFont,
				.letterSpacing = fontPx * letterSpacing,
				.transform = uppercase ? Foundation::TextTransform::Uppercase : Foundation::TextTransform::None,
			});
			if (!right.empty()) {
				Renderer::Primitives::drawText({
					.text = right,
					.position = position,
					.scale = fontPx / 16.0F,
					.color = rightColor,
					.font = UI::fontMono,
					.hAlign = Foundation::HorizontalAlign::Right,
					.boxWidth = size.x,
				});
			}
		}
	};

	// A Salvage meter row (track + optional label/value line above).
	struct GearMeterRow : UI::Component {
		std::string		  label;
		std::string		  valueText;
		float			  value01 = 0.0F;
		Foundation::Color tone = UI::data;

		explicit GearMeterRow(std::string meterLabel)
			: label(std::move(meterLabel)) {
			size = {0.0F, label.empty() ? kBareMeterH : kLabeledMeterH};
			widthMode = UI::SizeMode::Fill;
		}

		const char* debugTypeName() const override { return "GearMeterRow"; }

		void render() override {
			tabs::drawMeter(position.x, position.y, size.x, label, value01, valueText, tone);
		}
	};

	// One backpack stack: name, xN quantity, right-aligned kg.
	struct GearStackRow : UI::Component {
		std::string name;
		std::string qtyText;
		std::string kgText;

		static constexpr float kKgColW = 64.0F;
		static constexpr float kQtyColW = 44.0F;

		GearStackRow() {
			size = {0.0F, kStackRowH};
			widthMode = UI::SizeMode::Fill;
		}

		const char* debugTypeName() const override { return "GearStackRow"; }

		void render() override {
			const float nameW = size.x - kKgColW - kQtyColW - UI::space_2 * 2.0F;
			tabs::drawText(elide(name, UI::fs_sm, nameW, UI::fontUi), position, UI::fs_sm, UI::text);
			if (!qtyText.empty()) {
				Renderer::Primitives::drawText({
					.text = qtyText,
					.position = {position.x, position.y + 1.0F},
					.scale = UI::fs_2xs / 16.0F,
					.color = UI::text_dim,
					.font = UI::fontMono,
					.hAlign = Foundation::HorizontalAlign::Right,
					.boxWidth = size.x - kKgColW - UI::space_2,
				});
			}
			Renderer::Primitives::drawText({
				.text = kgText,
				.position = {position.x, position.y + 1.0F},
				.scale = UI::fs_2xs / 16.0F,
				.color = UI::data_bright,
				.font = UI::fontMono,
				.hAlign = Foundation::HorizontalAlign::Right,
				.boxWidth = size.x,
			});
		}
	};

	// A quick-draw belt slot cell: bordered square with a tool glyph (filled) or
	// a faint plus (empty). Icons are built once; visibility toggles per update.
	struct GearBeltCell : UI::Component {
		bool	  filled = false;
		UI::Icon* toolIcon = nullptr;
		UI::Icon* plusIcon = nullptr;

		GearBeltCell() {
			size = {kBeltCellSize, kBeltCellSize};
			const auto toolHandle = addChild(UI::Icon(UI::Icon::Args{.size = 18.0F, .glyph = "hammer", .tint = UI::accent}));
			const auto plusHandle = addChild(UI::Icon(UI::Icon::Args{.size = 12.0F, .glyph = "plus", .tint = UI::text_dim, .strokeWidth = 3.0F}));
			toolIcon = getChild<UI::Icon>(toolHandle);
			plusIcon = getChild<UI::Icon>(plusHandle);
			setFilled(false);
		}

		void setFilled(bool value) {
			filled = value;
			toolIcon->visible = filled;
			plusIcon->visible = !filled;
		}

		void setPosition(float x, float y) override {
			UI::Component::setPosition(x, y);
			toolIcon->setPosition(x + (kBeltCellSize - 18.0F) * 0.5F, y + (kBeltCellSize - 18.0F) * 0.5F);
			plusIcon->setPosition(x + (kBeltCellSize - 12.0F) * 0.5F, y + (kBeltCellSize - 12.0F) * 0.5F);
		}

		const char* debugTypeName() const override { return "GearBeltCell"; }

		void render() override {
			Renderer::Primitives::drawRect({
				.bounds = {position.x, position.y, size.x, size.y},
				.style = {.fill = filled ? UI::bg_inset : UI::withAlpha(UI::bg_inset, 0.4F),
						  .border = Foundation::BorderStyle{.color = filled ? UI::line_edge : UI::line_hairline, .width = UI::bw, .cornerRadius = UI::r_sm, .position = Foundation::BorderPosition::Inside}},
			});
			UI::Component::render();
		}
	};

	// The center body silhouette, traced from the prototype's 64x150 SVG figure.
	struct GearBodyFigure : UI::Component {
		GearBodyFigure() { size = {kFigureW, kFigureH}; }

		const char* debugTypeName() const override { return "GearBodyFigure"; }

		void render() override {
			const float s = size.y / 150.0F;
			const float ox = position.x + (size.x - 64.0F * s) * 0.5F;
			const float oy = position.y;
			const Foundation::Color fill = UI::text_faint;

			auto drawPoly = [&](const Foundation::Vec2* base, size_t count, const uint16_t* indices, size_t indexCount) {
				Foundation::Vec2 verts[8];
				for (size_t i = 0; i < count; ++i) {
					verts[i] = {ox + base[i].x * s, oy + base[i].y * s};
				}
				Renderer::Primitives::drawTriangles({.vertices = verts, .indices = indices, .vertexCount = count, .indexCount = indexCount, .color = fill});
			};

			// Head
			Renderer::Primitives::drawCircle({.center = {ox + 32.0F * s, oy + 18.0F * s}, .radius = 11.0F * s, .style = {.fill = fill}});

			// Torso
			static constexpr Foundation::Vec2 torso[8] = {{21, 30}, {43, 30}, {47, 44}, {42, 47}, {42, 78}, {22, 78}, {22, 47}, {17, 44}};
			static constexpr uint16_t		  torsoIdx[18] = {0, 1, 2, 0, 2, 3, 0, 3, 6, 0, 6, 7, 6, 3, 4, 6, 4, 5};
			drawPoly(torso, 8, torsoIdx, 18);

			// Arms
			static constexpr Foundation::Vec2 armL[4] = {{19, 31}, {9, 64}, {14, 67}, {23, 42}};
			static constexpr Foundation::Vec2 armR[4] = {{45, 31}, {55, 64}, {50, 67}, {41, 42}};
			static constexpr uint16_t		  quadIdx[6] = {0, 1, 2, 0, 2, 3};
			drawPoly(armL, 4, quadIdx, 6);
			drawPoly(armR, 4, quadIdx, 6);

			// Legs
			static constexpr Foundation::Vec2 legL[6] = {{24, 78}, {31, 78}, {31, 120}, {29, 146}, {23, 146}, {25, 118}};
			static constexpr Foundation::Vec2 legR[6] = {{40, 78}, {33, 78}, {33, 120}, {35, 146}, {41, 146}, {39, 118}};
			static constexpr uint16_t		  legIdx[12] = {0, 1, 2, 0, 2, 5, 5, 2, 3, 5, 3, 4};
			drawPoly(legL, 6, legIdx, 12);
			drawPoly(legR, 6, legIdx, 12);
		}
	};

	void GearTabView::create(const Foundation::Rect& contentBounds) {
		size = {contentBounds.width, contentBounds.height};

		auto rootC = UI::LayoutContainer(UI::LayoutContainer::Args{
			.position = {contentBounds.x, contentBounds.y},
			.size = {contentBounds.width, contentBounds.height},
			.direction = UI::Direction::Vertical,
			.gap = UI::space_4,
			.id = "gear_content",
		});

		// ---- Paperdoll: worn stubs | body figure | back/belt + hands ----
		auto rowC = UI::LayoutContainer(UI::LayoutContainer::Args{.direction = UI::Direction::Horizontal, .gap = UI::space_4, .id = "gear_paperdoll"});
		rowC.widthMode = UI::SizeMode::Fill;

		auto leftColC = UI::LayoutContainer(UI::LayoutContainer::Args{.direction = UI::Direction::Vertical, .gap = UI::space_1_5, .id = "gear_worn_left"});
		leftColC.widthMode = UI::SizeMode::Fill;
		// No apparel system yet: the worn slots are permanent empty stubs.
		// ASCII separators only: the SDF font atlas has no middot glyph.
		for (const char* label : {"Head", "Face", "Body - Over", "Body - Under", "Legs", "Feet"}) {
			leftColC.addChild(GearSlotBox(label));
		}

		auto centerColC = UI::LayoutContainer(UI::LayoutContainer::Args{
			.size = {kFigureW, 0.0F},
			.direction = UI::Direction::Vertical,
			.distribution = UI::Distribution::Center,
			.crossAlign = UI::CrossAlign::Center,
			.id = "gear_figure_col",
		});
		centerColC.heightMode = UI::SizeMode::Fill;
		centerColC.addChild(GearBodyFigure());

		auto rightColC = UI::LayoutContainer(UI::LayoutContainer::Args{.direction = UI::Direction::Vertical, .gap = UI::space_1_5, .id = "gear_worn_right"});
		rightColC.widthMode = UI::SizeMode::Fill;
		rightColC.addChild(GearSlotBox("Back"));
		rightColC.addChild(GearSlotBox("Belt"));
		const auto bothHandle = rightColC.addChild(GearSlotBox("Both Hands"));
		const auto leftHandle = rightColC.addChild(GearSlotBox("Left Hand"));
		const auto rightHandle = rightColC.addChild(GearSlotBox("Right Hand"));
		bothHandsBox = rightColC.getChild<GearSlotBox>(bothHandle);
		leftHandBox = rightColC.getChild<GearSlotBox>(leftHandle);
		rightHandBox = rightColC.getChild<GearSlotBox>(rightHandle);
		bothHandsBox->visible = false;

		const auto leftColHandle = rowC.addChild(std::move(leftColC));
		rowC.addChild(std::move(centerColC));
		const auto rightColHandle = rowC.addChild(std::move(rightColC));
		rightCol = rowC.getChild<UI::LayoutContainer>(rightColHandle);
		(void)leftColHandle;

		// ---- Field Pack: cargo weight meter + stack rows ----
		auto packC = UI::LayoutContainer(UI::LayoutContainer::Args{.direction = UI::Direction::Vertical, .gap = UI::space_1, .id = "gear_pack"});
		packC.widthMode = UI::SizeMode::Fill;
		const auto packHeaderHandle = packC.addChild(GearTextRow(GearTextRow::Args{.left = "Field Pack", .uppercase = true, .letterSpacing = UI::ls_wide}));
		const auto packMeterHandle = packC.addChild(GearMeterRow(""));
		packC.addChild(GearTextRow(GearTextRow::Args{
			.left = "Weight-based - cargo in hands & pack - tools ride free",
			.fontPx = UI::fs_2xs,
			.leftColor = UI::text_faint,
			.leftFont = UI::fontUi,
		}));

		auto packListC = UI::LayoutContainer(UI::LayoutContainer::Args{.direction = UI::Direction::Vertical, .gap = UI::space_0_5, .id = "gear_pack_list"});
		packListC.widthMode = UI::SizeMode::Fill;
		for (size_t i = 0; i < kPackRowPool; ++i) {
			const auto rowHandle = packListC.addChild(GearStackRow());
			packRows[i] = packListC.getChild<GearStackRow>(rowHandle);
			packRows[i]->visible = false;
		}
		const auto moreHandle = packListC.addChild(GearTextRow(GearTextRow::Args{.left = "", .fontPx = UI::fs_2xs, .leftColor = UI::text_faint, .leftFont = UI::fontUi}));
		packMoreRow = packListC.getChild<GearTextRow>(moreHandle);
		packMoreRow->visible = false;
		const auto emptyHandle = packListC.addChild(GearTextRow(GearTextRow::Args{.left = "Nothing carried", .fontPx = UI::fs_xs, .leftColor = UI::text_faint, .leftFont = UI::fontUi}));
		packEmptyRow = packListC.getChild<GearTextRow>(emptyHandle);

		const auto packListHandle = packC.addChild(std::move(packListC));
		packList = packC.getChild<UI::LayoutContainer>(packListHandle);
		packHeader = packC.getChild<GearTextRow>(packHeaderHandle);
		packMeter = packC.getChild<GearMeterRow>(packMeterHandle);

		// ---- Tool Belt: two quick-draw slots ----
		auto beltC = UI::LayoutContainer(UI::LayoutContainer::Args{.direction = UI::Direction::Vertical, .gap = UI::space_1, .id = "gear_belt"});
		beltC.widthMode = UI::SizeMode::Fill;
		const auto beltHeaderHandle = beltC.addChild(GearTextRow(GearTextRow::Args{.left = "Tool Belt", .uppercase = true, .letterSpacing = UI::ls_wide}));
		beltHeader = beltC.getChild<GearTextRow>(beltHeaderHandle);
		beltC.addChild(GearTextRow(GearTextRow::Args{
			.left = "Quick-draw slots - one-hand tools only",
			.fontPx = UI::fs_2xs,
			.leftColor = UI::text_faint,
			.leftFont = UI::fontUi,
		}));
		auto beltRowC = UI::LayoutContainer(UI::LayoutContainer::Args{.direction = UI::Direction::Horizontal, .gap = UI::space_2, .id = "gear_belt_row"});
		for (size_t i = 0; i < beltCells.size(); ++i) {
			const auto cellHandle = beltRowC.addChild(GearBeltCell());
			beltCells[i] = beltRowC.getChild<GearBeltCell>(cellHandle);
		}
		beltC.addChild(std::move(beltRowC));

		// ---- Assemble ----
		const auto rowHandle = rootC.addChild(std::move(rowC));
		const auto packHandle = rootC.addChild(std::move(packC));
		rootC.addChild(std::move(beltC));
		const auto totalHandle = rootC.addChild(GearMeterRow("Carry Load"));
		const auto rootHandle = addChild(std::move(rootC));

		root = getChild<UI::LayoutContainer>(rootHandle);
		paperdollRow = root->getChild<UI::LayoutContainer>(rowHandle);
		packSection = root->getChild<UI::LayoutContainer>(packHandle);
		totalMeter = root->getChild<GearMeterRow>(totalHandle);
	}

	void GearTabView::update(const GearData& gear) {
		if (root == nullptr) {
			return;
		}

		// Hands: one spanning box for a two-hand armful, otherwise per-hand boxes.
		bothHandsBox->visible = gear.bothHands;
		leftHandBox->visible = !gear.bothHands;
		rightHandBox->visible = !gear.bothHands;
		if (gear.bothHands) {
			const GearItem& item = *gear.leftHand;
			bothHandsBox->setItem(item.quantity > 1 ? item.name + " x" + std::to_string(item.quantity) : item.name, "2H", true);
		} else {
			auto applyHand = [](GearSlotBox* box, const std::optional<GearItem>& item) {
				if (item.has_value()) {
					box->setItem(item->quantity > 1 ? item->name + " x" + std::to_string(item->quantity) : item->name, "", true);
				} else {
					box->setEmpty();
				}
			};
			applyHand(leftHandBox, gear.leftHand);
			applyHand(rightHandBox, gear.rightHand);
		}

		// Field Pack header, meter, and stack rows.
		const float cargo01 = gear.capacityKg > 0.0F ? gear.cargoKg / gear.capacityKg : 0.0F;
		packHeader->right = fmtKg1(gear.cargoKg) + " / " + fmtKg0(gear.capacityKg) + " kg";
		packMeter->value01 = cargo01;
		packMeter->tone = cargo01 > 0.85F ? UI::status_warn : UI::data;

		const size_t shown = gear.items.size() < kPackRowPool ? gear.items.size() : kPackRowPool;
		for (size_t i = 0; i < kPackRowPool; ++i) {
			GearStackRow* row = packRows[i];
			row->visible = i < shown;
			if (i < shown) {
				const GearItem& item = gear.items[i];
				row->name = item.name;
				row->qtyText = item.quantity > 1 ? "x" + std::to_string(item.quantity) : "";
				row->kgText = fmtKg1(item.totalKg) + " kg";
			}
		}
		packMoreRow->visible = gear.items.size() > kPackRowPool;
		if (packMoreRow->visible) {
			packMoreRow->left = "+" + std::to_string(gear.items.size() - kPackRowPool) + " more";
		}
		packEmptyRow->visible = gear.items.empty();

		// Tool Belt slots.
		uint32_t beltUsed = 0;
		for (size_t i = 0; i < beltCells.size(); ++i) {
			const bool filled = gear.belt[i].has_value();
			beltCells[i]->setFilled(filled);
			if (filled) {
				++beltUsed;
			}
		}
		beltHeader->right = std::to_string(beltUsed) + " / " + std::to_string(beltCells.size()) + " slots - " + fmtKg1(gear.beltKg) + " kg";

		// Total carry load (tools included).
		const float total01 = gear.capacityKg > 0.0F ? gear.totalKg / gear.capacityKg : 0.0F;
		totalMeter->value01 = total01;
		totalMeter->valueText = fmtKg1(gear.totalKg) + " / " + fmtKg0(gear.capacityKg) + " kg";
		totalMeter->tone = total01 > 0.85F ? UI::status_warn : UI::data;

		// Visibility flips change measured heights; re-run layout on the chain.
		packList->invalidateLayout();
		packSection->invalidateLayout();
		rightCol->invalidateLayout();
		paperdollRow->invalidateLayout();
		root->invalidateLayout();
	}

} // namespace world_sim

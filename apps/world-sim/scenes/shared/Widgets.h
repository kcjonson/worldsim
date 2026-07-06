#pragma once

// Widgets - small layout-engine leaves shared by the pre-game scenes.
//
// Widget is an IComponent with position/size plumbing but no child arena
// (Component carries a 64KB MemoryArena; these leaves don't need one). A
// widget draws Salvage primitives (Panel, Badge, Divider, ...) inside the
// rect the layout engine assigns it. setLayoutSize adopts parent-assigned
// sizes, so Fill/Stretch work.

#include <component/Component.h>
#include <components/divider/Divider.h>
#include <components/scroll/ScrollContainer.h>
#include <font/FontRenderer.h>
#include <graphics/PrimitiveStyles.h>
#include <math/Types.h>
#include <primitives/Primitives.h>
#include <theme/Variants.h>

#include <array>
#include <cctype>
#include <memory>
#include <string>
#include <utility>

namespace world_sim {

	class Widget : public UI::IComponent {
	  public:
		Foundation::Vec2 position{0.0F, 0.0F};
		Foundation::Vec2 size{0.0F, 0.0F};

		[[nodiscard]] float getWidth() const override { return size.x + margin * 2.0F; }
		[[nodiscard]] float getHeight() const override { return size.y + margin * 2.0F; }
		void				setPosition(float x, float y) override { position = {x + margin, y + margin}; }
		void				setLayoutSize(float w, float h) override {
			if (w >= 0.0F) {
				size.x = w;
			}
			if (h >= 0.0F) {
				size.y = h;
			}
		}
		[[nodiscard]] Foundation::Vec2 getPosition() const override {
			return {position.x - margin, position.y - margin};
		}
		const char* debugTypeName() const override { return "Widget"; }
	};

	// Single-line styled text row: Renderer::Primitives::drawText carries the
	// font family / letter-spacing / transform that UI::Text (Roboto-only)
	// cannot. Height is fixed from the font size; width hugs or stretches.
	class Label : public Widget {
	  public:
		struct Args {
			std::string					text;
			float						fontSize{13.0F};
			Foundation::Color			color{UI::text};
			Renderer::FontFamily		font{UI::fontUi};
			float						letterSpacingEm{0.0F};
			Foundation::TextTransform	transform{Foundation::TextTransform::None};
			Foundation::HorizontalAlign hAlign{Foundation::HorizontalAlign::Left};
			const char*					id{nullptr};
			float						margin{0.0F};
		};

		explicit Label(Args args) : args(std::move(args)) {
			margin = this->args.margin;
			size.y = this->args.fontSize * 1.3F;
			widthMode = UI::SizeMode::Hug;
		}

		void setText(std::string text) { args.text = std::move(text); }
		void setColor(Foundation::Color color) { args.color = color; }

		// Hug width measures the styled text; a parent-assigned width wins.
		[[nodiscard]] float getWidth() const override {
			if (size.x > 0.0F) {
				return size.x + margin * 2.0F;
			}
			auto* fontRenderer = Renderer::Primitives::getFontRenderer();
			if (fontRenderer == nullptr || args.text.empty()) {
				return margin * 2.0F;
			}
			std::string measured = args.text;
			if (args.transform == Foundation::TextTransform::Uppercase) {
				for (char& c : measured) {
					c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
				}
			}
			return fontRenderer
					   ->MeasureText(measured, args.fontSize / 16.0F, args.font, args.fontSize * args.letterSpacingEm)
					   .x +
				   margin * 2.0F;
		}

		void render() override {
			Renderer::Primitives::drawText({
				.text = args.text,
				.position = position,
				.scale = args.fontSize / 16.0F,
				.color = args.color,
				.font = args.font,
				.hAlign = args.hAlign,
				.vAlign = Foundation::VerticalAlign::Top,
				.boxWidth = size.x,
				.letterSpacing = args.fontSize * args.letterSpacingEm,
				.transform = args.transform,
			});
		}

		const char* debugTypeName() const override { return "Label"; }
		const char* debugId() const override { return args.id; }

	  private:
		Args args;
	};

	// Amber diamond mark inside a fixed-size box so the layout engine can seat it
	// (above or beside a title) with spacing baked into the box.
	class Diamond : public Widget {
	  public:
		explicit Diamond(float radius, float boxHeight, const char* id = nullptr) : radius(radius), id(id) {
			size = {radius * 2.0F, boxHeight};
		}

		void render() override {
			const float							  cx = position.x + radius;
			const float							  cy = position.y + radius;
			const std::array<Foundation::Vec2, 4> v{
				{{cx, cy - radius}, {cx + radius, cy}, {cx, cy + radius}, {cx - radius, cy}}};
			const std::array<uint16_t, 6> idx{0, 1, 2, 0, 2, 3};
			Renderer::Primitives::drawTriangles(Renderer::Primitives::TrianglesArgs{
				.vertices = v.data(), .indices = idx.data(), .vertexCount = 4, .indexCount = 6, .color = UI::accent});
		}

		const char* debugTypeName() const override { return "Diamond"; }
		const char* debugId() const override { return id; }

	  private:
		float		radius;
		const char* id{nullptr};
	};

	// Labeled hairline rule row (UI::Divider) that stretches to the column width.
	class DividerRow : public Widget {
	  public:
		explicit DividerRow(std::string label, const char* id = nullptr) : label(std::move(label)), id(id) {
			size.y = 14.0F;
			widthMode = UI::SizeMode::Hug;
		}

		void render() override {
			UI::Divider({.position = {position.x, position.y + size.y * 0.5F}, .width = size.x, .label = label})
				.render();
		}

		const char* debugTypeName() const override { return "DividerRow"; }
		const char* debugId() const override { return id; }

	  private:
		std::string label;
		const char* id{nullptr};
	};

	// Layout-engine seat for a UI::ScrollContainer. ScrollContainer children
	// live in a local (0,0) coordinate space that the layout lint cannot
	// translate, so the scroll subtree is held privately (lint sees this leaf's
	// bounds only) while position/viewport track the assigned rect. The inner
	// content column's width is synced to the viewport minus the scrollbar.
	class ScrollRegion : public Widget {
	  public:
		explicit ScrollRegion(const char* id = nullptr) : id(id) {
			scroll = std::make_unique<UI::ScrollContainer>(UI::ScrollContainer::Args{.size = {1.0F, 1.0F}});
		}

		UI::ScrollContainer& container() { return *scroll; }

		void render() override {
			const Foundation::Vec2 scrollPos = scroll->getPosition();
			if (scrollPos.x != position.x || scrollPos.y != position.y) {
				scroll->setPosition(position.x, position.y);
			}
			if (scroll->size.x != size.x || scroll->size.y != size.y) {
				scroll->setViewportSize(size);
			}
			if (!scroll->getChildren().empty()) {
				scroll->getChildren().front()->setLayoutSize(size.x - 12.0F, UI::kSizeKeep);
			}
			scroll->render();
		}

		bool handleEvent(UI::InputEvent& event) override { return scroll->handleEvent(event); }
		bool containsPoint(Foundation::Vec2 p) const override { return scroll->containsPoint(p); }

		const char* debugTypeName() const override { return "ScrollRegion"; }
		const char* debugId() const override { return id; }

	  private:
		std::unique_ptr<UI::ScrollContainer> scroll;
		const char*							 id{nullptr};
	};

} // namespace world_sim

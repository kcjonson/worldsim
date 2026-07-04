#pragma once

#include "component/Component.h"
#include "graphics/Color.h"
#include "graphics/PrimitiveStyles.h"
#include "math/Types.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

// Basic shape types for UI component system
// Shapes implement IComponent (render only) - they are leaf nodes with no children.
// Use Component base class for elements that can have children.
//
// See: /docs/technical/ui-framework/architecture.md

namespace UI {

	// Rectangle shape - leaf node
	struct Rectangle : public IComponent {
		// Args struct for construction (replaces designated initializers)
		struct Args {
			Foundation::Vec2	  position{0.0F, 0.0F};
			Foundation::Vec2	  size{100.0F, 100.0F};
			Foundation::RectStyle style;
			const char*			  id = nullptr;
			short				  zIndex{0};
			bool				  visible{true};
			float				  margin{0.0F};
		};

		Foundation::Vec2	  position{0.0F, 0.0F};
		Foundation::Vec2	  size{100.0F, 100.0F};
		Foundation::RectStyle style;
		const char*			  id = nullptr;

		Rectangle() = default;
		explicit Rectangle(const Args& args)
			: position(args.position),
			  size(args.size),
			  style(args.style),
			  id(args.id) {
			zIndex = args.zIndex;
			IComponent::visible = args.visible;
			IComponent::margin = args.margin;
		}

		// Layout API
		float getWidth() const override { return size.x + margin * 2.0F; }
		float getHeight() const override { return size.y + margin * 2.0F; }
		void  setPosition(float x, float y) override { position = {x + margin, y + margin}; }
		void  setLayoutSize(float w, float h) override {
			 if (w >= 0.0F) {
				 size.x = w;
			 }
			 if (h >= 0.0F) {
				 size.y = h;
			 }
		}

		// Debug introspection (position stores the content origin, so back out margin)
		Foundation::Vec2 getPosition() const override { return {position.x - margin, position.y - margin}; }
		const char*		 debugTypeName() const override { return "Rectangle"; }
		const char*		 debugId() const override { return id; }

		void render() override;
	};

	// Circle shape - leaf node
	struct Circle : public IComponent {
		// Args struct for construction (replaces designated initializers)
		struct Args {
			Foundation::Vec2		center{0.0F, 0.0F};
			float					radius{50.0F};
			Foundation::CircleStyle style;
			const char*				id = nullptr;
			short					zIndex{0};
			bool					visible{true};
			float					margin{0.0F};
		};

		Foundation::Vec2		center{0.0F, 0.0F};
		float					radius{50.0F};
		Foundation::CircleStyle style;
		const char*				id = nullptr;

		Circle() = default;
		explicit Circle(const Args& args)
			: center(args.center),
			  radius(args.radius),
			  style(args.style),
			  id(args.id) {
			zIndex = args.zIndex;
			IComponent::visible = args.visible;
			IComponent::margin = args.margin;
		}

		// Layout API
		float getWidth() const override { return radius * 2.0F + margin * 2.0F; }
		float getHeight() const override { return radius * 2.0F + margin * 2.0F; }
		void  setPosition(float x, float y) override { center = {x + margin + radius, y + margin + radius}; }

		// Debug introspection
		Foundation::Vec2 getPosition() const override { return {center.x - radius - margin, center.y - radius - margin}; }
		const char*		 debugTypeName() const override { return "Circle"; }
		const char*		 debugId() const override { return id; }

		void render() override;
	};

	// Line shape - leaf node
	struct Line : public IComponent {
		// Args struct for construction (replaces designated initializers)
		struct Args {
			Foundation::Vec2	  start{0.0F, 0.0F};
			Foundation::Vec2	  end{100.0F, 100.0F};
			Foundation::LineStyle style;
			const char*			  id = nullptr;
			short				  zIndex{0};
			bool				  visible{true};
			float				  margin{0.0F};
		};

		Foundation::Vec2	  start{0.0F, 0.0F};
		Foundation::Vec2	  end{100.0F, 100.0F};
		Foundation::LineStyle style;
		const char*			  id = nullptr;

		Line() = default;
		explicit Line(const Args& args)
			: start(args.start),
			  end(args.end),
			  style(args.style),
			  id(args.id) {
			zIndex = args.zIndex;
			IComponent::visible = args.visible;
			IComponent::margin = args.margin;
		}

		// Layout API
		float getWidth() const override {
			return std::abs(end.x - start.x) + margin * 2.0F;
		}
		float getHeight() const override {
			return std::abs(end.y - start.y) + margin * 2.0F;
		}
		void setPosition(float x, float y) override {
			// Translate both endpoints by the delta from current top-left
			float minX = std::min(start.x, end.x);
			float minY = std::min(start.y, end.y);
			float dx = (x + margin) - minX;
			float dy = (y + margin) - minY;
			start.x += dx;
			start.y += dy;
			end.x += dx;
			end.y += dy;
		}

		// Debug introspection
		Foundation::Vec2 getPosition() const override {
			return {std::min(start.x, end.x) - margin, std::min(start.y, end.y) - margin};
		}
		const char* debugTypeName() const override { return "Line"; }
		const char* debugId() const override { return id; }

		void render() override;
	};

	// Text shape - leaf node
	struct Text : public IComponent {
		// Args struct for construction (replaces designated initializers)
		struct Args {
			Foundation::Vec2	  position{0.0F, 0.0F};
			std::optional<float>  width = std::nullopt;
			std::optional<float>  height = std::nullopt;
			std::string			  text;
			Foundation::TextStyle style;
			const char*			  id = nullptr;
			short				  zIndex{0};
			bool				  visible{true};
			float				  margin{0.0F};
		};

		Foundation::Vec2	  position{0.0F, 0.0F};
		std::optional<float>  width = std::nullopt;	 // Optional bounding box width (also max wrap width)
		std::optional<float>  height = std::nullopt; // Optional bounding box height
		std::string			  text;
		Foundation::TextStyle style;
		const char*			  id = nullptr;

		Text() {
			widthMode = SizeMode::Hug;
			heightMode = SizeMode::Hug;
		}
		explicit Text(const Args& args)
			: position(args.position),
			  width(args.width),
			  height(args.height),
			  text(args.text),
			  style(args.style),
			  id(args.id) {
			zIndex = args.zIndex;
			IComponent::visible = args.visible;
			IComponent::margin = args.margin;
			widthMode = args.width.has_value() ? SizeMode::Fixed : SizeMode::Hug;
			heightMode = args.height.has_value() ? SizeMode::Fixed : SizeMode::Hug;
		}

		// Layout API
		// Returns explicit width/height if set, otherwise computes from text measurement
		float getWidth() const override;
		float getHeight() const override;
		void  setPosition(float x, float y) override { position = {x + margin, y + margin}; }

		// A parent-assigned width becomes the wrap width: with style.wordWrap
		// the text reflows and getHeight() reports the wrapped height (via the
		// FontRenderer measurement cache). Height assignment sets the vAlign
		// bounding box.
		void setLayoutSize(float w, float h) override {
			if (w >= 0.0F) {
				width = w;
			}
			if (h >= 0.0F) {
				height = h;
			}
		}

		// Debug introspection (position stores the content origin, so back out margin)
		Foundation::Vec2 getPosition() const override { return {position.x - margin, position.y - margin}; }
		const char*		 debugTypeName() const override { return "Text"; }
		const char*		 debugId() const override { return id; }

		void render() override;

	  private:
		// Mutable cache for lazy dimension computation (allows const getWidth/getHeight)
		mutable std::optional<float> cachedWidth;
		mutable std::optional<float> cachedHeight;
		mutable std::string			 cachedText;
		mutable float				 cachedFontSize{0.0F};
		mutable std::optional<float> cachedWrapWidth;
		mutable bool				 cachedWordWrap{false};

		// Ensure cache is valid, recompute if needed
		void ensureCacheValid() const;
	};

} // namespace UI

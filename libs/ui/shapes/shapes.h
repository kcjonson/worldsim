#pragma once

#include "component/component.h"
#include "graphics/color.h"
#include "graphics/primitive_styles.h"
#include "math/types.h"
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
		bool				  visible{true};
		const char*			  id = nullptr;
		short				  zIndex{0};
	};

	Foundation::Vec2	  position{0.0F, 0.0F};
	Foundation::Vec2	  size{100.0F, 100.0F};
	Foundation::RectStyle style;
	bool				  visible{true};
	const char*			  id = nullptr;

	Rectangle() = default;
	explicit Rectangle(const Args& args)
		: position(args.position), size(args.size), style(args.style), visible(args.visible), id(args.id) {
		zIndex = args.zIndex;
	}

	void Render() override;
};

// Circle shape - leaf node
struct Circle : public IComponent {
	// Args struct for construction (replaces designated initializers)
	struct Args {
		Foundation::Vec2		center{0.0F, 0.0F};
		float					radius{50.0F};
		Foundation::CircleStyle style;
		bool					visible{true};
		const char*				id = nullptr;
		short					zIndex{0};
	};

	Foundation::Vec2		center{0.0F, 0.0F};
	float					radius{50.0F};
	Foundation::CircleStyle style;
	bool					visible{true};
	const char*				id = nullptr;

	Circle() = default;
	explicit Circle(const Args& args)
		: center(args.center), radius(args.radius), style(args.style), visible(args.visible), id(args.id) {
		zIndex = args.zIndex;
	}

	void Render() override;
};

// Line shape - leaf node
struct Line : public IComponent {
	// Args struct for construction (replaces designated initializers)
	struct Args {
		Foundation::Vec2	  start{0.0F, 0.0F};
		Foundation::Vec2	  end{100.0F, 100.0F};
		Foundation::LineStyle style;
		bool				  visible{true};
		const char*			  id = nullptr;
		short				  zIndex{0};
	};

	Foundation::Vec2	  start{0.0F, 0.0F};
	Foundation::Vec2	  end{100.0F, 100.0F};
	Foundation::LineStyle style;
	bool				  visible{true};
	const char*			  id = nullptr;

	Line() = default;
	explicit Line(const Args& args)
		: start(args.start), end(args.end), style(args.style), visible(args.visible), id(args.id) {
		zIndex = args.zIndex;
	}

	void Render() override;
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
		bool				  visible{true};
		const char*			  id = nullptr;
		short				  zIndex{0};
	};

	Foundation::Vec2	  position{0.0F, 0.0F};
	std::optional<float>  width = std::nullopt;	 // Optional bounding box width
	std::optional<float>  height = std::nullopt; // Optional bounding box height
	std::string			  text;
	Foundation::TextStyle style;
	bool				  visible{true};
	const char*			  id = nullptr;

	Text() = default;
	explicit Text(const Args& args)
		: position(args.position), width(args.width), height(args.height),
		  text(args.text), style(args.style), visible(args.visible), id(args.id) {
		zIndex = args.zIndex;
	}

	void Render() override;
};

} // namespace UI

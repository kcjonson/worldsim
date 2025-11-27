#pragma once

#include "graphics/color.h"
#include "graphics/primitive_styles.h"
#include "layer/layer.h"
#include "math/types.h"
#include <optional>
#include <string>

// Basic shape types for UI layer system
// These are plain structs that call Primitives API during rendering
// All shapes satisfy the Layer concept with no-op HandleInput/Update methods
//
// See: /docs/technical/ui-framework/architecture.md

namespace UI {

	// Container - Pure hierarchy node with no visual representation
	// Used for grouping and organizing other layers
	struct Container {
		const char* id = nullptr;
		float		zIndex{-1.0F}; // -1.0F = auto-assign based on insertion order
		bool		visible{true};

		// Layer concept implementation (all no-ops for containers)
		void HandleInput() {}
		void Update(float /*deltaTime*/) {}
		void Render() const {}
	};

	// Rectangle shape
	struct Rectangle {
		Foundation::Vec2	  position{0.0F, 0.0F};
		Foundation::Vec2	  size{100.0F, 100.0F};
		Foundation::RectStyle style;
		float				  zIndex{-1.0F}; // -1.0F = auto-assign based on insertion order
		bool				  visible{true};
		const char*			  id = nullptr;

		// Layer concept implementation
		void HandleInput() {}
		void Update(float /*deltaTime*/) {}
		void Render() const;
	};

	// Circle shape
	struct Circle {
		Foundation::Vec2		center{0.0F, 0.0F};
		float					radius{50.0F};
		Foundation::CircleStyle style;
		float					zIndex{-1.0F}; // -1.0F = auto-assign based on insertion order
		bool					visible{true};
		const char*				id = nullptr;

		// Layer concept implementation
		void HandleInput() {}
		void Update(float /*deltaTime*/) {}
		void Render() const;
	};

	// Line shape
	struct Line {
		Foundation::Vec2	  start{0.0F, 0.0F};
		Foundation::Vec2	  end{100.0F, 100.0F};
		Foundation::LineStyle style;
		float				  zIndex{-1.0F}; // -1.0F = auto-assign based on insertion order
		bool				  visible{true};
		const char*			  id = nullptr;

		// Layer concept implementation
		void HandleInput() {}
		void Update(float /*deltaTime*/) {}
		void Render() const;
	};

	// Text shape
	struct Text {
		Foundation::Vec2	  position{0.0F, 0.0F};
		std::optional<float>  width = std::nullopt;  // Optional bounding box width
		std::optional<float>  height = std::nullopt; // Optional bounding box height
		std::string			  text;
		Foundation::TextStyle style;
		float				  zIndex{-1.0F}; // -1.0F = auto-assign based on insertion order
		bool				  visible{true};
		const char*			  id = nullptr;

		// Layer concept implementation
		void HandleInput() {}
		void Update(float /*deltaTime*/) {}
		void Render() const;
	};

	// Compile-time verification that all shapes satisfy the Layer concept
	static_assert(Layer<Container>, "Container must satisfy Layer concept");
	static_assert(Layer<Rectangle>, "Rectangle must satisfy Layer concept");
	static_assert(Layer<Circle>, "Circle must satisfy Layer concept");
	static_assert(Layer<Line>, "Line must satisfy Layer concept");
	static_assert(Layer<Text>, "Text must satisfy Layer concept");

} // namespace UI

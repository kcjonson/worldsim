#pragma once

#include "graphics/color.h"
#include "graphics/primitive_styles.h"
#include "math/types.h"
#include <string>

// Basic shape types for UI layer system
// These are plain structs that call Primitives API during rendering
// Research-aligned: value semantics, no pointers, contiguous storage

namespace UI {

	// Container - Pure hierarchy node with no visual representation
	// Used for grouping and organizing other layers
	struct Container {
		const char* id = nullptr;
		float		zIndex{-1.0F}; // -1.0F = auto-assign based on insertion order
		bool		visible{true};

		void Render() const {} // No-op: containers don't render
	};

	// Rectangle shape
	struct Rectangle {
		Foundation::Vec2	  position{0.0F, 0.0F};
		Foundation::Vec2	  size{100.0F, 100.0F};
		Foundation::RectStyle style;
		float				  zIndex{-1.0F}; // -1.0F = auto-assign based on insertion order
		bool				  visible{true};
		const char*			  id = nullptr;

		// Render this rectangle using Primitives API
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

		void Render() const;
	};

	// Text shape
	struct Text {
		Foundation::Vec2	  position{0.0F, 0.0F};
		std::string			  text;
		Foundation::TextStyle style;
		float				  zIndex{-1.0F}; // -1.0F = auto-assign based on insertion order
		bool				  visible{true};
		const char*			  id = nullptr;

		void Render() const;
	};

} // namespace UI

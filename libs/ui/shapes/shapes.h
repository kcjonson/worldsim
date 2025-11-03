#pragma once

#include "graphics/color.h"
#include "graphics/primitive_styles.h"
#include "math/types.h"
#include <string>

// Basic shape types for UI layer system
// These are plain structs that call Primitives API during rendering
// Research-aligned: value semantics, no pointers, contiguous storage

namespace UI {

	// Rectangle shape
	struct Rectangle {
		Foundation::Vec2	  position{0.0F, 0.0F};
		Foundation::Vec2	  size{100.0F, 100.0F};
		Foundation::RectStyle style;
		const char*			  id = nullptr; // Optional: for inspection/debugging

		// Render this rectangle using Primitives API
		void Render() const;
	};

	// Circle shape
	struct Circle {
		Foundation::Vec2  center{0.0F, 0.0F};
		float			  radius{50.0F};
		Foundation::Color color = Foundation::Color::White(); // TODO: Add CircleStyle
		const char*		  id = nullptr;

		void Render() const;
	};

	// Line shape
	struct Line {
		Foundation::Vec2	  start{0.0F, 0.0F};
		Foundation::Vec2	  end{100.0F, 100.0F};
		Foundation::LineStyle style;
		const char*			  id = nullptr;

		void Render() const;
	};

	// Text shape
	struct Text {
		Foundation::Vec2  position{0.0F, 0.0F};
		std::string		  text;
		Foundation::Color color = Foundation::Color::White(); // TODO: Add TextStyle
		const char*		  id = nullptr;

		void Render() const;
	};

} // namespace UI

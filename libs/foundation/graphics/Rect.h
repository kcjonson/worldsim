#pragma once

// Rectangle type for 2D axis-aligned bounding boxes.
// Used throughout the UI and rendering systems for positioning and bounds checking.
// Stores position (x, y) and size (width, height).

#include "math/Types.h"

namespace Foundation { // NOLINT(readability-identifier-naming)

	struct Rect {
		float x, y, width, height;

		// Constructors
		constexpr Rect()
			: x(0.0F),
			  y(0.0F),
			  width(0.0F),
			  height(0.0F) {}

		constexpr Rect(float x, float y, float width, float height)
			: x(x),
			  y(y),
			  width(width),
			  height(height) {}

		constexpr Rect(const Vec2& position, const Vec2& size)
			: x(position.x),
			  y(position.y),
			  width(size.x),
			  height(size.y) {}

		// Helper methods
		Vec2 position() const { return {x, y}; }
		Vec2 size() const { return {width, height}; }

		float left() const { return x; }
		float right() const { return x + width; }
		float top() const { return y; }
		float bottom() const { return y + height; }

		Vec2 topLeft() const { return {x, y}; }
		Vec2 topRight() const { return {x + width, y}; }
		Vec2 bottomLeft() const { return {x, y + height}; }
		Vec2 bottomRight() const { return {x + width, y + height}; }

		Vec2 center() const { return {x + (width * 0.5F), y + (height * 0.5F)}; }

		bool contains(const Vec2& point) const { return point.x >= x && point.x <= x + width && point.y >= y && point.y <= y + height; }

		bool intersects(const Rect& other) const {
			return x < other.x + other.width && x + width > other.x && y < other.y + other.height && y + height > other.y;
		}

		static Rect intersection(const Rect& a, const Rect& b) {
			float left = glm::max(a.x, b.x);
			float right = glm::min(a.x + a.width, b.x + b.width);
			float top = glm::max(a.y, b.y);
			float bottom = glm::min(a.y + a.height, b.y + b.height);

			if (right > left && bottom > top) {
				return {left, top, right - left, bottom - top};
			}

			return {}; // Empty rect
		}
	};

} // namespace Foundation

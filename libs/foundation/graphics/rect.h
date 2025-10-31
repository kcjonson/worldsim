#pragma once

// Rectangle type for 2D axis-aligned bounding boxes.
// Used throughout the UI and rendering systems for positioning and bounds checking.
// Stores position (x, y) and size (width, height).

#include "math/types.h"

namespace Foundation {

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
		Vec2 Position() const { return Vec2(x, y); }
		Vec2 Size() const { return Vec2(width, height); }

		float Left() const { return x; }
		float Right() const { return x + width; }
		float Top() const { return y; }
		float Bottom() const { return y + height; }

		Vec2 TopLeft() const { return Vec2(x, y); }
		Vec2 TopRight() const { return Vec2(x + width, y); }
		Vec2 BottomLeft() const { return Vec2(x, y + height); }
		Vec2 BottomRight() const { return Vec2(x + width, y + height); }

		Vec2 Center() const { return Vec2(x + width * 0.5F, y + height * 0.5F); }

		bool Contains(const Vec2& point) const { return point.x >= x && point.x <= x + width && point.y >= y && point.y <= y + height; }

		bool Intersects(const Rect& other) const {
			return x < other.x + other.width && x + width > other.x && y < other.y + other.height && y + height > other.y;
		}

		static Rect Intersection(const Rect& a, const Rect& b) {
			float left = glm::max(a.x, b.x);
			float right = glm::min(a.x + a.width, b.x + b.width);
			float top = glm::max(a.y, b.y);
			float bottom = glm::min(a.y + a.height, b.y + b.height);

			if (right > left && bottom > top) {
				return Rect(left, top, right - left, bottom - top);
			}

			return Rect(); // Empty rect
		}
	};

} // namespace Foundation

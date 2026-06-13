#pragma once

#include "Int128.h"

#include <math/Types.h>

#include <cmath>
#include <cstdint>

// Integer-quantized 2D point in millimeters. Committed geometry lives here so
// that vertex equality is exact `==` and orientation/intersection predicates
// are exact with 128-bit intermediates (see building-construction D1).

namespace geometry {

	constexpr std::int64_t kMillimetersPerMeter = 1000;

	struct Vec2i64 {
		std::int64_t x = 0;
		std::int64_t y = 0;

		constexpr Vec2i64() = default;
		constexpr Vec2i64(std::int64_t x, std::int64_t y) : x(x), y(y) {}

		bool operator==(const Vec2i64& rhs) const { return x == rhs.x && y == rhs.y; }
		bool operator!=(const Vec2i64& rhs) const { return !(*this == rhs); }

		// Lexicographic order (x then y): a total order for sorting and map keys.
		bool operator<(const Vec2i64& rhs) const { return x != rhs.x ? x < rhs.x : y < rhs.y; }
		bool operator>(const Vec2i64& rhs) const { return rhs < *this; }
		bool operator<=(const Vec2i64& rhs) const { return !(rhs < *this); }
		bool operator>=(const Vec2i64& rhs) const { return !(*this < rhs); }

		Vec2i64 operator+(const Vec2i64& rhs) const { return {x + rhs.x, y + rhs.y}; }
		Vec2i64 operator-(const Vec2i64& rhs) const { return {x - rhs.x, y - rhs.y}; }
		Vec2i64 operator-() const { return {-x, -y}; }
		Vec2i64 operator*(std::int64_t s) const { return {x * s, y * s}; }
	};

	inline Vec2i64 operator*(std::int64_t s, const Vec2i64& v) { return v * s; }

	// Dot and cross products promote to 128-bit because the operands are
	// products of int64 differences; int64 would overflow for large coordinates.
	inline Int128 dot(const Vec2i64& a, const Vec2i64& b) {
		return Int128::product(a.x, b.x) + Int128::product(a.y, b.y);
	}

	inline Int128 cross(const Vec2i64& a, const Vec2i64& b) {
		return Int128::product(a.x, b.y) - Int128::product(a.y, b.x);
	}

	inline Vec2i64 quantize(const Foundation::Vec2& meters) {
		return {
			std::llround(static_cast<double>(meters.x) * static_cast<double>(kMillimetersPerMeter)),
			std::llround(static_cast<double>(meters.y) * static_cast<double>(kMillimetersPerMeter)),
		};
	}

	inline Foundation::Vec2 dequantize(const Vec2i64& mm) {
		return {
			static_cast<float>(static_cast<double>(mm.x) / static_cast<double>(kMillimetersPerMeter)),
			static_cast<float>(static_cast<double>(mm.y) / static_cast<double>(kMillimetersPerMeter)),
		};
	}

} // namespace geometry

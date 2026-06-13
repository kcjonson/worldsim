#pragma once

#include <cstdint>

// Minimal signed 128-bit integer for exact geometry predicates and area
// accumulation. Only the operations the predicates need are provided: products
// of two int64 values, addition/subtraction of such products, negation,
// comparison, and sign extraction.
//
// MSVC x64 has no __int128. Where the platform offers a native 128-bit integer
// (GCC/Clang) we delegate to it; on MSVC we use the _mul128 intrinsic for the
// widening multiply and carry the two-limb arithmetic by hand.

#if defined(_MSC_VER) && defined(_M_X64)
	#include <intrin.h>
	#pragma intrinsic(_mul128, _umul128)
	#define GEOMETRY_INT128_NATIVE 0
#elif defined(__SIZEOF_INT128__)
	#define GEOMETRY_INT128_NATIVE 1
#else
	#error "geometry::Int128 requires a 64-bit MSVC target or a compiler with __int128"
#endif

namespace geometry {

#if GEOMETRY_INT128_NATIVE

	class Int128 {
	  public:
		constexpr Int128() = default;
		constexpr explicit Int128(std::int64_t value) : value(value) {}

		static Int128 product(std::int64_t a, std::int64_t b) {
			return Int128(static_cast<__int128>(a) * static_cast<__int128>(b));
		}

		Int128 operator+(const Int128& rhs) const { return Int128(value + rhs.value, tag{}); }
		Int128 operator-(const Int128& rhs) const { return Int128(value - rhs.value, tag{}); }
		Int128 operator-() const { return Int128(-value, tag{}); }

		bool operator==(const Int128& rhs) const { return value == rhs.value; }
		bool operator!=(const Int128& rhs) const { return value != rhs.value; }
		bool operator<(const Int128& rhs) const { return value < rhs.value; }
		bool operator>(const Int128& rhs) const { return value > rhs.value; }
		bool operator<=(const Int128& rhs) const { return value <= rhs.value; }
		bool operator>=(const Int128& rhs) const { return value >= rhs.value; }

		int sign() const { return value < 0 ? -1 : (value > 0 ? 1 : 0); }

		// Lossy: only valid when the value fits in a double's mantissa. Used for
		// UI readouts (areas, distances), never on the exact comparison path.
		double toDouble() const { return static_cast<double>(value); }

		// Sign of (c*c - a*b), computed exactly. Inputs must be non-negative.
		// Used for the interior point-to-segment comparison, where both products
		// reach 256 bits. Returns -1, 0, or +1.
		static int compareSquareToProduct(const Int128& c, const Int128& a, const Int128& b);

	  private:
		struct tag {};
		constexpr Int128(__int128 raw, tag) : value(raw) {}

		void magnitudeLimbs(std::uint64_t& low, std::uint64_t& high) const {
			unsigned __int128 mag = value < 0 ? static_cast<unsigned __int128>(-value) : static_cast<unsigned __int128>(value);
			low	 = static_cast<std::uint64_t>(mag);
			high = static_cast<std::uint64_t>(mag >> 64);
		}

		__int128 value = 0;
	};

#else

	class Int128 {
	  public:
		constexpr Int128() = default;
		constexpr explicit Int128(std::int64_t value)
			: lo(static_cast<std::uint64_t>(value)), hi(value < 0 ? -1 : 0) {}

		static Int128 product(std::int64_t a, std::int64_t b) {
			std::int64_t  high = 0;
			std::uint64_t low  = static_cast<std::uint64_t>(_mul128(a, b, &high));
			return Int128(low, high);
		}

		Int128 operator+(const Int128& rhs) const {
			std::uint64_t low	= lo + rhs.lo;
			std::int64_t  carry = (low < lo) ? 1 : 0;
			std::int64_t  high	= hi + rhs.hi + carry;
			return Int128(low, high);
		}

		Int128 operator-(const Int128& rhs) const { return *this + (-rhs); }

		Int128 operator-() const {
			std::uint64_t low  = ~lo + 1U;
			std::int64_t  high = static_cast<std::int64_t>(~static_cast<std::uint64_t>(hi) + (low == 0 ? 1U : 0U));
			return Int128(low, high);
		}

		bool operator==(const Int128& rhs) const { return lo == rhs.lo && hi == rhs.hi; }
		bool operator!=(const Int128& rhs) const { return !(*this == rhs); }

		bool operator<(const Int128& rhs) const {
			if (hi != rhs.hi) {
				return hi < rhs.hi;
			}
			return lo < rhs.lo;
		}
		bool operator>(const Int128& rhs) const { return rhs < *this; }
		bool operator<=(const Int128& rhs) const { return !(rhs < *this); }
		bool operator>=(const Int128& rhs) const { return !(*this < rhs); }

		int sign() const {
			if (hi < 0) {
				return -1;
			}
			if (hi == 0 && lo == 0) {
				return 0;
			}
			return 1;
		}

		double toDouble() const {
			// Build from the magnitude then apply sign: combining a negative hi
			// limb with a large unsigned lo limb directly would catastrophically
			// cancel (the lo cast rounds, the 2^64 terms then nearly subtract out).
			constexpr double kTwoPow64 = 18446744073709551616.0;
			std::uint64_t	 low	   = 0;
			std::uint64_t	 high	   = 0;
			magnitudeLimbs(low, high);
			double mag = static_cast<double>(high) * kTwoPow64 + static_cast<double>(low);
			return hi < 0 ? -mag : mag;
		}

		// Sign of (c*c - a*b), computed exactly. Inputs must be non-negative.
		// Used for the interior point-to-segment comparison, where both products
		// reach 256 bits. Returns -1, 0, or +1.
		static int compareSquareToProduct(const Int128& c, const Int128& a, const Int128& b);

	  private:
		constexpr Int128(std::uint64_t low, std::int64_t high) : lo(low), hi(high) {}

		void magnitudeLimbs(std::uint64_t& low, std::uint64_t& high) const {
			if (hi < 0) {
				std::uint64_t negLo = ~lo + 1U;
				std::uint64_t negHi = ~static_cast<std::uint64_t>(hi) + (negLo == 0 ? 1U : 0U);
				low	 = negLo;
				high = negHi;
			} else {
				low	 = lo;
				high = static_cast<std::uint64_t>(hi);
			}
		}

		std::uint64_t lo = 0;
		std::int64_t  hi = 0;
	};

#endif

	namespace detail {

		// 256-bit unsigned magnitude as four 64-bit limbs, little-endian.
		struct U256 {
			std::uint64_t limb[4] = {0, 0, 0, 0};
		};

		inline void mul64(std::uint64_t a, std::uint64_t b, std::uint64_t& low, std::uint64_t& high) {
#if defined(_MSC_VER) && defined(_M_X64)
			low = _umul128(a, b, &high);
#else
			unsigned __int128 product = static_cast<unsigned __int128>(a) * static_cast<unsigned __int128>(b);
			low						  = static_cast<std::uint64_t>(product);
			high					  = static_cast<std::uint64_t>(product >> 64);
#endif
		}

		// Multiply two 128-bit magnitudes (limb pairs) into a 256-bit magnitude.
		inline U256 mul128(std::uint64_t aLo, std::uint64_t aHi, std::uint64_t bLo, std::uint64_t bHi) {
			U256 r;
			std::uint64_t lo = 0;
			std::uint64_t hi = 0;

			mul64(aLo, bLo, lo, hi);
			r.limb[0] = lo;
			std::uint64_t carry = hi;

			mul64(aLo, bHi, lo, hi);
			std::uint64_t mid = r.limb[1] + lo;
			std::uint64_t c1  = (mid < lo) ? 1U : 0U;
			std::uint64_t sum = mid + carry;
			std::uint64_t c2  = (sum < mid) ? 1U : 0U;
			r.limb[1]		  = sum;
			carry			  = hi + c1 + c2;

			mul64(aHi, bLo, lo, hi);
			mid		  = r.limb[1] + lo;
			c1		  = (mid < lo) ? 1U : 0U;
			r.limb[1] = mid;
			std::uint64_t hi2 = hi + c1;
			std::uint64_t add = carry + hi2;
			std::uint64_t c3  = (add < carry) ? 1U : 0U;
			r.limb[2]		  = add;
			std::uint64_t carry2 = c3;

			mul64(aHi, bHi, lo, hi);
			std::uint64_t t = r.limb[2] + lo;
			c1				= (t < lo) ? 1U : 0U;
			r.limb[2]		= t;
			r.limb[3]		= hi + c1 + carry2;
			return r;
		}

		inline int compareU256(const U256& a, const U256& b) {
			for (int i = 3; i >= 0; --i) {
				if (a.limb[i] != b.limb[i]) {
					return a.limb[i] < b.limb[i] ? -1 : 1;
				}
			}
			return 0;
		}

	} // namespace detail

	inline int Int128::compareSquareToProduct(const Int128& c, const Int128& a, const Int128& b) {
		std::uint64_t cLo = 0;
		std::uint64_t cHi = 0;
		std::uint64_t aLo = 0;
		std::uint64_t aHi = 0;
		std::uint64_t bLo = 0;
		std::uint64_t bHi = 0;
		c.magnitudeLimbs(cLo, cHi);
		a.magnitudeLimbs(aLo, aHi);
		b.magnitudeLimbs(bLo, bHi);

		detail::U256 lhs = detail::mul128(cLo, cHi, cLo, cHi);
		detail::U256 rhs = detail::mul128(aLo, aHi, bLo, bHi);
		return detail::compareU256(lhs, rhs);
	}

} // namespace geometry

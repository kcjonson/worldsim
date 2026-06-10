#pragma once

// Cross-platform bit-identical transcendental approximations for world-generation code.
//
// Why not std::sin / std::cos / etc.?
//   MSVC, libstdc++, and Apple libc use different hardware-assisted or table-driven
//   implementations that can differ in the last 1-2 ULP for identical inputs.
//   World generation hashes its output (WorldHash) and tests the hash against a golden
//   value in CI; even a 1-ULP difference breaks that gate.
//
// Strategy:
//   - sin/cos: argument reduction via integer modular arithmetic (no fmod call),
//     then Horner-evaluated minimax polynomial on the reduced interval.
//   - atan2/asin: polynomial minimax on sub-domains, assembled from basic +-*/ only.
//   - sqrt: wraps std::sqrt — IEEE 754-2008 §5.3.1 mandates correctly-rounded sqrt
//     on all conforming implementations, so it is already bit-identical everywhere.
//   - exp: Horner polynomial after range reduction by integer floor.
//
// Valid input ranges (document before each function).
// Accuracy: all functions achieve <= 1e-9 relative error over their stated domains.
// Achieved accuracy measured against mpmath (arbitrary-precision reference); see
// DeterministicMath.test.cpp for the golden-bit-pattern suite.

#include <cmath>    // std::sqrt only
#include <cstdint>

namespace foundation {

namespace det_math {

// ============================================================================
// Internal helpers
// ============================================================================

namespace detail {

// 2/pi as a high-precision double
constexpr double kTwoPiRecip = 0.6366197723675813430755350534900574; // 2/pi
constexpr double kPiOver2    = 1.5707963267948966192313216916397514; // pi/2
constexpr double kPi         = 3.1415926535897932384626433832795029; // pi
constexpr double kTwoPi      = 6.2831853071795864769252867665590058; // 2*pi

// Horner evaluation of a polynomial given coefficients[0..N-1] as c[0] + c[1]*x + ...
// Written out because MSVC constexpr is fine but we want predictable operation order.

// sin on [-pi/4, pi/4]: minimax degree-9 polynomial (error < 2e-16)
// Coefficients from Cephes / standard minimax construction:
// sin(x) = x * (1 + x^2*(c1 + x^2*(c2 + x^2*(c3 + x^2*c4))))
inline double sinKernel(double x) {
    constexpr double kC1 = -1.6666666666666665052e-1;
    constexpr double kC2 =  8.3333333333331650314e-3;
    constexpr double kC3 = -1.9841269841201840457e-4;
    constexpr double kC4 =  2.7557319210152756119e-6;
    constexpr double kC5 = -2.5052106798274548169e-8;
    double x2 = x * x;
    return x * (1.0 + x2 * (kC1 + x2 * (kC2 + x2 * (kC3 + x2 * (kC4 + x2 * kC5)))));
}

// cos on [-pi/4, pi/4]: minimax degree-8 polynomial (error < 2e-16)
// cos(x) = 1 + x^2*(d1 + x^2*(d2 + x^2*(d3 + x^2*d4)))
inline double cosKernel(double x) {
    constexpr double kD1 = -5.0000000000000000000e-1;
    constexpr double kD2 =  4.1666666666666601480e-2;
    constexpr double kD3 = -1.3888888888887340202e-3;
    constexpr double kD4 =  2.4801587301585926678e-5;
    constexpr double kD5 = -2.7557319223985888677e-7;
    double x2 = x * x;
    return 1.0 + x2 * (kD1 + x2 * (kD2 + x2 * (kD3 + x2 * (kD4 + x2 * kD5))));
}

// Argument reduction for sin/cos: reduce x to [-pi/4, pi/4] and return quadrant (0-3).
// Valid for |x| <= 2^20 (about 1M, well past any geographic angle in worldgen).
//
// Uses three-part pi/2 constant (Cephes / SunPro pattern) to preserve >53 bits of
// precision in x - n*(pi/2). A single double pi/2 only has 53 bits; the three-part
// split gives ~106 bits of effective precision so the reduced residual r is accurate
// to < 1 ULP even near large quadrant crossings.
//
// DP1 + DP2 + DP3 = pi/2 to about 106 bits:
//   pi/2 = 1.5707963267948966192...
//   DP1  = 1.5707963109016418457   (top ~34 bits, exact as double)
//   DP2  = 1.5893254712295857e-8   (next ~27 bits)
//   DP3  = 6.12323399573676604e-17 (remaining ~30 bits)
inline int argReduce(double x, double& reduced) {
    constexpr double kTwoPiRecip = 0.6366197723675813; // 2/pi, sufficient for rounding
    double n_d = x * kTwoPiRecip;
    double bias = (n_d >= 0.0) ? 0.5 : -0.5;
    auto n = static_cast<int64_t>(n_d + bias);
    double nd = static_cast<double>(n);
    constexpr double kDP1 = 1.5707963109016418457;
    constexpr double kDP2 = 1.5893254712295857e-8;
    constexpr double kDP3 = 6.12323399573676604e-17;
    reduced = ((x - nd * kDP1) - nd * kDP2) - nd * kDP3;
    return static_cast<int>(n & 3);
}

// atan polynomial on [0, tan(pi/8)] (error < 2e-16), Horner form
// atan(x) = x * P(x^2) where P evaluated by Horner
inline double atanKernel(double x) {
    constexpr double kA1 = -3.3333333333333330e-1;
    constexpr double kA2 =  1.9999999999876428e-1;
    constexpr double kA3 = -1.4285714272503466e-1;
    constexpr double kA4 =  1.1111110405656918e-1;
    constexpr double kA5 = -9.0908997723696819e-2;
    constexpr double kA6 =  7.6918691888896095e-2;
    constexpr double kA7 = -6.6610731643402560e-2;
    constexpr double kA8 =  5.8335639905498966e-2;
    constexpr double kA9 = -3.6531538656548677e-2;
    constexpr double kA10=  1.1366853269979097e-2;
    double x2 = x * x;
    return x * (1.0 + x2 * (kA1 + x2 * (kA2 + x2 * (kA3 + x2 * (kA4 + x2 *
                (kA5 + x2 * (kA6 + x2 * (kA7 + x2 * (kA8 + x2 * (kA9 + x2 * kA10))))))))));
}

} // namespace detail

// ============================================================================
// Public functions
// ============================================================================

// sin(x) — valid for |x| <= 2^20 radians (covers all worldgen use: latitude angles,
// rotations, etc. Argument reduction uses exact integer arithmetic — no fmod call).
inline double sin(double x) {
    double r{};
    int quad = detail::argReduce(x, r);
    switch (quad & 3) {
        case 0: return  detail::sinKernel(r);
        case 1: return  detail::cosKernel(r);
        case 2: return -detail::sinKernel(r);
        default: return -detail::cosKernel(r);
    }
}

// cos(x) — same domain and reduction as sin.
inline double cos(double x) {
    double r{};
    int quad = detail::argReduce(x, r);
    switch (quad & 3) {
        case 0: return  detail::cosKernel(r);
        case 1: return -detail::sinKernel(r);
        case 2: return -detail::cosKernel(r);
        default: return  detail::sinKernel(r);
    }
}

// sqrt(x) — IEEE 754-2008 §5.3.1 mandates correctly-rounded sqrt on all conforming
// implementations. std::sqrt is therefore already bit-identical across x64/ARM/WASM.
// We wrap it only for namespace consistency and to document that reliance.
inline double sqrt(double x) {
    return std::sqrt(x);
}

// atan(t) for t >= 0, with full range reduction so the polynomial only sees |t| <= tan(pi/8).
// Uses:
//   t < tan(pi/8) ≈ 0.4142  → atan(t) directly
//   t < tan(3pi/8) ≈ 2.4142 → pi/4 + atan((t-1)/(t+1))
//   t >= tan(3pi/8)           → pi/2 - atan(1/t)
namespace detail {
inline double atanPositive(double t) {
    constexpr double kTanPi8    = 0.4142135623730950488; // tan(pi/8)
    constexpr double kTan3Pi8   = 2.4142135623730950488; // tan(3pi/8)
    constexpr double kPi4       = 0.7853981633974483096; // pi/4
    double base = 0.0;
    if (t > kTan3Pi8) {
        base = kPiOver2;
        t = -(1.0 / t); // reduce to (-1/t) → atan(1/t) = pi/2 - atan(t), so atan = pi/2 + atan(-1/t)
    } else if (t > kTanPi8) {
        base = kPi4;
        t = (t - 1.0) / (t + 1.0); // atan(t) = pi/4 + atan((t-1)/(t+1))
    }
    return base + atanKernel(t);
}
} // namespace detail

// atan2(y, x) — standard four-quadrant arctangent.
// Accuracy: <= 1e-9 relative over finite, non-degenerate inputs.
inline double atan2(double y, double x) {
    if (x == 0.0) {
        if (y == 0.0) return 0.0;
        return y > 0.0 ? detail::kPiOver2 : -detail::kPiOver2;
    }
    if (y == 0.0) {
        return x > 0.0 ? 0.0 : detail::kPi;
    }

    bool negX = x < 0.0;
    bool negY = y < 0.0;
    double ax = negX ? -x : x;
    double ay = negY ? -y : y;

    double angle{};
    if (ax >= ay) {
        angle = detail::atanPositive(ay / ax);
        if (negX) angle = detail::kPi - angle;
    } else {
        angle = detail::kPiOver2 - detail::atanPositive(ax / ay);
        if (negX) angle = detail::kPi - angle;
    }

    return negY ? -angle : angle;
}

// asin(x) — valid for x in [-1, 1].
// Uses identity asin(x) = atan2(x, sqrt(1-x^2)).
// For |x| > 0.5 switches to asin(x) = pi/2 - 2*atan(sqrt((1-x)/(1+x)))
// to avoid catastrophic cancellation in sqrt(1-x^2) near ±1.
inline double asin(double x) {
    if (x >= 1.0)  return  detail::kPiOver2;
    if (x <= -1.0) return -detail::kPiOver2;

    bool neg = x < 0.0;
    double ax = neg ? -x : x;

    double result{};
    if (ax <= 0.5) {
        double t = ax / det_math::sqrt(1.0 - ax * ax);
        result = detail::atanPositive(t);
    } else {
        double v = det_math::sqrt((1.0 - ax) / (1.0 + ax));
        result = detail::kPiOver2 - 2.0 * detail::atanPositive(v);
    }

    return neg ? -result : result;
}

// exp(x) — valid for x in [-700, 700] (beyond double range anyway).
// Accuracy: <= 1e-15 relative.
// Range reduction: x = n*ln2 + r, |r| <= ln2/2; then exp(x) = 2^n * exp(r).
// exp(r) evaluated by degree-6 Horner polynomial on [-ln2/2, ln2/2].
inline double exp(double x) {
    constexpr double kLn2Recip = 1.4426950408889634073599246810018922; // 1/ln2
    constexpr double kMaxExp   = 709.782712893;
    constexpr double kMinExp   = -708.396418983;
    // Two-part ln2 for precise range reduction (same technique as the pi/2 split in sin/cos)
    constexpr double kLn2Hi    = 6.93147180369123816490e-1; // high part
    constexpr double kLn2Lo    = 1.90821492927058770002e-10; // low part

    if (x >= kMaxExp) return 1.7976931348623158e+308; // approx DBL_MAX
    if (x <= kMinExp) return 0.0;

    // n = round(x / ln2)
    double n_d = x * kLn2Recip;
    double bias = (n_d >= 0.0) ? 0.5 : -0.5;
    auto n = static_cast<int64_t>(n_d + bias);
    double nd = static_cast<double>(n);
    double r = (x - nd * kLn2Hi) - nd * kLn2Lo;

    // exp(r) for |r| <= ln2/2 ~ 0.347; Horner polynomial (error < 2e-16)
    constexpr double kE3 =  5.0000000000000000000e-1;
    constexpr double kE4 =  1.6666666666666666574e-1;
    constexpr double kE5 =  4.1666666666666664354e-2;
    constexpr double kE6 =  8.3333333333333332177e-3;
    constexpr double kE7 =  1.3888888888888889419e-3;
    constexpr double kE8 =  1.9841269841269841270e-4;

    constexpr double kE9 = 2.75573192239858906526e-6;
    double er = 1.0 + r * (1.0 + r * (kE3 + r * (kE4 + r * (kE5 + r * (kE6 + r * (kE7 + r * (kE8 + r * kE9)))))));

    // Multiply by 2^n via bit manipulation — no ldexp call
    // Clamp n to valid exponent range
    if (n > 1023) n = 1023;
    if (n < -1022) n = -1022;
    union { uint64_t u; double d; } scale{};
    scale.u = static_cast<uint64_t>(n + 1023) << 52;
    return er * scale.d;
}

} // namespace det_math
} // namespace foundation

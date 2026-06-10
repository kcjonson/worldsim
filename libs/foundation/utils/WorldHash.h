#pragma once

// FNV-1a 64-bit hash utilities for world determinism checking.
//
// FNV-1a is chosen for its simplicity and speed on streaming byte data.
// Output is NOT cryptographic; it's used to detect accidental non-determinism
// (different seeds or thread counts producing different worlds) in CI.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace foundation {

constexpr uint64_t kFnvOffset = 14695981039346656037ULL; // FNV offset basis
constexpr uint64_t kFnvPrime  = 1099511628211ULL;        // FNV prime

// Hash a byte span, starting from seed (default: FNV offset basis).
// Calling hashBytes(buf, len, hashBytes(buf2, len2)) chains two spans correctly.
inline uint64_t hashBytes(const void* data, size_t len, uint64_t seed = kFnvOffset) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint64_t h = seed;
    for (size_t i = 0; i < len; ++i) {
        h ^= static_cast<uint64_t>(bytes[i]); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        h *= kFnvPrime;
    }
    return h;
}

// Combine two hashes in an order-sensitive way.
// hashCombine(a, b) != hashCombine(b, a) for a != b.
inline uint64_t hashCombine(uint64_t a, uint64_t b) {
    // FNV-fold b into a byte by byte so the operation inherits FNV's mixing
    uint8_t buf[8];
    std::memcpy(buf, &b, 8);
    return hashBytes(buf, 8, a);
}

// Hash a contiguous vector of trivially-copyable T.
template<typename T>
uint64_t hashSpan(const std::vector<T>& v, uint64_t seed = kFnvOffset) {
    static_assert(std::is_trivially_copyable_v<T>,
        "hashSpan requires trivially-copyable element type");
    return hashBytes(v.data(), v.size() * sizeof(T), seed);
}

} // namespace foundation

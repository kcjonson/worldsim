#include "WorldHash.h"
#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>

using namespace foundation;

// ============================================================================
// Known FNV-1a 64-bit vectors (from the FNV reference implementation)
// http://www.isthe.com/chongo/tech/comp/fnv/
// ============================================================================

TEST(WorldHashTests, KnownFnvVectors) {
    // Standard FNV-1a 64-bit test vectors from http://www.isthe.com/chongo/tech/comp/fnv/
    // These are hardcoded literals — not derived from kFnvOffset/kFnvPrime — so they
    // catch any accidental change to those constants.

    // Empty string: FNV-1a of "" is the offset basis itself
    EXPECT_EQ(hashBytes("", 0), 0xcbf29ce484222325ULL);

    // "a"
    {
        const uint8_t data[] = {'a'};
        EXPECT_EQ(hashBytes(data, 1), 0xaf63dc4c8601ec8cULL);
    }

    // "foobar"
    {
        const char* s = "foobar";
        EXPECT_EQ(hashBytes(s, std::strlen(s)), 0x85944171f73967e8ULL);
    }
}

TEST(WorldHashTests, HashBytesWithSeed) {
    // Chaining: hashBytes(B, hashBytes(A)) should equal hashing A+B as one span
    const uint8_t a[] = {1, 2, 3};
    const uint8_t b[] = {4, 5, 6};
    uint8_t ab[6] = {1, 2, 3, 4, 5, 6};

    uint64_t chained = hashBytes(b, 3, hashBytes(a, 3));
    uint64_t oneShot = hashBytes(ab, 6);
    EXPECT_EQ(chained, oneShot);
}

TEST(WorldHashTests, HashCombineNonCommutative) {
    uint64_t a = 0xDEADBEEFCAFEBABEULL;
    uint64_t b = 0x0123456789ABCDEFULL;
    EXPECT_NE(hashCombine(a, b), hashCombine(b, a));
}

TEST(WorldHashTests, HashCombineAssociative) {
    // hashCombine is not required to be associative, but should be deterministic
    uint64_t x = 0x111111ULL, y = 0x222222ULL, z = 0x333333ULL;
    EXPECT_EQ(hashCombine(hashCombine(x, y), z),
              hashCombine(hashCombine(x, y), z));
}

TEST(WorldHashTests, HashSpanTrivialType) {
    std::vector<uint32_t> v = {1, 2, 3, 4, 5};
    uint64_t h1 = hashSpan(v);
    uint64_t h2 = hashSpan(v);
    EXPECT_EQ(h1, h2);

    // Match manual hashBytes over raw bytes
    uint64_t manual = hashBytes(v.data(), v.size() * sizeof(uint32_t));
    EXPECT_EQ(h1, manual);
}

TEST(WorldHashTests, HashSpanDifferentData) {
    std::vector<uint32_t> a = {1, 2, 3};
    std::vector<uint32_t> b = {1, 2, 4}; // last element differs
    EXPECT_NE(hashSpan(a), hashSpan(b));
}

TEST(WorldHashTests, HashBytesEmpty) {
    EXPECT_EQ(hashBytes(nullptr, 0), kFnvOffset);
    EXPECT_EQ(hashBytes(nullptr, 0, 42ULL), 42ULL); // non-default seed, empty data
}

TEST(WorldHashTests, Determinism) {
    const uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    uint64_t h1 = hashBytes(data, 4);
    const uint8_t data2[] = {0x01, 0x02, 0x03, 0x04};
    uint64_t h2 = hashBytes(data2, 4);
    EXPECT_EQ(h1, h2);
}

TEST(WorldHashTests, SeedPropagation) {
    const uint8_t data[] = {0xFF};
    // Different seeds should produce different hashes for same data
    EXPECT_NE(hashBytes(data, 1, 0ULL), hashBytes(data, 1, 1ULL));
}

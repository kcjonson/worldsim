#include "world/BiomeWeights.h"

#include <gtest/gtest.h>

using namespace engine::world;

// Sum of raw uint8 weights (not divided by 255).
static uint32_t rawSum(const BiomeWeights& bw) {
    uint32_t s = 0;
    for (uint8_t i = 0; i < bw.count; ++i) s += bw.entries[i].weight;
    return s;
}

// ============================================================================
// set() – out-of-range clamping
// ============================================================================

TEST(BiomeWeightsSetTests, NegativeWeightClampedToZero) {
    BiomeWeights bw;
    bw.set(Biome::Ocean, -0.5F);
    EXPECT_FLOAT_EQ(bw.get(Biome::Ocean), 0.0F);
    EXPECT_EQ(bw.count, 0); // w==0 skips insert
}

TEST(BiomeWeightsSetTests, OverOneWeightClampedToOne) {
    BiomeWeights bw;
    bw.set(Biome::Ocean, 1.5F);
    // clamped to 1.0 => quantized to 255
    EXPECT_EQ(bw.count, 1);
    EXPECT_EQ(bw.entries[0].weight, 255);
    EXPECT_FLOAT_EQ(bw.get(Biome::Ocean), 1.0F);
}

TEST(BiomeWeightsSetTests, LargePositiveWeightClampedToOne) {
    BiomeWeights bw;
    bw.set(Biome::HotDesert, 99.0F);
    EXPECT_EQ(bw.entries[0].weight, 255);
}

TEST(BiomeWeightsSetTests, ExactlyZeroWeightInsertsNothing) {
    BiomeWeights bw;
    bw.set(Biome::Lake, 0.0F);
    EXPECT_EQ(bw.count, 0);
}

TEST(BiomeWeightsSetTests, ExactlyOneWeightIsMax) {
    BiomeWeights bw;
    bw.set(Biome::Lake, 1.0F);
    EXPECT_EQ(bw.entries[0].weight, 255);
}

TEST(BiomeWeightsSetTests, UpdateExistingEntryClampsCorrectly) {
    BiomeWeights bw;
    bw.set(Biome::Ocean, 0.5F);
    bw.set(Biome::Ocean, 2.0F); // update + clamp
    EXPECT_EQ(bw.count, 1);
    EXPECT_EQ(bw.entries[0].weight, 255);
}

TEST(BiomeWeightsSetTests, NegativeUpdateClampsToZero) {
    BiomeWeights bw;
    bw.set(Biome::Ocean, 0.5F);
    bw.set(Biome::Ocean, -1.0F); // clamp => w=0, entry stays with weight=0
    EXPECT_EQ(bw.entries[0].weight, 0);
}

// ============================================================================
// normalize() – exact sum of 255 after various distributions
// ============================================================================

TEST(BiomeWeightsNormalizeTests, SingleEntryIsExactly255) {
    BiomeWeights bw;
    bw.set(Biome::Ocean, 0.7F);
    bw.normalize();
    EXPECT_EQ(rawSum(bw), 255u);
}

TEST(BiomeWeightsNormalizeTests, TwoEqualWeightsSum255) {
    BiomeWeights bw;
    bw.set(Biome::Ocean, 0.5F);
    bw.set(Biome::Lake, 0.5F);
    bw.normalize();
    EXPECT_EQ(rawSum(bw), 255u);
}

TEST(BiomeWeightsNormalizeTests, ThreeEqualWeightsSum255) {
    // 255 / 3 = 85, remainder 0 — already exact.
    BiomeWeights bw;
    bw.set(Biome::Ocean,     1.0F);
    bw.set(Biome::Lake,      1.0F);
    bw.set(Biome::HotDesert, 1.0F);
    bw.normalize();
    EXPECT_EQ(rawSum(bw), 255u);
}

TEST(BiomeWeightsNormalizeTests, FourEqualWeightsSum255) {
    // 255 / 4 = 63 r3 — three entries get +1, total = 63*4+3 = 255.
    BiomeWeights bw;
    bw.set(Biome::Ocean,            1.0F);
    bw.set(Biome::Lake,             1.0F);
    bw.set(Biome::HotDesert,        1.0F);
    bw.set(Biome::TropicalRainforest, 1.0F);
    bw.normalize();
    EXPECT_EQ(rawSum(bw), 255u);
}

TEST(BiomeWeightsNormalizeTests, UnevenWeightsSum255) {
    // 1:2:3 ratio — truncation would leave 85+255*2/6+... let the impl handle it.
    BiomeWeights bw;
    bw.entries[0] = { static_cast<uint8_t>(Biome::Ocean),    1 };
    bw.entries[1] = { static_cast<uint8_t>(Biome::Lake),     2 };
    bw.entries[2] = { static_cast<uint8_t>(Biome::HotDesert),3 };
    bw.count = 3;
    bw.normalize();
    EXPECT_EQ(rawSum(bw), 255u);
}

TEST(BiomeWeightsNormalizeTests, LargelySkewedWeightsSum255) {
    // One heavy, three tiny — all four entries present.
    BiomeWeights bw;
    bw.entries[0] = { static_cast<uint8_t>(Biome::Ocean),            200 };
    bw.entries[1] = { static_cast<uint8_t>(Biome::Lake),               1 };
    bw.entries[2] = { static_cast<uint8_t>(Biome::HotDesert),          1 };
    bw.entries[3] = { static_cast<uint8_t>(Biome::TropicalRainforest), 1 };
    bw.count = 4;
    bw.normalize();
    EXPECT_EQ(rawSum(bw), 255u);
}

TEST(BiomeWeightsNormalizeTests, PrimesSum255) {
    // Weights 7, 11, 13 — sum 31, hard remainders.
    BiomeWeights bw;
    bw.entries[0] = { static_cast<uint8_t>(Biome::Ocean),     7 };
    bw.entries[1] = { static_cast<uint8_t>(Biome::Lake),     11 };
    bw.entries[2] = { static_cast<uint8_t>(Biome::HotDesert),13 };
    bw.count = 3;
    bw.normalize();
    EXPECT_EQ(rawSum(bw), 255u);
}

TEST(BiomeWeightsNormalizeTests, AlreadyNormalisedIsStable) {
    auto bw = BiomeWeights::single(Biome::Ocean);
    bw.normalize();
    EXPECT_EQ(rawSum(bw), 255u);
    EXPECT_EQ(bw.entries[0].weight, 255);
}

TEST(BiomeWeightsNormalizeTests, AllZeroDoesNothing) {
    BiomeWeights bw;
    bw.normalize();
    EXPECT_EQ(rawSum(bw), 0u);
}

TEST(BiomeWeightsNormalizeTests, LargestRemainderGoesToCorrectEntry) {
    // Weights 1:1:1, sum=3, 255/3=85 each, remainder=0 — no disambiguation needed.
    // Use weights 1:1:2, sum=4:
    //   entry0: 1*255/4 = 63 rem 3
    //   entry1: 1*255/4 = 63 rem 3
    //   entry2: 2*255/4 = 127 rem 2
    //   floor total = 253, deficit = 2
    //   entry0 and entry1 have larger remainder (3 > 2), both get +1.
    BiomeWeights bw;
    bw.entries[0] = { static_cast<uint8_t>(Biome::Ocean),    1 };
    bw.entries[1] = { static_cast<uint8_t>(Biome::Lake),     1 };
    bw.entries[2] = { static_cast<uint8_t>(Biome::HotDesert),2 };
    bw.count = 3;
    bw.normalize();
    EXPECT_EQ(rawSum(bw), 255u);
    EXPECT_EQ(bw.entries[0].weight, 64);  // 63 + 1
    EXPECT_EQ(bw.entries[1].weight, 64);  // 63 + 1
    EXPECT_EQ(bw.entries[2].weight, 127); // no bump
}

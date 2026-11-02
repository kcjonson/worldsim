// The ground tile bake's CPU filters (GroundTileFilter.h): the Kaiser kernel, the wrap-aware
// downsample and its clamp, the mean lock, sRGB encoding, the metrics, and the runtime's LOD and
// trilinear sample the asset-manager previews with.

#include "world/rendering/GroundTileFilter.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

using namespace worldview::ground;

namespace {

	LinearImage filled(int width, int height, glm::vec4 color) {
		LinearImage image(width, height);
		for (glm::vec4& t : image.texels) {
			t = color;
		}
		return image;
	}

	// Black and white squares of `cell` texels, in linear light.
	LinearImage checker(int size, int cell) {
		LinearImage image(size, size);
		for (int y = 0; y < size; ++y) {
			for (int x = 0; x < size; ++x) {
				const float v = (((x / cell) + (y / cell)) % 2 == 0) ? 0.0F : 1.0F;
				image.at(x, y) = {v, v, v, 1.0F};
			}
		}
		return image;
	}

	// Deterministic noise in [0, 1) per texel and channel.
	float noise(int x, int y, int c) {
		uint32_t h =
			(static_cast<uint32_t>(x) * 73856093U) ^ (static_cast<uint32_t>(y) * 19349663U) ^ (static_cast<uint32_t>(c) * 83492791U);
		h ^= h >> 13;
		h *= 0x5BD1E995U;
		h ^= h >> 15;
		return static_cast<float>(h & 0xFFFFU) / 65536.0F;
	}

} // namespace

TEST(GroundTileFilterTest, KaiserKernelIsNormalized) {
	EXPECT_FLOAT_EQ(kaiser(0.0F), 1.0F);
	EXPECT_EQ(kaiser(kKaiserHalfWidth), 0.0F);
	EXPECT_EQ(kaiser(-4.0F), 0.0F);
	EXPECT_FLOAT_EQ(kaiser(1.3F), kaiser(-1.3F));
	EXPECT_NEAR(kaiser(1.0F), 0.0F, 1e-6F) << "the sinc's first zero";
	EXPECT_LT(kaiser(1.5F), 0.0F) << "the negative lobe the clamp exists for";

	// The continuous kernel integrates to about 1, so the polyphase normalization only trims rounding.
	double		  integral = 0.0;
	constexpr int kSteps = 6000;
	for (int i = 0; i < kSteps; ++i) {
		const double x = -kKaiserHalfWidth + ((i + 0.5) * 2.0 * kKaiserHalfWidth / kSteps);
		integral += kaiser(static_cast<float>(x)) * (2.0 * kKaiserHalfWidth / kSteps);
	}
	EXPECT_NEAR(integral, 1.0, 0.02);

	// Every output texel's weights sum to 1, at the supersample factors, the mip steps, and an odd ratio.
	for (const auto& [src, dst] : std::vector<std::pair<int, int>>{{512, 64}, {128, 32}, {16, 8}, {24, 8}, {512, 8}, {7, 7}}) {
		const PolyphaseKernel kernel = makeKaiserKernel(src, dst);
		ASSERT_EQ(kernel.taps.size(), static_cast<size_t>(dst));
		for (const PolyphaseKernel::Tap& tap : kernel.taps) {
			double sum = 0.0;
			for (int j = 0; j < tap.count; ++j) {
				sum += kernel.weights[static_cast<size_t>(tap.offset + j)];
				const int source = kernel.sources[static_cast<size_t>(tap.offset + j)];
				EXPECT_GE(source, 0);
				EXPECT_LT(source, src) << "sources are wrapped into the image";
			}
			EXPECT_NEAR(sum, 1.0, 1e-5) << src << " -> " << dst;
			EXPECT_GE(static_cast<double>(tap.count), 2.0 * kKaiserHalfWidth * src / dst) << "the kernel reaches 3 output texels each way";
		}
	}
}

TEST(GroundTileFilterTest, DownsampleOfAConstantStaysConstant) {
	const glm::vec4	  color(0.2F, 0.5F, 0.1F, 1.0F);
	const LinearImage out = downsample(filled(64, 64, color), 8, 8);
	ASSERT_EQ(out.width, 8);
	ASSERT_EQ(out.height, 8);
	for (const glm::vec4& t : out.texels) {
		for (int c = 0; c < 4; ++c) {
			EXPECT_NEAR(t[c], color[c], 1e-5F);
		}
	}

	// The same from a render target's bytes, decoded to linear light on the way.
	std::vector<uint8_t> bytes(32 * 32 * 4);
	for (size_t i = 0; i < bytes.size(); i += 4) {
		bytes[i] = 100;
		bytes[i + 1] = 150;
		bytes[i + 2] = 50;
		bytes[i + 3] = 255;
	}
	const LinearImage fromBytes = downsampleSrgb8Premultiplied(bytes, 32, 32, 8, 8);
	for (const glm::vec4& t : fromBytes.texels) {
		EXPECT_NEAR(t.r, srgbToLinear(100.0F / 255.0F), 1e-5F);
		EXPECT_NEAR(t.g, srgbToLinear(150.0F / 255.0F), 1e-5F);
		EXPECT_NEAR(t.b, srgbToLinear(50.0F / 255.0F), 1e-5F);
		EXPECT_NEAR(t.a, 1.0F, 1e-6F);
	}
}

TEST(GroundTileFilterTest, CheckerDownsamplesToItsMeanInLinearLight) {
	// A checker one source texel a square, and one whose period is exactly one output texel: either way
	// each output texel is the mean, at the supersample factor and at a mip step.
	for (const auto& [size, cell, dst] : std::vector<std::tuple<int, int, int>>{{64, 1, 8}, {64, 4, 8}, {16, 1, 8}, {32, 8, 2}}) {
		const LinearImage out = downsample(checker(size, cell), dst, dst);
		for (const glm::vec4& t : out.texels) {
			EXPECT_NEAR(t.r, 0.5F, 1e-4F) << size << "/" << cell << " -> " << dst;
		}
	}

	// Black and white bytes average to half the light, which encodes near code 188, not 128.
	std::vector<uint8_t> bytes(32 * 32 * 4);
	for (int y = 0; y < 32; ++y) {
		for (int x = 0; x < 32; ++x) {
			const uint8_t v = ((x + y) % 2 == 0) ? 0 : 255;
			uint8_t*	  p = &bytes[((static_cast<size_t>(y) * 32) + static_cast<size_t>(x)) * 4];
			p[0] = p[1] = p[2] = v;
			p[3] = 255;
		}
	}
	const LinearImage fromBytes = downsampleSrgb8Premultiplied(bytes, 32, 32, 4, 4);
	EXPECT_NEAR(fromBytes.at(1, 2).g, 0.5F, 1e-4F);
	EXPECT_NEAR(linearToSrgb(fromBytes.at(1, 2).g) * 255.0F, 188.0F, 0.5F);
}

TEST(GroundTileFilterTest, DownsampleWrapsAcrossTheEdge) {
	// A bright stripe straddling the seam (source columns 63 and 0) must land equally in output columns
	// 0 and 7, which it only does when the kernel wraps.
	LinearImage image = filled(64, 64, {0.1F, 0.1F, 0.1F, 1.0F});
	for (int y = 0; y < 64; ++y) {
		image.at(63, y) = image.at(0, y) = {0.9F, 0.9F, 0.9F, 1.0F};
	}
	const LinearImage out = downsample(image, 8, 8);
	for (int y = 0; y < 8; ++y) {
		EXPECT_NEAR(out.at(0, y).r, out.at(7, y).r, 1e-5F);
		EXPECT_GT(out.at(0, y).r, 0.15F);
		EXPECT_NEAR(out.at(3, y).r, out.at(4, y).r, 1e-6F) << "columns the kernel never carries the stripe to stay one base";
		EXPECT_NEAR(out.at(3, y).r, 0.1F, 0.01F);
	}
	EXPECT_NEAR(mean(out).r, mean(image).r, 1e-6F) << "and the mean is the source's";
}

TEST(GroundTileFilterTest, DownsampleKeepsTheMeanOfSmallLightDetail) {
	// One light texel in each 8x8 block of a dark field: at 8:1 each is an eighth of an output texel wide,
	// where the clamp alone would brighten the result (it trims the undershoot around each and finds no
	// overshoot to trim). Dark texels stay non-negative and the mean comes back exactly.
	LinearImage image = filled(64, 64, {0.01F, 0.02F, 0.005F, 1.0F});
	for (int y = 3; y < 64; y += 8) {
		for (int x = 5; x < 64; x += 8) {
			image.at(x, y) = {0.8F, 0.9F, 0.7F, 1.0F};
		}
	}
	const LinearImage out = downsample(image, 4, 4);
	const glm::vec4	  in = mean(image);
	const glm::vec4	  got = mean(out);
	for (int c = 0; c < 4; ++c) {
		EXPECT_NEAR(got[c], in[c], in[c] * 1e-5F) << "channel " << c;
	}
	EXPECT_GE(minAlpha(out), 1.0F);
	for (const glm::vec4& t : out.texels) {
		EXPECT_GE(t.b, 0.0F);
	}
}

TEST(GroundTileFilterTest, ClampStopsTheNegativeLobeRinging) {
	// Black left half, white right half (so two edges, one at the seam). Without the clamp the texels
	// one and a half output texels from an edge undershoot below black, since the kernel's negative
	// lobe reaches the white; the clamp holds them to what their main lobe covers.
	LinearImage image(64, 64);
	for (int y = 0; y < 64; ++y) {
		for (int x = 0; x < 64; ++x) {
			const float v = x < 32 ? 0.0F : 1.0F;
			image.at(x, y) = {v, v, v, 1.0F};
		}
	}
	const LinearImage out = downsample(image, 8, 8);
	for (const glm::vec4& t : out.texels) {
		EXPECT_GE(t.r, 0.0F);
		EXPECT_LE(t.r, 1.0F);
	}
	EXPECT_EQ(out.at(1, 4).r, 0.0F) << "inside the black half, 1.5 texels from both edges";
	EXPECT_EQ(out.at(2, 4).r, 0.0F);
	EXPECT_EQ(out.at(5, 4).r, 1.0F);
	EXPECT_GT(out.at(3, 4).r, 0.0F) << "next to the edge the main lobe still blends it";
}

TEST(GroundTileFilterTest, MeanLockBringsABiasedMipToTheReference) {
	LinearImage reference(32, 32);
	LinearImage rendered(32, 32);
	for (int y = 0; y < 32; ++y) {
		for (int x = 0; x < 32; ++x) {
			const glm::vec4 base(0.08F + (0.1F * noise(x, y, 0)), 0.2F + (0.15F * noise(x, y, 1)), 0.03F + (0.02F * noise(x, y, 2)), 1.0F);
			reference.at(x, y) = base;
			// Detail dropped at this level made it 8% darker, with its own texel noise.
			rendered.at(x, y) = glm::vec4(glm::vec3(base) * (0.92F + (0.04F * noise(x, y, 3))), 1.0F);
		}
	}
	const glm::vec4 before = mean(rendered);
	const glm::vec4 target = mean(reference);
	const glm::vec4 shift = lockMean(rendered, reference, 2.0F);
	const glm::vec4 after = mean(rendered);
	for (int c = 0; c < 3; ++c) {
		EXPECT_NEAR(shift[c], target[c] - before[c], 1e-6F);
		EXPECT_GT(std::abs(before[c] - target[c]) / target[c], 0.04F) << "the input really is biased";
		EXPECT_LT(std::abs(after[c] - target[c]) / target[c], 0.0005F) << "locked to within 0.05%, channel " << c;
	}

	// And it survives encoding, with the dither, to within the bake's 0.5% gate.
	const glm::vec4 encoded = mean(decodeSrgb8(encodeSrgb8(rendered, 7U)));
	for (int c = 0; c < 3; ++c) {
		EXPECT_LT(std::abs(encoded[c] - target[c]) / target[c], 0.005F) << "channel " << c;
	}
}

TEST(GroundTileFilterTest, DitheredEncodingKeepsAFlatMean) {
	// Linear 0.2 sits between two codes; plain rounding would land 0.8% high, the dither averages out.
	const LinearImage flat = filled(64, 64, {0.2F, 0.2F, 0.2F, 1.0F});
	const Srgb8Image  encoded = encodeSrgb8(flat, 3U);
	EXPECT_EQ(encoded.rgba[3], 255);
	EXPECT_NEAR(mean(decodeSrgb8(encoded)).r, 0.2F, 0.2F * 0.0025F);
}

TEST(GroundTileFilterTest, DitheredEncodingKeepsASmallLevelsMean) {
	// A 4x4 mip of a dark ground: 16 texels' dither errors alone leave a channel's mean up to about 1% off
	// (a code near sRGB 50 is 4% of its light), so the encode steps codes until it's back within 0.05%.
	for (uint32_t seed = 0; seed < 32; ++seed) {
		LinearImage level(4, 4);
		for (int y = 0; y < 4; ++y) {
			for (int x = 0; x < 4; ++x) {
				const int s = static_cast<int>(seed) * 3;
				level.at(x, y) = {0.07F + (0.01F * noise(x, y, s)), 0.05F + (0.008F * noise(x, y, s + 1)), 0.03F + (0.006F * noise(x, y, s + 2)), 1.0F};
			}
		}
		const glm::vec4 target = mean(level);
		const glm::vec4 encoded = mean(decodeSrgb8(encodeSrgb8(level, seed)));
		for (int c = 0; c < 3; ++c) {
			EXPECT_LT(std::abs(encoded[c] - target[c]) / target[c], 0.005F) << "seed " << seed << ", channel " << c;
		}
	}
}

TEST(GroundTileFilterTest, MetricsOnSyntheticImages) {
	EXPECT_NEAR(linearToLab(glm::vec3(1.0F)).x, 100.0F, 0.01F);
	EXPECT_NEAR(linearToLab(glm::vec3(1.0F)).y, 0.0F, 0.01F);
	EXPECT_NEAR(linearToLab(glm::vec3(1.0F)).z, 0.0F, 0.01F);
	EXPECT_NEAR(deltaE76(glm::vec3(0.0F), glm::vec3(1.0F)), 100.0F, 0.01F);
	EXPECT_EQ(deltaE76(glm::vec3(0.1F, 0.3F, 0.05F), glm::vec3(0.1F, 0.3F, 0.05F)), 0.0F);
	// A 1% luminance step on a mid green is well under the 1.0 gate.
	EXPECT_LT(deltaE76(glm::vec3(0.1F, 0.2F, 0.05F), glm::vec3(0.101F, 0.202F, 0.0505F)), 1.0F);

	EXPECT_NEAR(windowedRmsContrast(filled(16, 16, {0.3F, 0.3F, 0.3F, 1.0F})), 0.0F, 1e-6F);
	// Luminance 0.2 and 0.6 alternating: deviation 0.2 over mean 0.4.
	LinearImage alternating(16, 16);
	for (int y = 0; y < 16; ++y) {
		for (int x = 0; x < 16; ++x) {
			const float v = ((x + y) % 2 == 0) ? 0.2F : 0.6F;
			alternating.at(x, y) = {v, v, v, 1.0F};
		}
	}
	EXPECT_NEAR(windowedRmsContrast(alternating), 0.5F, 1e-5F);
	EXPECT_NEAR(windowedRmsContrast(alternating, 64), 0.5F, 1e-5F) << "a window larger than the image is the image";

	// Contrast that only shows between windows (each 8x8 window flat) reads as none: the metric is local.
	LinearImage blocks(16, 16);
	for (int y = 0; y < 16; ++y) {
		for (int x = 0; x < 16; ++x) {
			const float v = ((x / 8) + (y / 8)) % 2 == 0 ? 0.2F : 0.6F;
			blocks.at(x, y) = {v, v, v, 1.0F};
		}
	}
	EXPECT_NEAR(windowedRmsContrast(blocks), 0.0F, 1e-6F);
	EXPECT_FLOAT_EQ(minAlpha(blocks), 1.0F);
}

TEST(GroundTileFilterTest, LodAndTrilinearSampleMatchTheRuntime) {
	EXPECT_NEAR(groundLod(128.0F, 24.0F, 7), std::log2(128.0F / 24.0F) + 0.25F, 1e-6F);
	EXPECT_EQ(groundLod(128.0F, 160.0F, 7), 0.0F) << "magnified: mip 0";
	EXPECT_EQ(groundLod(128.0F, 1.0F, 7), 6.0F) << "clamped to the last mip";

	// A two-level chain: mip 0 alternating 0 and 1 by column, mip 1 flat 0.5.
	std::vector<LinearImage> chain;
	chain.push_back(LinearImage(4, 4));
	for (int y = 0; y < 4; ++y) {
		for (int x = 0; x < 4; ++x) {
			const float v = x % 2 == 0 ? 0.0F : 1.0F;
			chain[0].at(x, y) = {v, v, v, 1.0F};
		}
	}
	chain.push_back(filled(2, 2, {0.5F, 0.5F, 0.5F, 1.0F}));

	EXPECT_NEAR(sampleTrilinear(chain, 1.5F, 1.5F, 0.0F).r, 1.0F, 1e-6F) << "a texel center returns the texel";
	EXPECT_NEAR(sampleTrilinear(chain, 2.0F, 1.5F, 0.0F).r, 0.5F, 1e-6F) << "between texels, bilinear";
	EXPECT_NEAR(sampleTrilinear(chain, 0.0F, 1.5F, 0.0F).r, 0.5F, 1e-6F) << "the left edge blends with the right column: repeat";
	EXPECT_NEAR(sampleTrilinear(chain, 1.5F, 1.5F, 0.5F).r, 0.75F, 1e-6F) << "half way to mip 1";
	EXPECT_NEAR(sampleTrilinear(chain, 1.5F, 1.5F, 1.0F).r, 0.5F, 1e-6F);
}

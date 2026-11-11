#include "world/rendering/Bc3.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

	std::vector<uint8_t> image(int width, int height, auto&& texel) {
		std::vector<uint8_t> rgba(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				const auto	 c = texel(x, y);
				const size_t i = ((static_cast<size_t>(y) * static_cast<size_t>(width)) + static_cast<size_t>(x)) * 4;
				for (size_t k = 0; k < 4; ++k) {
					rgba[i + k] = c[k];
				}
			}
		}
		return rgba;
	}

	// Colors RGB565 holds exactly, and alpha at the block's two ends, come back exactly; the blocks are a
	// quarter of the bytes. The second block is half red and half green, whose bounding box's diagonal is
	// orthogonal to every texel's deviation from the mean.
	TEST(Bc3, EndpointsRoundTripExactly) {
		const std::vector<uint8_t> rgba = image(8, 4, [](int x, int y) {
			const bool on = ((x + y) % 2) == 0;
			const auto top = static_cast<uint8_t>(on ? 255 : 0);
			const auto alpha = static_cast<uint8_t>(on ? 200 : 13);
			if (x < 4) {
				return std::array<uint8_t, 4>{top, top, top, alpha};
			}
			return std::array<uint8_t, 4>{top, static_cast<uint8_t>(255 - top), 0, alpha};
		});
		const std::vector<uint8_t> blocks = worldview::encodeBc3(rgba, 8, 4);
		ASSERT_EQ(blocks.size(), rgba.size() / 4);
		EXPECT_EQ(worldview::decodeBc3(blocks, 8, 4), rgba);
	}

	// One block written out by hand from the format, so a layout mistake the encoder and decoder shared
	// couldn't pass: texels 0 to 3 take each index.
	TEST(Bc3, DecodesAHandWrittenBlock) {
		// Alpha 70 and 0 (eight-alpha), then indices 0, 1, 2 and 7 in 3-bit fields from the low bits up.
		const std::array<uint8_t, 8> alpha = {70, 0, 0b10'001'000, 0b0000'1110, 0, 0, 0, 0};
		// Red 0xF800 and blue 0x001F, little-endian, then indices 0, 1, 2 and 3 in 2-bit fields.
		const std::array<uint8_t, 8> color = {0x00, 0xF8, 0x1F, 0x00, 0b11'10'01'00, 0, 0, 0};
		std::vector<uint8_t>		 block(alpha.begin(), alpha.end());
		block.insert(block.end(), color.begin(), color.end());
		const std::vector<uint8_t> rgba = worldview::decodeBc3(block, 4, 4);
		ASSERT_EQ(rgba.size(), 64U);
		const std::array<std::array<uint8_t, 4>, 4> expected = {{
			{255, 0, 0, 70},  // c0
			{0, 0, 255, 0},	  // c1
			{170, 0, 85, 60}, // two thirds c0; alpha (6 * 70 + 0) / 7
			{85, 0, 170, 10}, // two thirds c1; alpha (70 + 6 * 0) / 7
		}};
		for (size_t t = 0; t < 4; ++t) {
			for (size_t k = 0; k < 4; ++k) {
				EXPECT_EQ(rgba[(t * 4) + k], expected[t][k]) << "texel " << t << " channel " << k;
			}
		}
	}

	// A flat block of a color RGB565 can't hold, as a small mip of a dithered ground mostly is: within a code a
	// channel, where rounding both endpoints to the nearest code would move blue 30 to 33.
	TEST(Bc3, AFlatBlockKeepsItsColor) {
		const std::vector<uint8_t> rgba = image(4, 4, [](int, int) { return std::array<uint8_t, 4>{30, 100, 200, 128}; });
		const std::vector<uint8_t> decoded = worldview::decodeBc3(worldview::encodeBc3(rgba, 4, 4), 4, 4);
		ASSERT_EQ(decoded.size(), rgba.size());
		for (size_t i = 0; i < rgba.size(); ++i) {
			EXPECT_LE(std::abs(static_cast<int>(rgba[i]) - static_cast<int>(decoded[i])), 1) << "byte " << i;
		}
	}

	// A smooth ramp, as a mip of the ground is: each texel within a palette step of its source, and the
	// image's mean, which the bake locks to 0.1%, within a code and a half a channel.
	TEST(Bc3, ARampStaysCloseAndKeepsItsMean) {
		const std::vector<uint8_t> rgba = image(16, 16, [](int x, int y) {
			return std::array<uint8_t, 4>{
				static_cast<uint8_t>(40 + (x * 6)),
				static_cast<uint8_t>(90 + (y * 5)),
				static_cast<uint8_t>(30 + ((x + y) * 2)),
				static_cast<uint8_t>(x * 16)
			};
		});
		const std::vector<uint8_t> decoded = worldview::decodeBc3(worldview::encodeBc3(rgba, 16, 16), 16, 16);
		ASSERT_EQ(decoded.size(), rgba.size());
		std::array<double, 4> sourceSum{};
		std::array<double, 4> decodedSum{};
		for (size_t i = 0; i < rgba.size(); ++i) {
			const int limit = (i % 4) == 3 ? 255 / 14 + 1 : 12; // alpha half a step of 7; color a 565 step plus a third
			EXPECT_LE(std::abs(static_cast<int>(rgba[i]) - static_cast<int>(decoded[i])), limit) << "byte " << i;
			sourceSum[i % 4] += rgba[i];
			decodedSum[i % 4] += decoded[i];
		}
		for (size_t k = 0; k < 4; ++k) {
			EXPECT_LE(std::abs(sourceSum[k] - decodedSum[k]) / 256.0, 1.5) << "channel " << k;
		}
	}

	// A mip that isn't a multiple of 4 (a 100-texel layer's 25, a chain's 2 and 1) pads its last blocks,
	// and its own texels come back.
	TEST(Bc3, AnySizePadsItsLastBlocks) {
		for (const int side : {1, 2, 3, 6, 25}) {
			const std::vector<uint8_t> rgba = image(side, side, [](int x, int y) {
				const auto on = static_cast<uint8_t>(((x + y) % 2) * 255); // two colors a block, which BC1 holds exactly
				return std::array<uint8_t, 4>{on, on, on, static_cast<uint8_t>(255 - on)};
			});
			const std::vector<uint8_t> blocks = worldview::encodeBc3(rgba, side, side);
			EXPECT_EQ(blocks.size(), worldview::bc3Bytes(side, side)) << side;
			const std::vector<uint8_t> decoded = worldview::decodeBc3(blocks, side, side);
			ASSERT_EQ(decoded.size(), rgba.size()) << side;
			for (size_t i = 0; i < rgba.size(); ++i) {
				EXPECT_LE(std::abs(static_cast<int>(rgba[i]) - static_cast<int>(decoded[i])), 1) << "side " << side << " byte " << i;
			}
		}
		EXPECT_TRUE(worldview::encodeBc3(std::vector<uint8_t>(15), 2, 2).empty());
		EXPECT_TRUE(worldview::decodeBc3(std::vector<uint8_t>(15), 4, 4).empty());
	}

} // namespace

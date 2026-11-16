#pragma once

// BC3 (DXT5) for the ground arrays: 4x4 texel blocks of 16 bytes, an 8-alpha BC4 block (two 8-bit
// endpoints, a 3-bit index a texel) then a four-color BC1 block (two RGB565 endpoints, a 2-bit index a
// texel). A quarter of RGBA8's bytes. The color is fit in the space the bytes are in (sRGB for the
// ground), along the block's principal axis and then refined by least squares; alpha spans the block's
// own range. Blocks run left to right, top to bottom; a side that isn't a multiple of 4 pads its last
// blocks with its edge texels, which nothing samples (a ground layer is any whole number of texels, and
// its mips run down to 1x1).

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace worldview {

	inline constexpr size_t kBc3BlockBytes = 16;

	// Bytes of a width x height image's blocks.
	[[nodiscard]] constexpr size_t bc3Bytes(int width, int height) {
		return static_cast<size_t>((width + 3) / 4) * static_cast<size_t>((height + 3) / 4) * kBc3BlockBytes;
	}

	// An RGBA8 image (width x height x 4 bytes, rows top to bottom) as BC3 blocks; empty for an empty
	// image or a span of the wrong size.
	[[nodiscard]] std::vector<uint8_t> encodeBc3(std::span<const uint8_t> rgba, int width, int height);

	// What the GPU reads back from the blocks, as RGBA8; empty when the span is the wrong size.
	[[nodiscard]] std::vector<uint8_t> decodeBc3(std::span<const uint8_t> blocks, int width, int height);

} // namespace worldview

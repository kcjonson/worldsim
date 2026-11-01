#pragma once

// The CPU half of the ground tile bake (GroundTileBaker.h): linear-light images, the wrap-aware
// Kaiser downsample and its anti-ringing clamp, the mean lock, sRGB encoding, and the acceptance
// metrics. No GL, so every piece is unit-tested on its own (GroundTileFilter.test.cpp).
//
// A ground tile is one period of a pattern that repeats forever, so every filter here wraps at the
// edges. Images hold premultiplied linear RGBA; ground tiles are opaque, where that is plain color.

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace foundation {
	class TaskPool;
}

namespace worldview::ground {

	/// RGBA in linear light, premultiplied by alpha, rows top to bottom.
	struct LinearImage {
		int					   width = 0;
		int					   height = 0;
		std::vector<glm::vec4> texels;

		LinearImage() = default;
		LinearImage(int newWidth, int newHeight)
			: width(newWidth),
			  height(newHeight),
			  texels(static_cast<size_t>(newWidth) * static_cast<size_t>(newHeight), glm::vec4(0.0F)) {}

		[[nodiscard]] glm::vec4& at(int x, int y) {
			return texels[(static_cast<size_t>(y) * static_cast<size_t>(width)) + static_cast<size_t>(x)];
		}
		[[nodiscard]] const glm::vec4& at(int x, int y) const {
			return texels[(static_cast<size_t>(y) * static_cast<size_t>(width)) + static_cast<size_t>(x)];
		}
	};

	/// Straight-alpha sRGB, 8 bits a channel, rows top to bottom: what the runtime uploads as GL_SRGB8_ALPHA8.
	struct Srgb8Image {
		int					 width = 0;
		int					 height = 0;
		std::vector<uint8_t> rgba;
	};

	[[nodiscard]] float srgbToLinear(float encoded);
	[[nodiscard]] float linearToSrgb(float linear);

	/// The downsample kernel: a Kaiser-windowed sinc (NVTT's, alpha 4, stretch 1) reaching 3 output
	/// texels each side. Its passband is flat to a quarter cycle a texel and it lets about 3% of the energy
	/// above Nyquist alias, against 20% for a box.
	inline constexpr float kKaiserAlpha = 4.0F;
	inline constexpr float kKaiserHalfWidth = 3.0F;

	/// The kernel at `x` output texels from the center; 1 at 0, 0 from the half-width out.
	[[nodiscard]] float kaiser(float x);

	/// Weights for resampling `srcLength` texels down to `dstLength` along one axis, wrapping. Output
	/// texel i takes taps[i].count weights from weights[taps[i].offset], each applied to the source texel
	/// at the same index of `sources` (already wrapped; taps[i].first is the unwrapped first one, which
	/// may be negative). Each texel's weights sum to 1: they are the kernel integrated over each source
	/// texel's footprint, then normalized. The lobe fields list, in `lobeSources`, the source texels
	/// under the kernel's main lobe (one output texel each way), the neighborhood the anti-ringing clamp
	/// reads.
	struct PolyphaseKernel {
		struct Tap {
			int first = 0;
			int count = 0;
			int offset = 0;
			int lobeFirst = 0;
			int lobeCount = 0;
			int lobeOffset = 0;
		};
		int				   srcLength = 0;
		int				   dstLength = 0;
		std::vector<Tap>   taps;
		std::vector<float> weights;
		std::vector<int>   sources;
		std::vector<int>   lobeSources;
	};

	/// `dstLength` must be from 1 to `srcLength`.
	[[nodiscard]] PolyphaseKernel makeKaiserKernel(int srcLength, int dstLength);

	/// Downsample to dstWidth x dstHeight with the Kaiser kernel on both axes, wrapping, then clamp each
	/// output texel per channel to the min and max of the source texels under its main lobe, so the
	/// kernel's negative lobes can't ring past values that were there. The clamp alone shifts the mean
	/// (it removes undershoot beside small light detail with nothing to balance it), so the source's mean
	/// is restored afterwards as a uniform offset. Rows run on `pool` when given.
	[[nodiscard]] LinearImage downsample(const LinearImage& src, int dstWidth, int dstHeight, foundation::TaskPool* pool = nullptr);

	/// The same from a render target's bytes: 8-bit sRGB premultiplied by alpha (what salal draws), rows
	/// top to bottom, srcWidth * srcHeight * 4 of them. Each texel is decoded to linear light first.
	[[nodiscard]] LinearImage downsampleSrgb8Premultiplied(
		std::span<const uint8_t> rgba,
		int						 srcWidth,
		int						 srcHeight,
		int						 dstWidth,
		int						 dstHeight,
		foundation::TaskPool*	 pool = nullptr
	);

	/// Separable Gaussian blur with `sigma` texels, wrapping; the weights sum to 1 however small the image,
	/// so the mean is kept.
	[[nodiscard]] LinearImage blurWrapped(const LinearImage& src, float sigma);

	/// The mean lock: rendered += blur(reference - rendered, sigma), wrapping, then clamped to a valid
	/// premultiplied color. The blur keeps the residual's mean, so the result's mean is the reference's
	/// (up to the clamp), while the rendered detail keeps its contrast. Both images must be one size.
	/// Returns the mean shift it applied: mean(reference) - mean(rendered) before the lock.
	glm::vec4 lockMean(LinearImage& rendered, const LinearImage& reference, float sigma);

	[[nodiscard]] glm::vec4 mean(const LinearImage& image);

	/// The smallest alpha in the image; ground tiles should be opaque (1).
	[[nodiscard]] float minAlpha(const LinearImage& image);

	/// Encode to straight-alpha sRGB 8-bit. Color is dithered by up to half a code before rounding, from
	/// a hash of (x, y, channel, ditherSeed): rounding is then unbiased, so a flat far mip keeps its mean
	/// instead of snapping to the nearest code. Alpha rounds plainly.
	[[nodiscard]] Srgb8Image encodeSrgb8(const LinearImage& image, uint32_t ditherSeed);

	/// Back to premultiplied linear light.
	[[nodiscard]] LinearImage decodeSrgb8(const Srgb8Image& image);

	/// CIE L*a*b* (D65) of a linear sRGB color.
	[[nodiscard]] glm::vec3 linearToLab(glm::vec3 rgb);

	/// CIE76 color difference between two linear sRGB colors; about 2.3 is just noticeable.
	[[nodiscard]] float deltaE76(glm::vec3 linearA, glm::vec3 linearB);

	/// The image's RMS luminance contrast (standard deviation over mean of linear luminance) in
	/// `window` x `window` texel windows tiling it, averaged over the windows. A window never exceeds the
	/// image, so a 4 x 4 mip is one window.
	[[nodiscard]] float windowedRmsContrast(const LinearImage& image, int window = 8);

	/// The runtime's LOD bias: a quarter level toward the blurrier mip (research rule 9).
	inline constexpr float kGroundLodBias = 0.25F;

	/// The mip level the runtime samples a ground texture at, once per draw: the top-down view is
	/// orthographic, so texel density is the same at every pixel. log2(texelsPerMeter / screen pixels
	/// per meter) + bias, clamped to the chain's levels.
	[[nodiscard]] float groundLod(float texelsPerMeter, float screenPixelsPerMeter, int levels, float bias = kGroundLodBias);

	/// What the GPU's trilinear filter with GL_REPEAT returns from `chain` (mip 0 first, each half the last):
	/// bilinear in levels floor(lod) and the next, blended by the fraction, in linear light as an sRGB
	/// texture filters. (u, v) is in mip 0 texels; the asset-manager's zoom preview draws with it.
	[[nodiscard]] glm::vec4 sampleTrilinear(std::span<const LinearImage> chain, float u, float v, float lod);

} // namespace worldview::ground

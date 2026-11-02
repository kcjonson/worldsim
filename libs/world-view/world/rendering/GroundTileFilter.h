#pragma once

// The CPU half of the ground tile bake (GroundTileBaker.h): linear-light images, the wrap-aware
// Kaiser downsample and its anti-ringing clamp, the mean lock, sRGB encoding, and the acceptance
// metrics. No GL, so every piece is unit-tested on its own (GroundTileFilter.test.cpp).
//
// A ground tile is one period of a pattern that repeats forever, so every filter here wraps at the
// edges. Ground tiles are opaque: images hold linear RGB, never premultiplied, and alpha is plain data,
// the baked height (docs/technical/ground-textures.md, "Heights in alpha"), filtered like any channel.

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace foundation {
	class TaskPool;
}

namespace worldview::ground {

	/// RGB in linear light and A as plain data, rows top to bottom.
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

	/// sRGB color and linear alpha, 8 bits a channel, rows top to bottom: what the runtime uploads as
	/// GL_SRGB8_ALPHA8.
	struct Srgb8Image {
		int					 width = 0;
		int					 height = 0;
		std::vector<uint8_t> rgba;
	};

	/// One channel of floats, rows top to bottom: a variant's heights.
	struct Plane {
		int				   width = 0;
		int				   height = 0;
		std::vector<float> values;

		Plane() = default;
		Plane(int newWidth, int newHeight)
			: width(newWidth),
			  height(newHeight),
			  values(static_cast<size_t>(newWidth) * static_cast<size_t>(newHeight), 0.0F) {}

		[[nodiscard]] float& at(int x, int y) {
			return values[(static_cast<size_t>(y) * static_cast<size_t>(width)) + static_cast<size_t>(x)];
		}
		[[nodiscard]] float at(int x, int y) const {
			return values[(static_cast<size_t>(y) * static_cast<size_t>(width)) + static_cast<size_t>(x)];
		}
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

	/// The same from a render target's bytes: 8-bit sRGB (what salal draws), rows top to bottom,
	/// srcWidth * srcHeight * 4 of them. Color decodes to linear light as opaque, whatever the alpha,
	/// and alpha is kept as data.
	[[nodiscard]] LinearImage downsampleSrgb8(
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

	/// The mean lock: rendered += blur(reference - rendered, sigma), wrapping, then clamped to 0..1 per
	/// channel. The blur keeps the residual's mean, so the result's mean is the reference's (up to the
	/// clamp), while the rendered detail keeps its contrast. Both images must be one size. A channel that
	/// already matches the reference (the heights, copied in first) is left as it is. Returns the mean
	/// shift it applied: mean(reference) - mean(rendered) before the lock.
	glm::vec4 lockMean(LinearImage& rendered, const LinearImage& reference, float sigma);

	[[nodiscard]] glm::vec4 mean(const LinearImage& image);

	/// Encode to 8 bits a channel: sRGB color, linear alpha. Color is dithered by up to half a code before
	/// rounding, from a hash of (x, y, channel, ditherSeed): rounding is then unbiased, so a flat far mip
	/// keeps its mean instead of snapping to the nearest code. A small level's dither errors don't average
	/// out, so a channel whose linear mean lands more than 0.05% off then has its codes stepped one at a
	/// time, the texels that rounded furthest the wrong way first, until no step brings it closer. Alpha
	/// rounds plainly, undithered.
	[[nodiscard]] Srgb8Image encodeSrgb8(const LinearImage& image, uint32_t ditherSeed);

	/// Back to linear light, alpha as data.
	[[nodiscard]] LinearImage decodeSrgb8(const Srgb8Image& image);

	/// The plane down by a whole factor, each output texel the mean of a factor x factor block.
	[[nodiscard]] Plane boxDownsample(const Plane& src, int factor);

	/// Separable Gaussian blur with `sigma` texels, wrapping, weights summing to 1.
	[[nodiscard]] Plane blurWrapped(const Plane& src, float sigma);

	/// Tileable blue noise in -1..1 with a uniform histogram: white noise from a hash of (x, y, seed) with
	/// a wrapped blur of itself taken away, so its energy sits at the highest frequencies, then ranked.
	[[nodiscard]] Plane blueNoise(int width, int height, uint32_t seed);

	/// How far the tie-break moves a height that ties with others (docs/technical/ground-textures.md,
	/// "Heights in alpha").
	inline constexpr float kHeightTieBreak = 0.03F;

	/// Replaces every value of every plane by its rank among all of them, 0 to 1, so the heights of a
	/// surface's variants share one uniform histogram. Values that round to the same 1 / 65536 tie: a group
	/// takes the middle of its ranks, plus blue noise (one tile from seed, each plane reading it at its own
	/// wrapped offset) times the lesser of tieBreak and half the group's span, so a flat area dithers about
	/// its middle instead of flipping at one distance, and no texel passes a higher value's rank.
	void rankNormalize(std::span<Plane> planes, uint32_t seed, float tieBreak = kHeightTieBreak);

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

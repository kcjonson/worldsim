#include "world/rendering/GroundTileFilter.h"

#include <threading/TaskPool.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <numbers>

namespace worldview::ground {

	namespace {

		constexpr int kChannels = 4;

		int wrapIndex(int i, int n) {
			const int r = i % n;
			return r < 0 ? r + n : r;
		}

		// Modified Bessel function of the first kind, order 0, by its power series.
		double bessel0(double x) {
			const double half = x * 0.5;
			double		 sum = 1.0;
			double		 term = 1.0;
			for (int k = 1; k < 64; ++k) {
				term *= (half / k) * (half / k);
				sum += term;
				if (term < sum * 1e-12) {
					break;
				}
			}
			return sum;
		}

		const std::array<float, 256>& srgbDecodeTable() {
			static const std::array<float, 256> table = [] {
				std::array<float, 256> t{};
				for (int i = 0; i < 256; ++i) {
					t[static_cast<size_t>(i)] = srgbToLinear(static_cast<float>(i) / 255.0F);
				}
				return t;
			}();
			return table;
		}

		// Rows [0, count) as slabs on the pool, or all at once without one. Every row is computed from its
		// inputs alone, so the result is the same either way.
		void forRows(foundation::TaskPool* pool, int count, const std::function<void(int, int)>& fn) {
			constexpr int kMinParallelRows = 32;
			if (pool == nullptr || count < kMinParallelRows) {
				fn(0, count);
				return;
			}
			const size_t slabs = static_cast<size_t>(pool->threadCount() + 1U) * 4U;
			const size_t grain = std::max<size_t>(8, static_cast<size_t>(count) / slabs);
			pool->parallelFor(0, static_cast<size_t>(count), grain, [&fn](size_t begin, size_t end) {
				fn(static_cast<int>(begin), static_cast<int>(end));
			});
		}

		// One output texel along an axis: the weighted sum over its taps and the min and max over its
		// main lobe, read through `at(sourceIndex)` which gives a pointer to 4 floats.
		template <typename At>
		void filterTexel(const PolyphaseKernel& kernel, int dst, const At& at, float* value, float* low, float* high) {
			const PolyphaseKernel::Tap& tap = kernel.taps[static_cast<size_t>(dst)];
			float						acc[kChannels] = {0.0F, 0.0F, 0.0F, 0.0F};
			for (int j = 0; j < tap.count; ++j) {
				const float	 w = kernel.weights[static_cast<size_t>(tap.offset + j)];
				const float* s = at(kernel.sources[static_cast<size_t>(tap.offset + j)]);
				for (int c = 0; c < kChannels; ++c) {
					acc[c] += w * s[c];
				}
			}
			for (int c = 0; c < kChannels; ++c) {
				value[c] = acc[c];
			}
			if (low == nullptr) {
				return;
			}
			float mn[kChannels] = {INFINITY, INFINITY, INFINITY, INFINITY};
			float mx[kChannels] = {-INFINITY, -INFINITY, -INFINITY, -INFINITY};
			for (int j = 0; j < tap.lobeCount; ++j) {
				const float* s = at(kernel.lobeSources[static_cast<size_t>(tap.lobeOffset + j)]);
				for (int c = 0; c < kChannels; ++c) {
					mn[c] = std::min(mn[c], s[c]);
					mx[c] = std::max(mx[c], s[c]);
				}
			}
			for (int c = 0; c < kChannels; ++c) {
				low[c] = mn[c];
				high[c] = mx[c];
			}
		}

		// Moves the image's mean to 	arget, per channel and exactly, without leaving the valid range: a
		// step down scales the channel, so black stays black; a step up fills each texel toward its alpha
		// in proportion to the room it has, so a full texel stays full. A uniform offset would push dark
		// texels below zero and lose the clamped part.
		void restoreMean(LinearImage& image, const glm::vec4& target) {
			if (image.texels.empty()) {
				return;
			}
			const auto shift = [&image](int channel, float current, float wanted, float ceiling) {
				if (wanted < current) {
					const float gain = current > 0.0F ? std::max(0.0F, wanted) / current : 0.0F;
					for (glm::vec4& t : image.texels) {
						t[channel] *= gain;
					}
				} else if (wanted > current) {
					const float room = ceiling - current;
					const float fill = room > 1e-9F ? std::min(1.0F, (wanted - current) / room) : 0.0F;
					for (glm::vec4& t : image.texels) {
						t[channel] += fill * ((channel == 3 ? 1.0F : t.a) - t[channel]);
					}
				}
			};
			const glm::vec4 before = mean(image);
			shift(3, before.a, target.a, 1.0F);
			const glm::vec4 current = mean(image);
			for (int c = 0; c < 3; ++c) {
				shift(c, current[c], target[c], current.a);
			}
		}

		// The separable downsample. `decodeRow(y, out)` writes source row y as srcWidth * 4 premultiplied
		// linear floats. The horizontal pass keeps, per output column and source row, the filtered value
		// and the lobe's min and max; the vertical pass filters those and clamps to the min of the mins and
		// the max of the maxes, which is the min and max over the 2D lobe window.
		template <typename DecodeRow>
		LinearImage
		downsampleRows(int srcWidth, int srcHeight, int dstWidth, int dstHeight, const DecodeRow& decodeRow, foundation::TaskPool* pool) {
			const PolyphaseKernel kx = makeKaiserKernel(srcWidth, dstWidth);
			const PolyphaseKernel ky = makeKaiserKernel(srcHeight, dstHeight);

			const size_t		columnStride = static_cast<size_t>(dstWidth) * kChannels;
			std::vector<float>	value(static_cast<size_t>(srcHeight) * columnStride);
			std::vector<float>	low(value.size());
			std::vector<float>	high(value.size());
			std::vector<double> rowSums(static_cast<size_t>(srcHeight) * kChannels);

			forRows(pool, srcHeight, [&](int begin, int end) {
				std::vector<float> row(static_cast<size_t>(srcWidth) * kChannels);
				const auto		   at = [&row](int i) {
					return row.data() + (static_cast<size_t>(i) * kChannels);
				};
				for (int y = begin; y < end; ++y) {
					decodeRow(y, row.data());
					for (int x = 0; x < srcWidth; ++x) {
						for (int c = 0; c < kChannels; ++c) {
							rowSums[(static_cast<size_t>(y) * kChannels) + static_cast<size_t>(c)] +=
								row[(static_cast<size_t>(x) * kChannels) + static_cast<size_t>(c)];
						}
					}
					const size_t base = static_cast<size_t>(y) * columnStride;
					for (int x = 0; x < dstWidth; ++x) {
						const size_t o = base + (static_cast<size_t>(x) * kChannels);
						filterTexel(kx, x, at, &value[o], &low[o], &high[o]);
					}
				}
			});

			LinearImage out(dstWidth, dstHeight);
			forRows(pool, dstHeight, [&](int begin, int end) {
				for (int y = begin; y < end; ++y) {
					for (int x = 0; x < dstWidth; ++x) {
						const size_t column = static_cast<size_t>(x) * kChannels;
						const auto	 valueAt = [&](int i) {
							  return value.data() + (static_cast<size_t>(i) * columnStride) + column;
						};
						const auto lowAt = [&](int i) {
							return low.data() + (static_cast<size_t>(i) * columnStride) + column;
						};
						const auto highAt = [&](int i) {
							return high.data() + (static_cast<size_t>(i) * columnStride) + column;
						};

						float filtered[kChannels];
						filterTexel(ky, y, valueAt, filtered, nullptr, nullptr);
						float						lowest[kChannels] = {INFINITY, INFINITY, INFINITY, INFINITY};
						float						highest[kChannels] = {-INFINITY, -INFINITY, -INFINITY, -INFINITY};
						const PolyphaseKernel::Tap& tap = ky.taps[static_cast<size_t>(y)];
						for (int j = 0; j < tap.lobeCount; ++j) {
							const int	 source = ky.lobeSources[static_cast<size_t>(tap.lobeOffset + j)];
							const float* l = lowAt(source);
							const float* h = highAt(source);
							for (int c = 0; c < kChannels; ++c) {
								lowest[c] = std::min(lowest[c], l[c]);
								highest[c] = std::max(highest[c], h[c]);
							}
						}
						glm::vec4& texel = out.at(x, y);
						for (int c = 0; c < kChannels; ++c) {
							texel[c] = std::clamp(filtered[c], lowest[c], highest[c]);
						}
					}
				}
			});

			// The clamp takes away the undershoot beside a feature narrower than the main lobe with no
			// overshoot on the feature to balance it, so alone it brightens a coarse mip of small light
			// detail by whole percent (25% on a test tile at 8:1). The source's mean goes back, which is
			// also what makes Ref_k carry mip 0's mean for the mean lock.
			double sourceSum[kChannels] = {0.0, 0.0, 0.0, 0.0};
			for (int y = 0; y < srcHeight; ++y) {
				for (int c = 0; c < kChannels; ++c) {
					sourceSum[c] += rowSums[(static_cast<size_t>(y) * kChannels) + static_cast<size_t>(c)];
				}
			}
			const double count = static_cast<double>(srcWidth) * srcHeight;
			restoreMean(
				out,
				{static_cast<float>(sourceSum[0] / count),
				 static_cast<float>(sourceSum[1] / count),
				 static_cast<float>(sourceSum[2] / count),
				 static_cast<float>(sourceSum[3] / count)}
			);
			return out;
		}

		// A bit mixer (murmur3's finalizer) over the texel's coordinates, for the dither.
		uint32_t ditherHash(uint32_t x, uint32_t y, uint32_t channel, uint32_t seed) {
			uint32_t h = (x * 0x8DA6B343U) ^ (y * 0xD8163841U) ^ (channel * 0xCB1AB31FU) ^ (seed * 0x9E3779B9U);
			h ^= h >> 16;
			h *= 0x85EBCA6BU;
			h ^= h >> 13;
			h *= 0xC2B2AE35U;
			h ^= h >> 16;
			return h;
		}

		float luminance(const glm::vec4& texel) {
			return (0.2126F * texel.r) + (0.7152F * texel.g) + (0.0722F * texel.b);
		}

	} // namespace

	float srgbToLinear(float encoded) {
		const float c = std::clamp(encoded, 0.0F, 1.0F);
		return c <= 0.04045F ? c / 12.92F : std::pow((c + 0.055F) / 1.055F, 2.4F);
	}

	float linearToSrgb(float linear) {
		const float c = std::clamp(linear, 0.0F, 1.0F);
		return c <= 0.0031308F ? c * 12.92F : (1.055F * std::pow(c, 1.0F / 2.4F)) - 0.055F;
	}

	float kaiser(float x) {
		const double ax = std::abs(static_cast<double>(x));
		if (ax >= kKaiserHalfWidth) {
			return 0.0F;
		}
		const double t = ax / kKaiserHalfWidth;
		const double window = bessel0(kKaiserAlpha * std::sqrt(1.0 - (t * t))) / bessel0(kKaiserAlpha);
		const double sinc = ax < 1e-9 ? 1.0 : std::sin(std::numbers::pi * ax) / (std::numbers::pi * ax);
		return static_cast<float>(sinc * window);
	}

	PolyphaseKernel makeKaiserKernel(int srcLength, int dstLength) {
		PolyphaseKernel kernel;
		kernel.srcLength = srcLength;
		kernel.dstLength = dstLength;
		if (srcLength <= 0 || dstLength <= 0 || dstLength > srcLength) {
			return kernel;
		}

		// In source texels: the scale, the kernel's reach, and the main lobe's.
		const double scale = static_cast<double>(srcLength) / static_cast<double>(dstLength);
		const double reach = kKaiserHalfWidth * scale;
		// Points per source texel when integrating the kernel over its footprint: a fine source (8x
		// supersampled) needs few, a 2:1 step needs more.
		const int subSamples = std::clamp(static_cast<int>(std::ceil(16.0 / scale)), 1, 16);

		kernel.taps.resize(static_cast<size_t>(dstLength));
		for (int i = 0; i < dstLength; ++i) {
			const double center = (static_cast<double>(i) + 0.5) * scale;
			const int	 first = static_cast<int>(std::floor(center - reach));
			const int	 last = static_cast<int>(std::ceil(center + reach)) - 1;

			PolyphaseKernel::Tap& tap = kernel.taps[static_cast<size_t>(i)];
			tap.first = first;
			tap.offset = static_cast<int>(kernel.weights.size());
			double sum = 0.0;
			for (int j = first; j <= last; ++j) {
				double w = 0.0;
				for (int s = 0; s < subSamples; ++s) {
					const double x = static_cast<double>(j) + ((static_cast<double>(s) + 0.5) / subSamples);
					w += kaiser(static_cast<float>((x - center) / scale));
				}
				w /= subSamples;
				kernel.weights.push_back(static_cast<float>(w));
				kernel.sources.push_back(wrapIndex(j, srcLength));
				sum += w;
			}
			tap.count = static_cast<int>(kernel.weights.size()) - tap.offset;
			for (int j = 0; j < tap.count; ++j) {
				kernel.weights[static_cast<size_t>(tap.offset + j)] =
					static_cast<float>(kernel.weights[static_cast<size_t>(tap.offset + j)] / sum);
			}

			// The main lobe: source texels whose footprint overlaps (center - scale, center + scale).
			tap.lobeFirst = static_cast<int>(std::floor(center - scale));
			const int lobeLast = static_cast<int>(std::ceil(center + scale)) - 1;
			tap.lobeOffset = static_cast<int>(kernel.lobeSources.size());
			for (int j = tap.lobeFirst; j <= lobeLast; ++j) {
				kernel.lobeSources.push_back(wrapIndex(j, srcLength));
			}
			tap.lobeCount = static_cast<int>(kernel.lobeSources.size()) - tap.lobeOffset;
		}
		return kernel;
	}

	LinearImage downsample(const LinearImage& src, int dstWidth, int dstHeight, foundation::TaskPool* pool) {
		const auto decodeRow = [&src](int y, float* out) {
			const glm::vec4* row = &src.texels[static_cast<size_t>(y) * static_cast<size_t>(src.width)];
			for (int x = 0; x < src.width; ++x) {
				for (int c = 0; c < kChannels; ++c) {
					out[(static_cast<size_t>(x) * kChannels) + static_cast<size_t>(c)] = row[x][c];
				}
			}
		};
		return downsampleRows(src.width, src.height, dstWidth, dstHeight, decodeRow, pool);
	}

	LinearImage downsampleSrgb8Premultiplied(
		std::span<const uint8_t> rgba,
		int						 srcWidth,
		int						 srcHeight,
		int						 dstWidth,
		int						 dstHeight,
		foundation::TaskPool*	 pool
	) {
		const std::array<float, 256>& table = srgbDecodeTable();
		const auto					  decodeRow = [&](int y, float* out) {
			   const uint8_t* row = rgba.data() + (static_cast<size_t>(y) * static_cast<size_t>(srcWidth) * kChannels);
			   for (int x = 0; x < srcWidth; ++x) {
				   const uint8_t* p = row + (static_cast<size_t>(x) * kChannels);
				   float*		  o = out + (static_cast<size_t>(x) * kChannels);
				   const uint8_t  a = p[3];
				   if (a == 255) {
					   o[0] = table[p[0]];
					   o[1] = table[p[1]];
					   o[2] = table[p[2]];
					   o[3] = 1.0F;
				   } else if (a == 0) {
					   o[0] = o[1] = o[2] = o[3] = 0.0F;
				   } else {
					   // The target blended in its 8-bit encoding, premultiplied: undo the alpha there, decode,
					   // and premultiply again in linear light.
					   const float alpha = static_cast<float>(a) / 255.0F;
					   for (int c = 0; c < 3; ++c) {
						   o[c] = srgbToLinear(std::min(1.0F, static_cast<float>(p[c]) / static_cast<float>(a))) * alpha;
					   }
					   o[3] = alpha;
				   }
			   }
		};
		return downsampleRows(srcWidth, srcHeight, dstWidth, dstHeight, decodeRow, pool);
	}

	LinearImage blurWrapped(const LinearImage& src, float sigma) {
		if (src.width <= 0 || src.height <= 0 || !(sigma > 0.0F)) {
			return src;
		}
		const int		   radius = std::max(1, static_cast<int>(std::ceil(3.0F * sigma)));
		std::vector<float> weights(static_cast<size_t>((2 * radius) + 1));
		float			   sum = 0.0F;
		for (int k = -radius; k <= radius; ++k) {
			const float w = std::exp(-static_cast<float>(k * k) / (2.0F * sigma * sigma));
			weights[static_cast<size_t>(k + radius)] = w;
			sum += w;
		}
		for (float& w : weights) {
			w /= sum;
		}

		LinearImage across(src.width, src.height);
		for (int y = 0; y < src.height; ++y) {
			for (int x = 0; x < src.width; ++x) {
				glm::vec4 acc(0.0F);
				for (int k = -radius; k <= radius; ++k) {
					acc += weights[static_cast<size_t>(k + radius)] * src.at(wrapIndex(x + k, src.width), y);
				}
				across.at(x, y) = acc;
			}
		}
		LinearImage out(src.width, src.height);
		for (int y = 0; y < src.height; ++y) {
			for (int x = 0; x < src.width; ++x) {
				glm::vec4 acc(0.0F);
				for (int k = -radius; k <= radius; ++k) {
					acc += weights[static_cast<size_t>(k + radius)] * across.at(x, wrapIndex(y + k, src.height));
				}
				out.at(x, y) = acc;
			}
		}
		return out;
	}

	glm::vec4 lockMean(LinearImage& rendered, const LinearImage& reference, float sigma) {
		if (rendered.width != reference.width || rendered.height != reference.height || rendered.texels.empty()) {
			return glm::vec4(0.0F);
		}
		const glm::vec4 shift = mean(reference) - mean(rendered);
		LinearImage		residual(rendered.width, rendered.height);
		for (size_t i = 0; i < residual.texels.size(); ++i) {
			residual.texels[i] = reference.texels[i] - rendered.texels[i];
		}
		const LinearImage smooth = blurWrapped(residual, sigma);
		for (size_t i = 0; i < rendered.texels.size(); ++i) {
			glm::vec4& t = rendered.texels[i];
			t += smooth.texels[i];
			t.a = std::clamp(t.a, 0.0F, 1.0F);
			for (int c = 0; c < 3; ++c) {
				t[c] = std::clamp(t[c], 0.0F, t.a);
			}
		}
		// Where the clamp bit, what it cut goes back across the tile.
		restoreMean(rendered, mean(reference));
		return shift;
	}

	glm::vec4 mean(const LinearImage& image) {
		if (image.texels.empty()) {
			return glm::vec4(0.0F);
		}
		// Summed in double: a 4096 x 4096 float sum loses the last digits the 0.5% gate reads.
		double sum[kChannels] = {0.0, 0.0, 0.0, 0.0};
		for (const glm::vec4& t : image.texels) {
			for (int c = 0; c < kChannels; ++c) {
				sum[c] += t[c];
			}
		}
		const double n = static_cast<double>(image.texels.size());
		return {
			static_cast<float>(sum[0] / n), static_cast<float>(sum[1] / n), static_cast<float>(sum[2] / n), static_cast<float>(sum[3] / n)
		};
	}

	float minAlpha(const LinearImage& image) {
		float lowest = 1.0F;
		for (const glm::vec4& t : image.texels) {
			lowest = std::min(lowest, t.a);
		}
		return lowest;
	}

	Srgb8Image encodeSrgb8(const LinearImage& image, uint32_t ditherSeed) {
		Srgb8Image out{.width = image.width, .height = image.height, .rgba = {}};
		const size_t count = image.texels.size();
		out.rgba.resize(count * kChannels);
		std::vector<float> wanted(count * 3); // each channel's code before the dither and rounding
		for (int y = 0; y < image.height; ++y) {
			for (int x = 0; x < image.width; ++x) {
				const size_t	 i = (static_cast<size_t>(y) * static_cast<size_t>(image.width)) + static_cast<size_t>(x);
				const glm::vec4& t = image.texels[i];
				const float		 alpha = std::clamp(t.a, 0.0F, 1.0F);
				uint8_t*		 o = &out.rgba[i * kChannels];
				for (int c = 0; c < 3; ++c) {
					const float straight = alpha > 0.0F ? t[c] / alpha : 0.0F;
					const float dither =
						(static_cast<float>(
							 ditherHash(static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(c), ditherSeed) >> 8U
						 ) /
						 16777216.0F) -
						0.5F;
					wanted[(i * 3) + static_cast<size_t>(c)] = linearToSrgb(straight) * 255.0F;
					o[c] = static_cast<uint8_t>(std::clamp(std::lround(wanted[(i * 3) + static_cast<size_t>(c)] + dither), 0L, 255L));
				}
				o[3] = static_cast<uint8_t>(std::lround(alpha * 255.0F));
			}
		}

		// The dither leaves each texel unbiased, but a small level's errors don't average out: on a 4x4
		// mip one code of a dark channel is several percent of its light. Where a channel's mean has
		// drifted past kTolerance, step the texels that rounded furthest the wrong way one code each,
		// while each step brings the mean closer.
		constexpr double			  kTolerance = 0.0005;
		const std::array<float, 256>& table = srgbDecodeTable();
		std::vector<size_t>			  order;
		for (int c = 0; c < 3; ++c) {
			double target = 0.0;
			double have = 0.0;
			for (size_t i = 0; i < count; ++i) {
				const double alpha = static_cast<double>(out.rgba[(i * kChannels) + 3]) / 255.0;
				target += image.texels[i][c];
				have += table[out.rgba[(i * kChannels) + static_cast<size_t>(c)]] * alpha;
			}
			double error = target - have;
			if (std::abs(error) <= kTolerance * std::max(target, 1e-9)) {
				continue;
			}
			const int direction = error > 0.0 ? 1 : -1;
			const auto residual = [&](size_t i) {
				return static_cast<float>(direction) *
					   (wanted[(i * 3) + static_cast<size_t>(c)] - static_cast<float>(out.rgba[(i * kChannels) + static_cast<size_t>(c)]));
			};
			order.clear();
			for (size_t i = 0; i < count; ++i) {
				if (residual(i) > 0.0F) {
					order.push_back(i);
				}
			}
			std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return residual(a) > residual(b); });
			for (const size_t i : order) {
				uint8_t&   code = out.rgba[(i * kChannels) + static_cast<size_t>(c)];
				const int  next = static_cast<int>(code) + direction;
				const auto alpha = static_cast<double>(out.rgba[(i * kChannels) + 3]) / 255.0;
				if (next < 0 || next > 255) {
					continue;
				}
				const double delta = (table[static_cast<size_t>(next)] - table[code]) * alpha;
				if (std::abs(error - delta) < std::abs(error)) {
					code = static_cast<uint8_t>(next);
					error -= delta;
				}
			}
		}
		return out;
	}

	LinearImage decodeSrgb8(const Srgb8Image& image) {
		const std::array<float, 256>& table = srgbDecodeTable();
		LinearImage					  out(image.width, image.height);
		for (size_t i = 0; i < out.texels.size(); ++i) {
			const uint8_t* p = &image.rgba[i * kChannels];
			const float	   alpha = static_cast<float>(p[3]) / 255.0F;
			out.texels[i] = {table[p[0]] * alpha, table[p[1]] * alpha, table[p[2]] * alpha, alpha};
		}
		return out;
	}

	glm::vec3 linearToLab(glm::vec3 rgb) {
		// sRGB primaries to XYZ, normalized to the D65 white
		const float x = ((0.4124564F * rgb.r) + (0.3575761F * rgb.g) + (0.1804375F * rgb.b)) / 0.95047F;
		const float y = (0.2126729F * rgb.r) + (0.7151522F * rgb.g) + (0.0721750F * rgb.b);
		const float z = ((0.0193339F * rgb.r) + (0.1191920F * rgb.g) + (0.9503041F * rgb.b)) / 1.08883F;
		const auto	f = [](float t) {
			 constexpr float kDelta = 6.0F / 29.0F;
			 return t > kDelta * kDelta * kDelta ? std::cbrt(t) : (t / (3.0F * kDelta * kDelta)) + (4.0F / 29.0F);
		};
		const float fx = f(x);
		const float fy = f(y);
		const float fz = f(z);
		return {(116.0F * fy) - 16.0F, 500.0F * (fx - fy), 200.0F * (fy - fz)};
	}

	float deltaE76(glm::vec3 linearA, glm::vec3 linearB) {
		const glm::vec3 d = linearToLab(linearA) - linearToLab(linearB);
		return std::sqrt((d.x * d.x) + (d.y * d.y) + (d.z * d.z));
	}

	float windowedRmsContrast(const LinearImage& image, int window) {
		if (image.width <= 0 || image.height <= 0) {
			return 0.0F;
		}
		const int w = std::max(1, std::min({window, image.width, image.height}));
		const int columns = (image.width + w - 1) / w;
		const int rows = (image.height + w - 1) / w;
		double	  total = 0.0;
		for (int wy = 0; wy < rows; ++wy) {
			for (int wx = 0; wx < columns; ++wx) {
				double sum = 0.0;
				double sumSquares = 0.0;
				for (int y = 0; y < w; ++y) {
					for (int x = 0; x < w; ++x) {
						const double l = luminance(image.at(wrapIndex((wx * w) + x, image.width), wrapIndex((wy * w) + y, image.height)));
						sum += l;
						sumSquares += l * l;
					}
				}
				const double n = static_cast<double>(w) * w;
				const double average = sum / n;
				const double variance = std::max(0.0, (sumSquares / n) - (average * average));
				total += average > 1e-9 ? std::sqrt(variance) / average : 0.0;
			}
		}
		return static_cast<float>(total / (static_cast<double>(columns) * rows));
	}

	float groundLod(float texelsPerMeter, float screenPixelsPerMeter, int levels, float bias) {
		if (!(texelsPerMeter > 0.0F) || !(screenPixelsPerMeter > 0.0F) || levels <= 1) {
			return 0.0F;
		}
		return std::clamp(std::log2(texelsPerMeter / screenPixelsPerMeter) + bias, 0.0F, static_cast<float>(levels - 1));
	}

	glm::vec4 sampleTrilinear(std::span<const LinearImage> chain, float u, float v, float lod) {
		if (chain.empty()) {
			return glm::vec4(0.0F);
		}
		const auto bilinear = [&](int level) {
			const LinearImage& image = chain[static_cast<size_t>(level)];
			const float		   scale = std::ldexp(1.0F, -level);
			// Texel centers sit at i + 0.5, as GL samples them.
			const float		x = (u * scale) - 0.5F;
			const float		y = (v * scale) - 0.5F;
			const float		fx = std::floor(x);
			const float		fy = std::floor(y);
			const int		x0 = wrapIndex(static_cast<int>(fx), image.width);
			const int		y0 = wrapIndex(static_cast<int>(fy), image.height);
			const int		x1 = wrapIndex(x0 + 1, image.width);
			const int		y1 = wrapIndex(y0 + 1, image.height);
			const float		tx = x - fx;
			const float		ty = y - fy;
			const glm::vec4 top = (image.at(x0, y0) * (1.0F - tx)) + (image.at(x1, y0) * tx);
			const glm::vec4 bottom = (image.at(x0, y1) * (1.0F - tx)) + (image.at(x1, y1) * tx);
			return (top * (1.0F - ty)) + (bottom * ty);
		};
		const int	last = static_cast<int>(chain.size()) - 1;
		const float clamped = std::clamp(lod, 0.0F, static_cast<float>(last));
		const int	lower = std::min(static_cast<int>(std::floor(clamped)), last);
		const int	upper = std::min(lower + 1, last);
		const float t = clamped - static_cast<float>(lower);
		if (upper == lower || t <= 0.0F) {
			return bilinear(lower);
		}
		return (bilinear(lower) * (1.0F - t)) + (bilinear(upper) * t);
	}

} // namespace worldview::ground

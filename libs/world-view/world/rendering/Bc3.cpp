#include "world/rendering/Bc3.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace worldview {

	namespace {

		struct Rgb {
			float r = 0.0F;
			float g = 0.0F;
			float b = 0.0F;
		};

		Rgb operator+(Rgb a, Rgb b) {
			return {a.r + b.r, a.g + b.g, a.b + b.b};
		}
		Rgb operator-(Rgb a, Rgb b) {
			return {a.r - b.r, a.g - b.g, a.b - b.b};
		}
		Rgb operator*(Rgb a, float s) {
			return {a.r * s, a.g * s, a.b * s};
		}
		float dot(Rgb a, Rgb b) {
			return (a.r * b.r) + (a.g * b.g) + (a.b * b.b);
		}

		using Block = std::array<std::array<uint8_t, 4>, 16>;

		uint16_t to565(Rgb c) {
			const auto channel = [](float v, int bits) {
				const float top = static_cast<float>((1 << bits) - 1);
				return static_cast<uint16_t>(std::clamp(std::lround(v * top / 255.0F), 0L, static_cast<long>(top)));
			};
			return static_cast<uint16_t>((channel(c.r, 5) << 11) | (channel(c.g, 6) << 5) | channel(c.b, 5));
		}

		// As the GPU expands it: each channel's bits repeated into eight.
		std::array<int, 3> from565(uint16_t c) {
			const int r = (c >> 11) & 31;
			const int g = (c >> 5) & 63;
			const int b = c & 31;
			return {(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)};
		}

		// The four-color palette: the two endpoints, then a third and two thirds of the way.
		std::array<std::array<int, 3>, 4> palette(uint16_t c0, uint16_t c1) {
			const std::array<int, 3>		  a = from565(c0);
			const std::array<int, 3>		  b = from565(c1);
			std::array<std::array<int, 3>, 4> p{};
			for (size_t k = 0; k < 3; ++k) {
				p[0][k] = a[k];
				p[1][k] = b[k];
				p[2][k] = ((2 * a[k]) + b[k] + 1) / 3;
				p[3][k] = (a[k] + (2 * b[k]) + 1) / 3;
			}
			return p;
		}

		// Each texel's nearest palette entry, packed two bits a texel, and the squared error.
		uint32_t assignColors(const Block& texels, uint16_t c0, uint16_t c1, int64_t& error) {
			const std::array<std::array<int, 3>, 4> p = palette(c0, c1);
			uint32_t								indices = 0;
			error = 0;
			for (size_t i = 0; i < 16; ++i) {
				int64_t best = std::numeric_limits<int64_t>::max();
				int		pick = 0;
				for (int e = 0; e < 4; ++e) {
					int64_t d = 0;
					for (size_t k = 0; k < 3; ++k) {
						const int64_t delta = static_cast<int64_t>(texels[i][k]) - p[static_cast<size_t>(e)][k];
						d += delta * delta;
					}
					if (d < best) {
						best = d;
						pick = e;
					}
				}
				error += best;
				indices |= static_cast<uint32_t>(pick) << (2 * i);
			}
			return indices;
		}

		// Endpoints c0 > c1, so every decoder reads the four-color palette; swapping the endpoints swaps
		// indices 0 and 1, and 2 and 3.
		void orderEndpoints(uint16_t& c0, uint16_t& c1, uint32_t& indices) {
			if (c0 >= c1) {
				return;
			}
			std::swap(c0, c1);
			indices ^= 0x55555555U;
		}

		void encodeColor(const Block& texels, uint8_t* out) {
			Rgb mean;
			for (const auto& t : texels) {
				mean = mean + Rgb{static_cast<float>(t[0]), static_cast<float>(t[1]), static_cast<float>(t[2])};
			}
			mean = mean * (1.0F / 16.0F);

			// The principal axis of the block's colors, by power iteration on their covariance.
			std::array<float, 6> cov{}; // rr rg rb gg gb bb
			Rgb					 lo{255.0F, 255.0F, 255.0F};
			Rgb					 hi{0.0F, 0.0F, 0.0F};
			for (const auto& t : texels) {
				const Rgb c{static_cast<float>(t[0]), static_cast<float>(t[1]), static_cast<float>(t[2])};
				const Rgb d = c - mean;
				cov[0] += d.r * d.r;
				cov[1] += d.r * d.g;
				cov[2] += d.r * d.b;
				cov[3] += d.g * d.g;
				cov[4] += d.g * d.b;
				cov[5] += d.b * d.b;
				lo = {std::min(lo.r, c.r), std::min(lo.g, c.g), std::min(lo.b, c.b)};
				hi = {std::max(hi.r, c.r), std::max(hi.g, c.g), std::max(hi.b, c.b)};
			}
			Rgb axis = hi - lo;
			for (int iteration = 0; iteration < 8; ++iteration) {
				const Rgb next{
					(cov[0] * axis.r) + (cov[1] * axis.g) + (cov[2] * axis.b),
					(cov[1] * axis.r) + (cov[3] * axis.g) + (cov[4] * axis.b),
					(cov[2] * axis.r) + (cov[4] * axis.g) + (cov[5] * axis.b),
				};
				const float length = std::sqrt(dot(next, next));
				if (length < 1e-6F) {
					break;
				}
				axis = next * (1.0F / length);
			}

			uint16_t	c0 = to565(mean);
			uint16_t	c1 = c0;
			const float axisLength = dot(axis, axis);
			if (axisLength > 1e-6F) {
				float tMin = std::numeric_limits<float>::max();
				float tMax = std::numeric_limits<float>::lowest();
				for (const auto& t : texels) {
					const float s =
						dot(Rgb{static_cast<float>(t[0]), static_cast<float>(t[1]), static_cast<float>(t[2])} - mean, axis) / axisLength;
					tMin = std::min(tMin, s);
					tMax = std::max(tMax, s);
				}
				c0 = to565(mean + (axis * tMax));
				c1 = to565(mean + (axis * tMin));
			}

			int64_t	 error = 0;
			uint32_t indices = assignColors(texels, c0, c1, error);

			// Least squares on the endpoints given the indices: a texel at weight w of the way from c1 to
			// c0 is w * c0 + (1 - w) * c1. Two passes; a pass that doesn't lower the error is undone.
			constexpr std::array<float, 4> kWeight{1.0F, 0.0F, 2.0F / 3.0F, 1.0F / 3.0F};
			for (int pass = 0; pass < 2 && error > 0; ++pass) {
				float aa = 0.0F;
				float ab = 0.0F;
				float bb = 0.0F;
				Rgb	  ax;
				Rgb	  bx;
				for (size_t i = 0; i < 16; ++i) {
					const float w = kWeight[(indices >> (2 * i)) & 3U];
					const float v = 1.0F - w;
					const Rgb	x{static_cast<float>(texels[i][0]), static_cast<float>(texels[i][1]), static_cast<float>(texels[i][2])};
					aa += w * w;
					ab += w * v;
					bb += v * v;
					ax = ax + (x * w);
					bx = bx + (x * v);
				}
				const float det = (aa * bb) - (ab * ab);
				if (std::abs(det) < 1e-6F) {
					break;
				}
				const float	   inv = 1.0F / det;
				const Rgb	   a = ((ax * bb) - (bx * ab)) * inv;
				const Rgb	   b = ((bx * aa) - (ax * ab)) * inv;
				const uint16_t n0 = to565(a);
				const uint16_t n1 = to565(b);
				int64_t		   nextError = 0;
				const uint32_t nextIndices = assignColors(texels, n0, n1, nextError);
				if (nextError >= error) {
					break;
				}
				c0 = n0;
				c1 = n1;
				indices = nextIndices;
				error = nextError;
			}
			orderEndpoints(c0, c1, indices);
			if (c0 == c1) {
				indices = 0;
			}
			std::memcpy(out, &c0, 2);
			std::memcpy(out + 2, &c1, 2);
			std::memcpy(out + 4, &indices, 4);
		}

		// The eight-alpha palette: the block's own max and min, and six steps between.
		void encodeAlpha(const Block& texels, uint8_t* out) {
			uint8_t lo = 255;
			uint8_t hi = 0;
			for (const auto& t : texels) {
				lo = std::min(lo, t[3]);
				hi = std::max(hi, t[3]);
			}
			out[0] = hi;
			out[1] = lo;
			uint64_t bits = 0;
			if (hi > lo) {
				std::array<int, 8> values{hi, lo};
				for (int k = 1; k < 7; ++k) {
					values[static_cast<size_t>(k + 1)] = (((7 - k) * hi) + (k * lo) + 3) / 7;
				}
				for (size_t i = 0; i < 16; ++i) {
					int pick = 0;
					int best = 256;
					for (int e = 0; e < 8; ++e) {
						const int d = std::abs(static_cast<int>(texels[i][3]) - values[static_cast<size_t>(e)]);
						if (d < best) {
							best = d;
							pick = e;
						}
					}
					bits |= static_cast<uint64_t>(pick) << (3 * i);
				}
			}
			for (size_t k = 0; k < 6; ++k) {
				out[2 + k] = static_cast<uint8_t>(bits >> (8 * k));
			}
		}

		std::array<int, 8> alphaPalette(uint8_t a0, uint8_t a1) {
			std::array<int, 8> values{a0, a1};
			if (a0 > a1) {
				for (int k = 1; k < 7; ++k) {
					values[static_cast<size_t>(k + 1)] = (((7 - k) * a0) + (k * a1) + 3) / 7;
				}
			} else {
				for (int k = 1; k < 5; ++k) {
					values[static_cast<size_t>(k + 1)] = (((5 - k) * a0) + (k * a1) + 2) / 5;
				}
				values[6] = 0;
				values[7] = 255;
			}
			return values;
		}

	} // namespace

	std::vector<uint8_t> encodeBc3(std::span<const uint8_t> rgba, int width, int height) {
		if (width <= 0 || height <= 0 || rgba.size() != static_cast<size_t>(width) * static_cast<size_t>(height) * 4) {
			return {};
		}
		std::vector<uint8_t> blocks(bc3Bytes(width, height));
		uint8_t*			 out = blocks.data();
		Block				 texels{};
		for (int by = 0; by < height; by += 4) {
			for (int bx = 0; bx < width; bx += 4) {
				for (int y = 0; y < 4; ++y) {
					for (int x = 0; x < 4; ++x) {
						const size_t sy = static_cast<size_t>(std::min(by + y, height - 1));
						const size_t sx = static_cast<size_t>(std::min(bx + x, width - 1));
						const size_t src = ((sy * static_cast<size_t>(width)) + sx) * 4;
						std::memcpy(texels[static_cast<size_t>((y * 4) + x)].data(), &rgba[src], 4);
					}
				}
				encodeAlpha(texels, out);
				encodeColor(texels, out + 8);
				out += kBc3BlockBytes;
			}
		}
		return blocks;
	}

	std::vector<uint8_t> decodeBc3(std::span<const uint8_t> blocks, int width, int height) {
		if (width <= 0 || height <= 0 || blocks.size() != bc3Bytes(width, height)) {
			return {};
		}
		std::vector<uint8_t> rgba(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
		const uint8_t*		 in = blocks.data();
		for (int by = 0; by < height; by += 4) {
			for (int bx = 0; bx < width; bx += 4) {
				const std::array<int, 8> alphas = alphaPalette(in[0], in[1]);
				uint64_t				 alphaBits = 0;
				for (size_t k = 0; k < 6; ++k) {
					alphaBits |= static_cast<uint64_t>(in[2 + k]) << (8 * k);
				}
				uint16_t c0 = 0;
				uint16_t c1 = 0;
				uint32_t indices = 0;
				std::memcpy(&c0, in + 8, 2);
				std::memcpy(&c1, in + 10, 2);
				std::memcpy(&indices, in + 12, 4);
				const std::array<std::array<int, 3>, 4> colors = palette(c0, c1);
				for (int i = 0; i < 16; ++i) {
					if (bx + (i % 4) >= width || by + (i / 4) >= height) {
						continue;
					}
					const size_t dst =
						((static_cast<size_t>(by + (i / 4)) * static_cast<size_t>(width)) + static_cast<size_t>(bx + (i % 4))) * 4;
					const std::array<int, 3>& c = colors[(indices >> (2 * i)) & 3U];
					rgba[dst] = static_cast<uint8_t>(c[0]);
					rgba[dst + 1] = static_cast<uint8_t>(c[1]);
					rgba[dst + 2] = static_cast<uint8_t>(c[2]);
					rgba[dst + 3] = static_cast<uint8_t>(alphas[(alphaBits >> (3 * i)) & 7U]);
				}
				in += kBc3BlockBytes;
			}
		}
		return rgba;
	}

} // namespace worldview

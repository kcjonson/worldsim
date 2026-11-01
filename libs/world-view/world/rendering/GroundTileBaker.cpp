#include "world/rendering/GroundTileBaker.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <resources/RenderTarget.h>
#include <utils/Log.h>
#include <vector/Types.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <numeric>

namespace worldview {

	namespace {

		using Clock = std::chrono::steady_clock;

		double msSince(Clock::time_point start) {
			return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
		}

		// A draw holds at most 65,535 vertices (16-bit indices); triangles go in unshared, 3 vertices each.
		constexpr size_t kTrianglesPerDraw = std::numeric_limits<uint16_t>::max() / 3;

		const std::vector<uint16_t>& sequentialIndices() {
			static const std::vector<uint16_t> indices = [] {
				std::vector<uint16_t> v(kTrianglesPerDraw * 3);
				std::iota(v.begin(), v.end(), uint16_t{0});
				return v;
			}();
			return indices;
		}

		// Puts the screen's coordinate system and viewport back when a render is done. RenderTarget::end()
		// already rebinds the target below it; the projection the batch draws with is this.
		class ScreenStateGuard {
		  public:
			explicit ScreenStateGuard(Renderer::CoordinateSystem* restore)
				: coordinateSystem(restore) {
				Renderer::Primitives::getViewport(width, height);
			}
			~ScreenStateGuard() {
				Renderer::Primitives::setCoordinateSystem(coordinateSystem);
				Renderer::Primitives::setViewport(width, height);
			}
			ScreenStateGuard(const ScreenStateGuard&) = delete;
			ScreenStateGuard& operator=(const ScreenStateGuard&) = delete;
			ScreenStateGuard(ScreenStateGuard&&) = delete;
			ScreenStateGuard& operator=(ScreenStateGuard&&) = delete;

		  private:
			Renderer::CoordinateSystem* coordinateSystem;
			int							width = 0;
			int							height = 0;
		};

		// The level's triangles in render pixels, each followed by its copies one period away (the 3x3
		// wrap) that land in the target, so a shape crossing one edge comes back in at the opposite one.
		// A triangle's copies follow it directly to keep the generator's paint order: drawing all the
		// copies after the whole tile would paint the base over every shape that wraps.
		struct WrappedTriangles {
			std::vector<Foundation::Vec2>  positions; // 3 a triangle
			std::vector<Foundation::Color> colors;
		};

		WrappedTriangles wrapTriangles(std::span<const renderer::TessellatedMesh> meshes, float scale, float renderSize) {
			WrappedTriangles		out;
			const Foundation::Color fallback(0.7F, 0.7F, 0.7F, 1.0F);
			for (const renderer::TessellatedMesh& mesh : meshes) {
				const bool hasColors = mesh.hasColors();
				for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
					Foundation::Vec2  p[3];
					Foundation::Color c[3];
					float			  minX = std::numeric_limits<float>::max();
					float			  minY = minX;
					float			  maxX = -minX;
					float			  maxY = -minX;
					for (size_t k = 0; k < 3; ++k) {
						const uint16_t index = mesh.indices[t + k];
						p[k] = mesh.vertices[index] * scale;
						c[k] = hasColors ? mesh.colors[index] : fallback;
						minX = std::min(minX, p[k].x);
						maxX = std::max(maxX, p[k].x);
						minY = std::min(minY, p[k].y);
						maxY = std::max(maxY, p[k].y);
					}
					for (int oy = -1; oy <= 1; ++oy) {
						for (int ox = -1; ox <= 1; ++ox) {
							const Foundation::Vec2 offset(static_cast<float>(ox) * renderSize, static_cast<float>(oy) * renderSize);
							if (minX + offset.x >= renderSize || maxX + offset.x <= 0.0F || minY + offset.y >= renderSize ||
								maxY + offset.y <= 0.0F) {
								continue;
							}
							for (size_t k = 0; k < 3; ++k) {
								out.positions.push_back(p[k] + offset);
								out.colors.push_back(c[k]);
							}
						}
					}
				}
			}
			return out;
		}

		uint32_t mix32(uint32_t h) {
			h ^= h >> 16;
			h *= 0x85EBCA6BU;
			h ^= h >> 13;
			h *= 0xC2B2AE35U;
			h ^= h >> 16;
			return h;
		}

		void appendf(std::string& out, const char* format, auto... values) {
			char line[256];
			std::snprintf(line, sizeof(line), format, values...);
			out += line;
		}

	} // namespace

	GroundTileBaker::GroundTileBaker(const Args& newArgs)
		: args(newArgs) {}

	uint32_t GroundTileBaker::variantSeed(const std::string& defName, uint32_t variant) {
		uint32_t h = 2166136261U; // FNV-1a over the name
		for (const char c : defName) {
			h ^= static_cast<uint8_t>(c);
			h *= 16777619U;
		}
		return mix32(h ^ ((variant + 1U) * 0x9E3779B9U));
	}

	int GroundTileBaker::layerSize(const assets::AssetDefinition& def) {
		return static_cast<int>(std::lround(def.ground.periodMeters * static_cast<float>(def.ground.pixelsPerMeter)));
	}

	int GroundTileBaker::mipCount(int layer) {
		int count = 1;
		for (int size = layer; size > 1 && count < kMaxMipLevels; size >>= 1) {
			++count;
		}
		return count;
	}

	int GroundTileBaker::supersampleFor(int level) {
		return level <= 1 ? 8 : 4;
	}

	std::vector<uint8_t>
	GroundTileBaker::renderWrapped(std::span<const renderer::TessellatedMesh> meshes, float periodMeters, int renderSize) const {
		const float			   size = static_cast<float>(renderSize);
		const WrappedTriangles triangles = wrapTriangles(meshes, size / periodMeters, size);
		const size_t		   triangleCount = triangles.positions.size() / 3;
		const int			   tileSize = std::clamp(args.maxRenderSize, 1, renderSize);
		const bool			   tiled = tileSize < renderSize;

		const std::vector<uint16_t>&   indices = sequentialIndices();
		std::vector<Foundation::Vec2>  chunkPositions;
		std::vector<Foundation::Color> chunkColors;
		chunkPositions.reserve(kTrianglesPerDraw * 3);
		chunkColors.reserve(kTrianglesPerDraw * 3);

		std::vector<uint8_t> image(static_cast<size_t>(renderSize) * static_cast<size_t>(renderSize) * 4);
		std::vector<uint8_t> tilePixels;

		const ScreenStateGuard guard(args.restoreCoordinateSystem);
		for (int y0 = 0; y0 < renderSize; y0 += tileSize) {
			const int height = std::min(tileSize, renderSize - y0);
			for (int x0 = 0; x0 < renderSize; x0 += tileSize) {
				const int			   width = std::min(tileSize, renderSize - x0);
				Renderer::RenderTarget target = Renderer::RenderTarget::create(width, height);
				if (!target.isValid()) {
					LOG_ERROR(Engine, "GroundTileBaker: could not create a %dx%d render target", width, height);
					return {};
				}
				target.begin();
				// The screen-space fallback projection (no coordinate system) matches the target exactly.
				// After begin(), so the size is the target's, not remembered as the backbuffer's.
				Renderer::Primitives::setCoordinateSystem(nullptr);
				Renderer::Primitives::setViewport(width, height);
				Renderer::Primitives::clear(Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F));
				Renderer::Primitives::beginFrame();

				// Each draw is flushed on its own, so the batch never holds more than one draw's triangles.
				const auto drawChunk = [&] {
					if (chunkPositions.empty()) {
						return;
					}
					Renderer::Primitives::drawTriangles({
						.vertices = chunkPositions.data(),
						.indices = indices.data(),
						.vertexCount = chunkPositions.size(),
						.indexCount = chunkPositions.size(),
						.colors = chunkColors.data(),
					});
					Renderer::Primitives::flush();
					chunkPositions.clear();
					chunkColors.clear();
				};
				const Foundation::Vec2 origin(static_cast<float>(x0), static_cast<float>(y0));
				for (size_t t = 0; t < triangleCount; ++t) {
					const Foundation::Vec2* p = &triangles.positions[t * 3];
					if (tiled) {
						const float minX = std::min({p[0].x, p[1].x, p[2].x}) - origin.x;
						const float maxX = std::max({p[0].x, p[1].x, p[2].x}) - origin.x;
						const float minY = std::min({p[0].y, p[1].y, p[2].y}) - origin.y;
						const float maxY = std::max({p[0].y, p[1].y, p[2].y}) - origin.y;
						if (minX >= static_cast<float>(width) || maxX <= 0.0F || minY >= static_cast<float>(height) || maxY <= 0.0F) {
							continue;
						}
					}
					for (size_t k = 0; k < 3; ++k) {
						chunkPositions.push_back(p[k] - origin);
						chunkColors.push_back(triangles.colors[(t * 3) + k]);
					}
					if (chunkPositions.size() == kTrianglesPerDraw * 3) {
						drawChunk();
					}
				}
				drawChunk();
				Renderer::Primitives::endFrame();

				tilePixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
				target.readPixels(tilePixels);
				target.end();

				// readPixels gives the tile's bottom row first; the image runs top to bottom.
				for (int row = 0; row < height; ++row) {
					const uint8_t* src = &tilePixels[static_cast<size_t>(height - 1 - row) * static_cast<size_t>(width) * 4];
					uint8_t*	   dst =
						&image[((static_cast<size_t>(y0 + row) * static_cast<size_t>(renderSize)) + static_cast<size_t>(x0)) * 4];
					std::memcpy(dst, src, static_cast<size_t>(width) * 4);
				}
			}
		}
		return image;
	}

	std::optional<GroundVariantBake> GroundTileBaker::bakeVariant(const std::string& defName, uint32_t variant) const {
		const auto					   start = Clock::now();
		assets::AssetRegistry&		   registry = assets::AssetRegistry::Get();
		const assets::AssetDefinition* def = registry.getDefinition(defName);
		if (def == nullptr || def->role != assets::AssetRole::Ground) {
			LOG_ERROR(Engine, "GroundTileBaker: '%s' is not a ground def", defName.c_str());
			return std::nullopt;
		}
		const int layer = layerSize(*def);
		if (layer < 1 || layer > kMaxLayerSize || !(def->ground.periodMeters > 0.0F)) {
			LOG_ERROR(
				Engine,
				"GroundTileBaker: '%s' has a %d texel layer (%.3g m at %u px/m); the baker takes 1 to %d",
				defName.c_str(),
				layer,
				static_cast<double>(def->ground.periodMeters),
				def->ground.pixelsPerMeter,
				kMaxLayerSize
			);
			return std::nullopt;
		}

		GroundVariantBake bake;
		bake.variant = variant;
		bake.seed = variantSeed(defName, variant);

		const int							   levels = mipCount(layer);
		ground::LinearImage					   mip0;
		std::vector<renderer::TessellatedMesh> meshes;
		for (int level = 0; level < levels; ++level) {
			const int	size = std::max(1, layer >> level);
			const int	supersample = supersampleFor(level);
			const int	renderSize = size * supersample;
			const float texelsPerMeter = std::ldexp(static_cast<float>(def->ground.pixelsPerMeter), -level);

			auto		 phase = Clock::now();
			const bool	 drew = registry.buildMeshes(defName, bake.seed, meshes, texelsPerMeter);
			const double meshMs = msSince(phase);
			bake.meshMs += meshMs;
			if (!drew) {
				if (level == 0) {
					LOG_ERROR(Engine, "GroundTileBaker: '%s' variant %u drew nothing", defName.c_str(), variant);
					return std::nullopt;
				}
				// Every path is finer than this level; it renders empty and the mean lock fills it from mip 0.
				meshes.clear();
			}

			GroundMipSample sample;
			sample.meshMs = meshMs;
			sample.renderSize = renderSize;
			sample.supersample = supersample;
			sample.meshCount = meshes.size();
			for (const renderer::TessellatedMesh& mesh : meshes) {
				sample.vertexCount += mesh.vertices.size();
			}

			phase = Clock::now();
			const std::vector<uint8_t> pixels = renderWrapped(meshes, def->ground.periodMeters, renderSize);
			bake.renderMs += msSince(phase);
			if (pixels.empty()) {
				return std::nullopt;
			}

			phase = Clock::now();
			ground::LinearImage linear = ground::downsampleSrgb8Premultiplied(pixels, renderSize, renderSize, size, size, args.pool);
			sample.minAlpha = ground::minAlpha(linear);
			ground::LinearImage reference;
			if (level == 0) {
				mip0 = linear;
				reference = linear;
			} else {
				reference = ground::downsample(mip0, size, size, args.pool);
				sample.lockShift = glm::vec3(ground::lockMean(linear, reference, kMeanLockSigma));
			}
			ground::Srgb8Image		  encoded = ground::encodeSrgb8(linear, bake.seed ^ (static_cast<uint32_t>(level) * 0x9E3779B9U));
			const ground::LinearImage decoded = ground::decodeSrgb8(encoded);
			sample.mean = glm::vec3(ground::mean(decoded));
			sample.contrast = ground::windowedRmsContrast(decoded);
			sample.referenceContrast = ground::windowedRmsContrast(reference);
			bake.mips.push_back({.size = size, .rgba = std::move(encoded.rgba)});
			bake.samples.push_back(sample);
			bake.filterMs += msSince(phase);
		}

		const auto thinnest = std::min_element(bake.samples.begin(), bake.samples.end(), [](const auto& a, const auto& b) {
			return a.minAlpha < b.minAlpha;
		});
		if (thinnest->minAlpha < 1.0F - (0.5F / 255.0F)) {
			LOG_WARNING(
				Engine,
				"GroundTileBaker: '%s' variant %u isn't opaque (alpha down to %.3f at mip %d); a ground tile should cover its period",
				defName.c_str(),
				variant,
				static_cast<double>(thinnest->minAlpha),
				static_cast<int>(thinnest - bake.samples.begin())
			);
		}
		bake.totalMs = msSince(start);
		return bake;
	}

	std::optional<GroundTileBake> GroundTileBaker::bakeDef(const std::string& defName) const {
		const auto					   start = Clock::now();
		const assets::AssetDefinition* def = assets::AssetRegistry::Get().getDefinition(defName);
		if (def == nullptr || def->role != assets::AssetRole::Ground) {
			LOG_ERROR(Engine, "GroundTileBaker: '%s' is not a ground def", defName.c_str());
			return std::nullopt;
		}

		GroundTileBake bake;
		bake.defName = defName;
		bake.periodMeters = def->ground.periodMeters;
		bake.pixelsPerMeter = def->ground.pixelsPerMeter;
		bake.layerSize = layerSize(*def);
		const uint32_t variantCount = std::max(1U, def->variantCount);
		for (uint32_t variant = 0; variant < variantCount; ++variant) {
			std::optional<GroundVariantBake> baked = bakeVariant(defName, variant);
			if (!baked.has_value()) {
				return std::nullopt;
			}
			bake.variants.push_back(std::move(*baked));
		}
		bake.metrics = measure(bake.variants);
		bake.totalMs = msSince(start);

		// A line a log call: the logger truncates long messages.
		const std::string report = formatReport(bake);
		for (size_t begin = 0; begin < report.size();) {
			const size_t end = std::min(report.find('\n', begin), report.size());
			LOG_INFO(Engine, "%s", report.substr(begin, end - begin).c_str());
			begin = end + 1;
		}
		int failures = 0;
		for (const GroundMipMetrics& m : bake.metrics) {
			failures += m.failures();
		}
		if (failures > 0) {
			LOG_WARNING(Engine, "GroundTileBaker: '%s' fails %d acceptance checks (flagged in the report)", defName.c_str(), failures);
		}
		return bake;
	}

	std::vector<GroundMipMetrics> GroundTileBaker::measure(std::span<const GroundVariantBake> variants) {
		std::vector<GroundMipMetrics> metrics;
		if (variants.empty()) {
			return metrics;
		}
		size_t levels = variants.front().samples.size();
		for (const GroundVariantBake& v : variants) {
			levels = std::min(levels, v.samples.size());
		}

		for (size_t k = 0; k < levels; ++k) {
			GroundMipMetrics m;
			m.level = static_cast<int>(k);
			m.size = variants.front().mips[k].size;
			m.renderSize = variants.front().samples[k].renderSize;
			m.supersample = variants.front().samples[k].supersample;
			m.contrastRatioMin = std::numeric_limits<float>::max();
			m.contrastRatioMax = 0.0F;
			for (const GroundVariantBake& v : variants) {
				const GroundMipSample& s = v.samples[k];
				const GroundMipSample& s0 = v.samples[0];
				m.vertexCount = std::max(m.vertexCount, s.vertexCount);
				m.meshCount = std::max(m.meshCount, s.meshCount);
				m.meshMs += s.meshMs / static_cast<double>(variants.size());
				for (int c = 0; c < 3; ++c) {
					const float base = std::max(s0.mean[c], 1e-6F);
					m.meanErrorPercent = std::max(m.meanErrorPercent, 100.0F * std::abs(s.mean[c] - s0.mean[c]) / base);
					m.lockShiftPercent = std::max(m.lockShiftPercent, 100.0F * std::abs(s.lockShift[c]) / base);
				}
				m.deltaEToMip0 = std::max(m.deltaEToMip0, ground::deltaE76(s.mean, s0.mean));
				if (k > 0) {
					m.deltaEToPrevious = std::max(m.deltaEToPrevious, ground::deltaE76(s.mean, v.samples[k - 1].mean));
				}
				const float ratio = s.referenceContrast > 1e-6F ? s.contrast / s.referenceContrast : 1.0F;
				m.contrastRatioMin = std::min(m.contrastRatioMin, ratio);
				m.contrastRatioMax = std::max(m.contrastRatioMax, ratio);
			}
			for (size_t a = 0; a < variants.size(); ++a) {
				for (size_t b = a + 1; b < variants.size(); ++b) {
					m.variantSpread = std::max(m.variantSpread, ground::deltaE76(variants[a].samples[k].mean, variants[b].samples[k].mean));
				}
			}
			metrics.push_back(m);
		}
		return metrics;
	}

	std::string GroundTileBaker::formatReport(const GroundTileBake& bake) {
		std::string out;
		double		meshMs = 0.0;
		double		renderMs = 0.0;
		double		filterMs = 0.0;
		double		variantMs = 0.0;
		for (const GroundVariantBake& v : bake.variants) {
			meshMs += v.meshMs;
			renderMs += v.renderMs;
			filterMs += v.filterMs;
			variantMs += v.totalMs;
		}
		const double n = std::max<double>(1.0, static_cast<double>(bake.variants.size()));
		appendf(
			out,
			"Ground bake %s: %zu variants of %.3g m at %u px/m (%d texel layers), %.0f ms, %.0f ms a variant (mesh %.0f, render %.0f, "
			"filter %.0f)\n",
			bake.defName.c_str(),
			bake.variants.size(),
			static_cast<double>(bake.periodMeters),
			bake.pixelsPerMeter,
			bake.layerSize,
			bake.totalMs,
			variantMs / n,
			meshMs / n,
			renderMs / n,
			filterMs / n
		);
		appendf(out, "mip  size  render       verts  meshes  mesh ms  mean%%  dE0   dEprev  spread  contrast   lock%%\n");
		for (const GroundMipMetrics& m : bake.metrics) {
			appendf(
				out,
				"%3d  %4d  %4d (%dx)  %7zu  %6zu  %7.1f  %5.2f  %4.2f  %6.2f  %6.2f  %3.0f-%3.0f%%  %5.2f",
				m.level,
				m.size,
				m.renderSize,
				m.supersample,
				m.vertexCount,
				m.meshCount,
				m.meshMs,
				static_cast<double>(m.meanErrorPercent),
				static_cast<double>(m.deltaEToMip0),
				static_cast<double>(m.deltaEToPrevious),
				static_cast<double>(m.variantSpread),
				static_cast<double>(m.contrastRatioMin * 100.0F),
				static_cast<double>(m.contrastRatioMax * 100.0F),
				static_cast<double>(m.lockShiftPercent)
			);
			if (m.failures() > 0) {
				out += "  FAIL:";
				out += m.meanOk() ? "" : " mean";
				out += m.deltaEOk() ? "" : " dE";
				out += m.spreadOk() ? "" : " spread";
				out += m.contrastOk() ? "" : " contrast";
			}
			out += "\n";
		}
		return out;
	}

} // namespace worldview

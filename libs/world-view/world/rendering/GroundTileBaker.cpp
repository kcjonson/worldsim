#include "world/rendering/GroundTileBaker.h"

#include "world/rendering/GroundTileCache.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <resources/RenderTarget.h>
#include <threading/TaskPool.h>
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

		// The level's triangles in render pixels, unshared (3 vertices each) and in paint order, the ones
		// that miss the target dropped. buildPaths has already put every path that crosses the tile edge
		// back in at the opposite one, right after the original, so the target's clip is the whole wrap.
		struct Triangles {
			std::vector<Foundation::Vec2>  positions; // 3 a triangle
			std::vector<Foundation::Color> colors;
		};

		Triangles scaleTriangles(std::span<const renderer::TessellatedMesh> meshes, float scale, float renderSize) {
			Triangles				out;
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
					if (minX >= renderSize || maxX <= 0.0F || minY >= renderSize || maxY <= 0.0F) {
						continue;
					}
					for (size_t k = 0; k < 3; ++k) {
						out.positions.push_back(p[k]);
						out.colors.push_back(c[k]);
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

		// The standard deviation of an image's alpha: the spread of its heights.
		float heightSpread(const ground::LinearImage& image) {
			if (image.texels.empty()) {
				return 0.0F;
			}
			double sum = 0.0;
			double sumSquares = 0.0;
			for (const glm::vec4& t : image.texels) {
				sum += t.a;
				sumSquares += static_cast<double>(t.a) * t.a;
			}
			const double n = static_cast<double>(image.texels.size());
			const double average = sum / n;
			return static_cast<float>(std::sqrt(std::max(0.0, (sumSquares / n) - (average * average))));
		}

		void appendf(std::string& out, const char* format, auto... values) {
			char line[256];
			std::snprintf(line, sizeof(line), format, values...);
			out += line;
		}

	} // namespace

	GroundTileBaker::GroundTileBaker(const Args& newArgs)
		: args(newArgs) {}

	uint32_t GroundTileBaker::variantSeed(const std::string& seedGroup, uint32_t variant) {
		uint32_t h = 2166136261U; // FNV-1a over the group
		for (const char c : seedGroup) {
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
	GroundTileBaker::render(std::span<const renderer::TessellatedMesh> meshes, float periodMeters, int renderSize) const {
		const float		size = static_cast<float>(renderSize);
		const Triangles triangles = scaleTriangles(meshes, size / periodMeters, size);
		const size_t	triangleCount = triangles.positions.size() / 3;
		const int		tileSize = std::clamp(args.maxRenderSize, 1, renderSize);
		const bool		tiled = tileSize < renderSize;

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

	std::optional<GroundTileBake> GroundTileBaker::begin(const std::string& defName) const {
		const assets::AssetDefinition* def = assets::AssetRegistry::Get().getDefinition(defName);
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

		std::optional<uint64_t> key;
		if (args.cache != nullptr) {
			key = GroundTileCache::keyFor(*def, args.maxRenderSize);
			std::optional<GroundTileBake> cached = key.has_value() ? args.cache->load(*def, *key) : std::nullopt;
			if (cached.has_value()) {
				cached->cacheKey = key;
				const std::string report = formatReport(*cached);
				LOG_INFO(Engine, "%s", report.substr(0, report.find('\n')).c_str());
				return cached;
			}
		}

		GroundTileBake bake;
		bake.defName = defName;
		bake.periodMeters = def->ground.periodMeters;
		bake.pixelsPerMeter = def->ground.pixelsPerMeter;
		bake.layerSize = layer;
		bake.cacheKey = key;
		return bake;
	}

	std::optional<std::vector<GroundVariantPaths>> GroundTileBaker::buildPaths(GroundTileBake& bake) const {
		const auto					   start = Clock::now();
		assets::AssetRegistry&		   registry = assets::AssetRegistry::Get();
		const assets::AssetDefinition* def = registry.getDefinition(bake.defName);
		if (def == nullptr) {
			LOG_ERROR(Engine, "GroundTileBaker: '%s' is gone from the registry", bake.defName.c_str());
			return std::nullopt;
		}
		const uint32_t					count = std::max(1U, def->variantCount);
		const float						density = static_cast<float>(bake.pixelsPerMeter);
		std::vector<GroundVariantPaths> variants(count);
		std::vector<uint8_t>			built(count, 0); // not vector<bool>: workers write neighboring entries

		const auto build = [&](size_t first, size_t last) {
			for (size_t v = first; v < last; ++v) {
				variants[v].variant = static_cast<uint32_t>(v);
				variants[v].seed = variantSeed(def->ground.seedGroup, static_cast<uint32_t>(v));
				built[v] = registry.buildPaths(bake.defName, variants[v].seed, variants[v].paths, density) ? 1 : 0;
			}
		};
		if (args.pool != nullptr) {
			args.pool->parallelFor(0, count, 1, build);
		} else {
			build(0, count);
		}
		bake.pathsMs = msSince(start);

		for (uint32_t v = 0; v < count; ++v) {
			if (built[v] == 0) {
				LOG_ERROR(Engine, "GroundTileBaker: '%s' variant %u's generator failed", bake.defName.c_str(), v);
				return std::nullopt;
			}
		}
		return variants;
	}

	bool GroundTileBaker::bakeHeights(GroundTileBake& bake, std::span<GroundVariantPaths> variants) const {
		const auto start = Clock::now();
		const int  renderSize = bake.layerSize * kHeightSupersample;

		// The renders one after another on this thread, then the filters side by side on the pool.
		std::vector<std::vector<uint8_t>>	   drawn(variants.size());
		std::vector<renderer::TessellatedMesh> meshes;
		for (size_t v = 0; v < variants.size(); ++v) {
			if (!variants[v].paths.heightMeshesAt(static_cast<float>(bake.pixelsPerMeter), meshes)) {
				LOG_ERROR(Engine, "GroundTileBaker: '%s' variant %u has no heights to draw", bake.defName.c_str(), variants[v].variant);
				return false;
			}
			drawn[v] = render(meshes, bake.periodMeters, renderSize);
			if (drawn[v].empty()) {
				return false;
			}
		}

		std::vector<ground::Plane> planes(variants.size());
		const auto				   filter = [&](size_t first, size_t last) {
			  for (size_t v = first; v < last; ++v) {
				  ground::Plane gray(renderSize, renderSize);
				  for (size_t i = 0; i < gray.values.size(); ++i) {
					  gray.values[i] = static_cast<float>(drawn[v][i * 4]) / 255.0F;
				  }
				  planes[v] = ground::blurWrapped(ground::boxDownsample(gray, kHeightSupersample), kHeightBlurSigma);
			  }
		};
		if (args.pool != nullptr) {
			args.pool->parallelFor(0, variants.size(), 1, filter);
		} else {
			filter(0, variants.size());
		}
		ground::rankNormalize(planes, variants.empty() ? 0U : mix32(variants.front().seed ^ 0x68656967U));
		for (size_t v = 0; v < variants.size(); ++v) {
			variants[v].heights = std::move(planes[v]);
		}
		bake.heightsMs = msSince(start);
		return true;
	}

	std::optional<GroundVariantBake> GroundTileBaker::bakeVariant(const GroundTileBake& bake, const GroundVariantPaths& variantPaths) const {
		const auto		  start = Clock::now();
		GroundVariantBake out;
		out.variant = variantPaths.variant;
		out.seed = variantPaths.seed;
		out.generateMs = variantPaths.paths.generateMs;
		out.tessellateMs = variantPaths.paths.tessellateMs;

		const ground::Plane& heights = variantPaths.heights;
		if (heights.width != bake.layerSize || heights.height != bake.layerSize) {
			LOG_ERROR(Engine, "GroundTileBaker: '%s' variant %u has no heights; bakeHeights runs first", bake.defName.c_str(), out.variant);
			return std::nullopt;
		}

		const int							   levels = mipCount(bake.layerSize);
		ground::LinearImage					   mip0;
		std::vector<renderer::TessellatedMesh> meshes;
		for (int level = 0; level < levels; ++level) {
			const int	size = std::max(1, bake.layerSize >> level);
			const int	supersample = supersampleFor(level);
			const int	renderSize = size * supersample;
			const float texelsPerMeter = std::ldexp(static_cast<float>(bake.pixelsPerMeter), -level);

			// Drawing nothing past mip 0 is fine: every path is finer than this level, it renders empty, and
			// the mean lock fills it from mip 0.
			auto phase = Clock::now();
			if (!variantPaths.paths.meshesAt(texelsPerMeter, meshes) && level == 0) {
				LOG_ERROR(Engine, "GroundTileBaker: '%s' variant %u drew nothing", bake.defName.c_str(), out.variant);
				return std::nullopt;
			}

			GroundMipSample sample;
			sample.renderSize = renderSize;
			sample.supersample = supersample;
			sample.meshCount = meshes.size();
			for (const renderer::TessellatedMesh& mesh : meshes) {
				sample.vertexCount += mesh.vertices.size();
			}

			const std::vector<uint8_t> pixels = render(meshes, bake.periodMeters, renderSize);
			sample.renderMs = msSince(phase);
			out.renderMs += sample.renderMs;
			if (pixels.empty()) {
				return std::nullopt;
			}

			phase = Clock::now();
			ground::LinearImage linear = ground::downsampleSrgb8(pixels, renderSize, renderSize, size, size, args.pool);
			ground::LinearImage reference;
			if (level == 0) {
				for (size_t i = 0; i < linear.texels.size(); ++i) {
					linear.texels[i].a = heights.values[i];
				}
				mip0 = linear;
				reference = linear;
			} else {
				// The heights come down the chain filtered from mip 0's, copied in before the lock so it
				// leaves them be.
				reference = ground::downsample(mip0, size, size, args.pool);
				for (size_t i = 0; i < linear.texels.size(); ++i) {
					linear.texels[i].a = reference.texels[i].a;
				}
				sample.lockShift = glm::vec3(ground::lockMean(linear, reference, kMeanLockSigma));
			}
			ground::Srgb8Image		  encoded = ground::encodeSrgb8(linear, out.seed ^ (static_cast<uint32_t>(level) * 0x9E3779B9U));
			const ground::LinearImage decoded = ground::decodeSrgb8(encoded);
			sample.mean = glm::vec3(ground::mean(decoded));
			sample.heightSpread = heightSpread(decoded);
			sample.contrast = ground::windowedRmsContrast(decoded);
			sample.referenceContrast = ground::windowedRmsContrast(reference);
			out.mips.push_back({.size = size, .rgba = std::move(encoded.rgba)});
			out.samples.push_back(sample);
			out.filterMs += msSince(phase);
		}

		out.totalMs = msSince(start);
		return out;
	}

	void GroundTileBaker::finish(GroundTileBake& bake) const {
		bake.metrics = measure(bake.variants);
		bake.totalMs = bake.pathsMs + bake.heightsMs;
		for (const GroundVariantBake& v : bake.variants) {
			bake.totalMs += v.totalMs;
		}
		if (args.cache != nullptr && bake.cacheKey.has_value()) {
			const auto start = Clock::now();
			args.cache->store(bake, *bake.cacheKey);
			bake.totalMs += msSince(start);
		}

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
			LOG_WARNING(Engine, "GroundTileBaker: '%s' fails %d acceptance checks (flagged in the report)", bake.defName.c_str(), failures);
		}
	}

	std::optional<GroundTileBake> GroundTileBaker::bakeDef(const std::string& defName) const {
		std::optional<GroundTileBake> bake = begin(defName);
		if (!bake.has_value() || bake->fromCache) {
			return bake;
		}
		std::optional<std::vector<GroundVariantPaths>> paths = buildPaths(*bake);
		if (!paths.has_value() || !bakeHeights(*bake, *paths)) {
			return std::nullopt;
		}
		for (GroundVariantPaths& variantPaths : *paths) {
			std::optional<GroundVariantBake> baked = bakeVariant(*bake, variantPaths);
			if (!baked.has_value()) {
				return std::nullopt;
			}
			bake->variants.push_back(std::move(*baked));
			variantPaths.paths = {}; // drawn; let the memory go before the next one renders
		}
		finish(*bake);
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
				m.renderMs += s.renderMs / static_cast<double>(variants.size());
				m.heightSpread += s.heightSpread / static_cast<float>(variants.size());
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
		appendf(
			out,
			"Ground bake %s: %zu variants of %.3g m at %u px/m (%d texel layers), %.0f ms",
			bake.defName.c_str(),
			bake.variants.size(),
			static_cast<double>(bake.periodMeters),
			bake.pixelsPerMeter,
			bake.layerSize,
			bake.totalMs
		);
		if (bake.fromCache) {
			out += " from the cache\n";
		} else {
			double generateMs = 0.0;
			double tessellateMs = 0.0;
			double renderMs = 0.0;
			double filterMs = 0.0;
			for (const GroundVariantBake& v : bake.variants) {
				generateMs += v.generateMs;
				tessellateMs += v.tessellateMs;
				renderMs += v.renderMs;
				filterMs += v.filterMs;
			}
			const double n = std::max<double>(1.0, static_cast<double>(bake.variants.size()));
			appendf(
				out,
				": paths %.0f ms on the pool (generate %.0f, tessellate %.0f a variant), heights %.0f, then render %.0f, filter %.0f a "
				"variant\n",
				bake.pathsMs,
				generateMs / n,
				tessellateMs / n,
				bake.heightsMs,
				renderMs / n,
				filterMs / n
			);
		}
		appendf(out, "mip  size  render       verts  meshes   rnd ms  mean%%  dE0   dEprev  spread  contrast   lock%%  h sd\n");
		for (const GroundMipMetrics& m : bake.metrics) {
			appendf(
				out,
				"%3d  %4d  %4d (%dx)  %7zu  %6zu  %7.1f  %5.2f  %4.2f  %6.2f  %6.2f  %3.0f-%3.0f%%  %5.2f  %4.2f",
				m.level,
				m.size,
				m.renderSize,
				m.supersample,
				m.vertexCount,
				m.meshCount,
				m.renderMs,
				static_cast<double>(m.meanErrorPercent),
				static_cast<double>(m.deltaEToMip0),
				static_cast<double>(m.deltaEToPrevious),
				static_cast<double>(m.variantSpread),
				static_cast<double>(m.contrastRatioMin * 100.0F),
				static_cast<double>(m.contrastRatioMax * 100.0F),
				static_cast<double>(m.lockShiftPercent),
				static_cast<double>(m.heightSpread)
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

#include "world/rendering/GroundTileBank.h"

#include <assets/AssetRegistry.h>
#include <threading/TaskPool.h>
#include <utils/Log.h>

#include <GL/glew.h>

#include <algorithm>
#include <chrono>

namespace worldview {

	namespace {

		using Clock = std::chrono::steady_clock;

		double msSince(Clock::time_point start) {
			return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
		}

		double mebibytes(size_t bytes) {
			return static_cast<double>(bytes) / (1024.0 * 1024.0);
		}

		size_t levelBytes(int layerSize, int level, int layers) {
			const auto size = static_cast<size_t>(std::max(1, layerSize >> level));
			return size * size * 4 * static_cast<size_t>(layers);
		}

	} // namespace

	GroundTileBank::GroundTileBank(const Args& newArgs)
		: args(newArgs),
		  cache(GroundTileCache::defaultDirectory()) {}

	GroundTileBank::~GroundTileBank() {
		for (size_t i = 0; i < arrayCount; ++i) {
			if (arrayTable[i].texture != 0) {
				glDeleteTextures(1, &arrayTable[i].texture);
			}
		}
	}

	size_t GroundTileBank::bytes() const {
		size_t total = 0;
		for (size_t i = 0; i < arrayCount; ++i) {
			total += arrayTable[i].bytes;
		}
		return total;
	}

	void GroundTileBank::fail(std::string line) {
		LOG_ERROR(Renderer, "GroundTileBank: %s", line.c_str());
		errorLines.push_back(std::move(line));
	}

	GroundTileBaker GroundTileBank::baker() const {
		return GroundTileBaker({.restoreCoordinateSystem = args.restoreCoordinateSystem, .pool = pool.get(), .cache = &cache});
	}

	void GroundTileBank::begin() {
		if (isStarted) {
			return;
		}
		isStarted = true;
		startTime = Clock::now();

		const assets::AssetRegistry& registry = assets::AssetRegistry::Get();
		std::vector<std::string>	 names = registry.getDefinitionNames();
		std::sort(names.begin(), names.end());

		std::array<bool, world::kSurfaceCount> painted{};
		for (const std::string& name : names) {
			const assets::AssetDefinition* def = registry.getDefinition(name);
			if (def == nullptr || def->role != assets::AssetRole::Ground || !def->ground.surface.has_value()) {
				continue;
			}
			const world::Surface id = *def->ground.surface;
			const int			 layerSize = GroundTileBaker::layerSize(*def);
			size_t				 array = 0;
			while (array < arrayCount && arrayTable[array].layerSize != layerSize) {
				++array;
			}
			if (array == arrayCount) {
				if (arrayCount == kMaxArrays) {
					fail(
						name + ": a " + std::to_string(layerSize) + " texel layer would need a fifth texture array; the tile shader has " +
						std::to_string(kMaxArrays)
					);
					continue;
				}
				arrayTable[arrayCount++] = {.layerSize = layerSize, .levels = GroundTileBaker::mipCount(layerSize)};
			}

			const uint32_t variants = std::max(1U, def->variantCount);
			Surface&	   s = surfaces[static_cast<size_t>(id)];
			s = {
				.array = static_cast<int>(array),
				.firstLayer = arrayTable[array].layers,
				.layerCount = static_cast<int>(variants),
				.invPeriod = 1.0F / def->ground.periodMeters,
				.texelsPerMeter = static_cast<float>(def->ground.pixelsPerMeter),
				.rotate = def->ground.rotate,
				.macro = def->ground.macro,
			};
			painted[static_cast<size_t>(id)] = true;
			defs.push_back({.defName = name, .array = static_cast<int>(array), .firstLayer = s.firstLayer, .variants = variants});
			arrayTable[array].layers += static_cast<int>(variants);
			variantTotal += static_cast<int>(variants);
		}

		for (size_t id = 0; id < world::kSurfaceCount; ++id) {
			const auto surface = static_cast<world::Surface>(id);
			if (surface != world::Surface::Water && !painted[id]) {
				fail("no ground def paints " + world::surfaceToString(surface));
			}
		}

		allocateArrays();
		LOG_INFO(Renderer, "GroundTileBank: %d variants of %zu defs into %zu texture arrays", variantTotal, defs.size(), arrayCount);
		if (defs.empty()) {
			finish();
			return;
		}
		pool = std::make_unique<foundation::TaskPool>();
	}

	void GroundTileBank::allocateArrays() {
		for (size_t i = 0; i < arrayCount; ++i) {
			Array& a = arrayTable[i];
			glGenTextures(1, &a.texture);
			glBindTexture(GL_TEXTURE_2D_ARRAY, a.texture);
			for (int level = 0; level < a.levels; ++level) {
				const int size = std::max(1, a.layerSize >> level);
				glTexImage3D(GL_TEXTURE_2D_ARRAY, level, GL_SRGB8_ALPHA8, size, size, a.layers, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
				a.bytes += levelBytes(a.layerSize, level, a.layers);
			}
			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BASE_LEVEL, 0);
			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, a.levels - 1);
		}
		glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
	}

	void GroundTileBank::upload(const Def& def, const GroundVariantBake& bake) const {
		if (bake.variant >= def.variants) {
			return;
		}
		const Array& a = arrayTable[static_cast<size_t>(def.array)];
		const int	 layer = def.firstLayer + static_cast<int>(bake.variant);
		GLint		 previousAlignment = 4;
		glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glBindTexture(GL_TEXTURE_2D_ARRAY, a.texture);
		const int levels = std::min(a.levels, static_cast<int>(bake.mips.size()));
		for (int level = 0; level < levels; ++level) {
			const GroundMip& mip = bake.mips[static_cast<size_t>(level)];
			glTexSubImage3D(GL_TEXTURE_2D_ARRAY, level, 0, 0, layer, mip.size, mip.size, 1, GL_RGBA, GL_UNSIGNED_BYTE, mip.rgba.data());
		}
		glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
		glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);
	}

	bool GroundTileBank::bakeNext() {
		if (!isStarted) {
			begin();
		}
		if (ready()) {
			return false;
		}
		const GroundTileBaker groundBaker = baker();
		if (current.has_value()) {
			bakeVariant(groundBaker);
		} else {
			while (nextDef < defs.size() && !startDef(groundBaker)) {}
		}
		if (!ready()) {
			return true;
		}
		finish();
		return false;
	}

	void GroundTileBank::bakeAll() {
		while (bakeNext()) {}
	}

	bool GroundTileBank::startDef(const GroundTileBaker& groundBaker) {
		const Def&					  def = defs[nextDef];
		auto						  phase = Clock::now();
		std::optional<GroundTileBake> bake = groundBaker.begin(def.defName);
		lookupMs += msSince(phase);
		if (!bake.has_value()) {
			fail(def.defName + ": not a bakeable ground def (the log says why)");
			endDef();
			return false;
		}
		if (bake->fromCache) {
			phase = Clock::now();
			for (const GroundVariantBake& variant : bake->variants) {
				upload(def, variant);
			}
			uploadMs += msSince(phase);
			++cachedDefs;
			endDef();
			return false;
		}

		phase = Clock::now();
		std::optional<std::vector<GroundVariantPaths>> paths = groundBaker.buildPaths(*bake);
		pathsMs += msSince(phase);
		if (!paths.has_value()) {
			fail(def.defName + ": a variant's generator failed (the log says why)");
			endDef();
			return false;
		}
		current = std::move(*bake);
		currentPaths = std::move(*paths);
		return true;
	}

	void GroundTileBank::bakeVariant(const GroundTileBaker& groundBaker) {
		const Def&						 def = defs[nextDef];
		GroundVariantPaths&				 paths = currentPaths[current->variants.size()];
		auto							 phase = Clock::now();
		std::optional<GroundVariantBake> baked = groundBaker.bakeVariant(*current, paths);
		renderMs += msSince(phase);
		paths.paths = {}; // drawn; let the memory go before the next one renders
		if (!baked.has_value()) {
			fail(def.defName + ": variant " + std::to_string(paths.variant) + " failed to bake (the log says why)");
			endDef();
			return;
		}

		phase = Clock::now();
		upload(def, *baked);
		uploadMs += msSince(phase);
		current->variants.push_back(std::move(*baked)); // texels and all: finish stores them in the cache
		if (current->variants.size() < currentPaths.size()) {
			return;
		}
		phase = Clock::now();
		groundBaker.finish(*current);
		finishMs += msSince(phase);
		++bakedDefs;
		endDef();
	}

	void GroundTileBank::endDef() {
		doneVariants += static_cast<int>(defs[nextDef].variants);
		current.reset();
		currentPaths.clear();
		++nextDef;
	}

	void GroundTileBank::finish() {
		pool.reset();
		const double stepsMs = lookupMs + pathsMs + renderMs + finishMs + uploadMs;
		LOG_INFO(
			Renderer,
			"GroundTileBank: %d variants of %zu defs in %.0f ms of bake steps (%.0f ms wall clock), %d from the cache, %d baked, %zu "
			"error(s)",
			variantTotal,
			defs.size(),
			stepsMs,
			msSince(startTime),
			cachedDefs,
			bakedDefs,
			errorLines.size()
		);
		LOG_INFO(
			Renderer,
			"GroundTileBank: lookup %.0f ms, paths %.0f, render and filter %.0f, finish %.0f, upload %.0f; %.1f MiB of texture arrays",
			lookupMs,
			pathsMs,
			renderMs,
			finishMs,
			uploadMs,
			mebibytes(bytes())
		);
		for (size_t i = 0; i < arrayCount; ++i) {
			const Array& a = arrayTable[i];
			LOG_INFO(
				Renderer,
				"GroundTileBank: array %zu: %d layers of %dx%d, %d levels, %.1f MiB",
				i,
				a.layers,
				a.layerSize,
				a.layerSize,
				a.levels,
				mebibytes(a.bytes)
			);
		}
	}

} // namespace worldview

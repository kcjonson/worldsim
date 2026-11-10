#include "world/rendering/GroundTileBank.h"

#include <assets/AssetRegistry.h>
#include <assets/GroundLayer.h>
#include <threading/TaskPool.h>
#include <utils/Log.h>

#include <GL/glew.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <string>

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

		// Runs a bake step and catches what it throws, the message in `error`: left to salal's guard, an
		// exception would only be logged, and the next call would run the same step again.
		template <typename Step>
		bool guarded(Step&& step, std::string& error) {
			try {
				step();
				return true;
			} catch (const std::exception& e) {
				error = e.what();
			} catch (...) {
				error = "an exception that isn't a std::exception";
			}
			return false;
		}

		GLint glLimit(GLenum name) {
			GLint value = 0;
			glGetIntegerv(name, &value);
			return value;
		}

		// Drops the GL errors raised before a check, so the check sees only its own. Bounded: a lost
		// context keeps reporting.
		void clearGlErrors() {
			for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; ++i) {}
		}

	} // namespace

	GroundTileBank::GroundTileBank(const Args& newArgs)
		: args(newArgs) {}

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
		return GroundTileBaker(
			{.restoreCoordinateSystem = args.restoreCoordinateSystem,
			 .maxRenderSize = maxRenderSize,
			 .pool = pool.get(),
			 .cache = args.cache}
		);
	}

	void GroundTileBank::begin() {
		if (isStarted) {
			return;
		}
		isStarted = true;
		startTime = Clock::now();

		std::string error;
		if (!guarded([this] { plan(); }, error)) {
			fail("planning the ground bake threw: " + error);
			defs.clear();
		}
		LOG_INFO(Renderer, "GroundTileBank: %d variants of %zu defs into %zu texture arrays", variantTotal, defs.size(), arrayCount);
		if (defs.empty()) {
			finish();
		}
	}

	void GroundTileBank::plan() {
		const GLint maxTextureSize = glLimit(GL_MAX_TEXTURE_SIZE);
		const GLint maxLayers = glLimit(GL_MAX_ARRAY_TEXTURE_LAYERS);
		maxRenderSize = std::min(GroundTileBaker::Args{}.maxRenderSize, static_cast<int>(maxTextureSize));

		const assets::AssetRegistry& registry = assets::AssetRegistry::Get();
		std::vector<std::string>	 names = registry.getDefinitionNames();
		std::sort(names.begin(), names.end());

		std::array<bool, world::kSurfaceCount>					 painted{};
		std::array<std::string, world::kSurfaceCount>			 seedGroups;
		std::array<assets::GroundTransitions, world::kSurfaceCount> transitions{};
		for (const std::string& name : names) {
			const assets::AssetDefinition* def = registry.getDefinition(name);
			if (def == nullptr || def->role != assets::AssetRole::Ground || !def->ground.surface.has_value()) {
				continue;
			}
			const world::Surface id = *def->ground.surface;
			const int			 layerSize = GroundTileBaker::layerSize(*def);
			if (layerSize == 0) {
				char line[256];
				std::snprintf(
					line,
					sizeof(line),
					"%s: %.3g m at %u px/m is not a layer of 1 to %d whole texels",
					name.c_str(),
					static_cast<double>(def->ground.periodMeters),
					def->ground.pixelsPerMeter,
					assets::kMaxGroundLayerSize
				);
				fail(line);
				continue;
			}
			if (!assets::groundVariantCountSupported(def->variantCount)) {
				fail(
					name + ": " + std::to_string(def->variantCount) + " variants; a ground def bakes 1 to " +
					std::to_string(assets::kMaxGroundVariants)
				);
				continue;
			}
			if (layerSize > maxTextureSize) {
				fail(
					name + ": a " + std::to_string(layerSize) + " texel layer is past this GPU's largest texture, " +
					std::to_string(maxTextureSize)
				);
				continue;
			}
			size_t				 array = 0;
			while (array < arrayCount && arrayTable[array].layerSize != layerSize) {
				++array;
			}
			if (array == arrayCount) {
				if (arrayCount == kMaxArrays) {
					fail(
						name + ": a " + std::to_string(layerSize) +
						" texel layer would need one more texture array than the tile shader's " + std::to_string(kMaxArrays)
					);
					continue;
				}
				arrayTable[arrayCount++] = {.layerSize = layerSize, .levels = GroundTileBaker::mipCount(layerSize)};
			}

			const uint32_t variants = def->variantCount;
			if (arrayTable[array].layers + static_cast<int>(variants) > maxLayers) {
				fail(
					name + ": its " + std::to_string(variants) + " layers would take texture array " + std::to_string(array) + " past " +
					std::to_string(maxLayers) + ", this GPU's most"
				);
				continue;
			}
			Surface& s = surfaces[static_cast<size_t>(id)];
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
			seedGroups[static_cast<size_t>(id)] = def->ground.seedGroup;
			transitions[static_cast<size_t>(id)] = def->ground.transitions;
			defs.push_back({.defName = name, .array = static_cast<int>(array), .firstLayer = s.firstLayer, .variants = variants});
			arrayTable[array].layers += static_cast<int>(variants);
			variantTotal += static_cast<int>(variants);
		}

		for (size_t id = 0; id < world::kSurfaceCount; ++id) {
			const auto surface = static_cast<world::Surface>(id);
			if (surface != world::Surface::Water && !painted[id]) {
				fail("no ground def paints " + world::surfaceToString(surface));
			}
			surfaces[id].hashSurface = static_cast<int>(id);
			for (size_t other = 0; other < id; ++other) {
				if (painted[id] && painted[other] && seedGroups[other] == seedGroups[id]) {
					surfaces[id].hashSurface = static_cast<int>(other);
					break;
				}
			}
		}
		pairTable = buildGroundPairTable(transitions);

		allocateArrays();
		if (!defs.empty()) {
			pool = std::make_unique<foundation::TaskPool>();
		}
	}

	void GroundTileBank::allocateArrays() {
		clearGlErrors();
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

			const GLenum status = glGetError();
			if (status == GL_NO_ERROR) {
				continue;
			}
			char line[192];
			std::snprintf(
				line,
				sizeof(line),
				"texture array %zu (%d layers of %dx%d, %.1f MiB) couldn't be allocated: GL error 0x%04x%s",
				i,
				a.layers,
				a.layerSize,
				a.layerSize,
				mebibytes(a.bytes),
				status,
				status == GL_OUT_OF_MEMORY ? " (out of video memory)" : ""
			);
			fail(line);
			clearGlErrors();
			glDeleteTextures(1, &a.texture);
			a.texture = 0;
			a.bytes = 0;
		}
		glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

		// A def whose array isn't there has nowhere to upload, so it isn't baked.
		std::erase_if(defs, [this](const Def& def) {
			if (arrayTable[static_cast<size_t>(def.array)].texture != 0) {
				return false;
			}
			variantTotal -= static_cast<int>(def.variants);
			return true;
		});
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
		if (!lookedUp) {
			lookUpAll(groundBaker);
		}
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

	void GroundTileBank::lookUpAll(const GroundTileBaker& groundBaker) {
		const auto start = Clock::now();
		lookedUp = true;
		lookups.resize(defs.size());
		lookupErrors.resize(defs.size());
		pool->parallelFor(0, defs.size(), 1, [&](size_t first, size_t last) {
			for (size_t i = first; i < last; ++i) {
				std::string error;
				if (!guarded([&] { lookups[i] = groundBaker.begin(defs[i].defName); }, error)) {
					lookupErrors[i] = "its cache lookup threw: " + error;
				}
			}
		});
		lookupMs += msSince(start);
	}

	bool GroundTileBank::startDef(const GroundTileBaker& groundBaker) {
		const Def&					  def = defs[nextDef];
		std::optional<GroundTileBake> bake = std::move(lookups[nextDef]);
		lookups[nextDef].reset();
		if (!bake.has_value()) {
			const std::string& error = lookupErrors[nextDef];
			fail(def.defName + ": " + (error.empty() ? "not a bakeable ground def (the log says why)" : error));
			endDef();
			return false;
		}
		auto phase = Clock::now();
		if (bake->fromCache) {
			for (const GroundVariantBake& variant : bake->variants) {
				upload(def, variant);
			}
			uploadMs += msSince(phase);
			++cachedDefs;
			endDef();
			return false;
		}

		std::string									   error;
		std::optional<std::vector<GroundVariantPaths>> paths;
		const bool									   built = guarded([&] { paths = groundBaker.buildPaths(*bake); }, error);
		pathsMs += msSince(phase);
		if (!paths.has_value()) {
			fail(def.defName + ": " + (built ? "a variant's generator failed (the log says why)" : "building its paths threw: " + error));
			endDef();
			return false;
		}
		phase = Clock::now();
		bool	   drewHeights = false;
		const bool ranHeights = guarded([&] { drewHeights = groundBaker.bakeHeights(*bake, *paths); }, error);
		heightsMs += msSince(phase);
		if (!drewHeights) {
			fail(
				def.defName + ": " + (ranHeights ? "its heights failed to bake (the log says why)" : "baking its heights threw: " + error)
			);
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
		std::string						 error;
		std::optional<GroundVariantBake> baked;
		const bool						 ran = guarded([&] { baked = groundBaker.bakeVariant(*current, paths); }, error);
		renderMs += msSince(phase);
		paths.paths = {}; // drawn; let the memory go before the next one renders
		if (!baked.has_value()) {
			fail(
				def.defName + ": variant " + std::to_string(paths.variant) +
				(ran ? " failed to bake (the log says why)" : " threw while baking: " + error)
			);
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
		const bool finished = guarded([&] { groundBaker.finish(*current); }, error);
		finishMs += msSince(phase);
		if (finished) {
			++bakedDefs;
		} else {
			fail(def.defName + ": measuring and storing its bake threw: " + error);
		}
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
		const double stepsMs = lookupMs + pathsMs + heightsMs + renderMs + finishMs + uploadMs;
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
			"GroundTileBank: lookup %.0f ms, paths %.0f, heights %.0f, render and filter %.0f, finish %.0f, upload %.0f; %.1f MiB of "
			"texture arrays",
			lookupMs,
			pathsMs,
			heightsMs,
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

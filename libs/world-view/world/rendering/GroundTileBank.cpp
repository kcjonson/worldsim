#include "world/rendering/GroundTileBank.h"

#include <assets/AssetRegistry.h>
#include <threading/TaskPool.h>
#include <utils/Log.h>

#include <GL/glew.h>

#include <algorithm>
#include <chrono>
#include <cstdio>

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

	void GroundTileBank::begin() {
		if (isStarted) {
			return;
		}
		isStarted = true;

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
			for (uint32_t v = 0; v < variants; ++v) {
				jobs.push_back({
					.defName = name,
					.surface = id,
					.variant = v,
					.array = static_cast<int>(array),
					.layer = s.firstLayer + static_cast<int>(v),
					.lastOfDef = v + 1 == variants,
				});
			}
			arrayTable[array].layers += static_cast<int>(variants);
		}

		for (size_t id = 0; id < world::kSurfaceCount; ++id) {
			const auto surface = static_cast<world::Surface>(id);
			if (surface != world::Surface::Water && !painted[id]) {
				fail("no ground def paints " + world::surfaceToString(surface));
			}
		}

		allocateArrays();
		LOG_INFO(Renderer, "GroundTileBank: baking %zu variants into %zu texture arrays", jobs.size(), arrayCount);
		if (jobs.empty()) {
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

	void GroundTileBank::upload(const Job& job, const GroundVariantBake& bake) const {
		const Array& a = arrayTable[static_cast<size_t>(job.array)];
		GLint		 previousAlignment = 4;
		glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glBindTexture(GL_TEXTURE_2D_ARRAY, a.texture);
		const int levels = std::min(a.levels, static_cast<int>(bake.mips.size()));
		for (int level = 0; level < levels; ++level) {
			const GroundMip& mip = bake.mips[static_cast<size_t>(level)];
			glTexSubImage3D(GL_TEXTURE_2D_ARRAY, level, 0, 0, job.layer, mip.size, mip.size, 1, GL_RGBA, GL_UNSIGNED_BYTE, mip.rgba.data());
		}
		glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
		glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);
	}

	bool GroundTileBank::bakeNext() {
		if (!isStarted) {
			begin();
		}
		if (nextJob >= jobs.size()) {
			return false;
		}
		const Job& job = jobs[nextJob++];

		const GroundTileBaker			 baker({.restoreCoordinateSystem = args.restoreCoordinateSystem, .pool = pool.get()});
		const auto						 bakeStart = Clock::now();
		std::optional<GroundVariantBake> bake = baker.bakeVariant(job.defName, job.variant);
		bakeMs += msSince(bakeStart);
		if (!bake.has_value()) {
			fail(job.defName + ": variant " + std::to_string(job.variant) + " failed to bake (the log says why)");
		} else {
			const auto uploadStart = Clock::now();
			upload(job, *bake);
			uploadMs += msSince(uploadStart);
			// The report needs each mip's size and measurements, not its texels.
			for (GroundMip& mip : bake->mips) {
				std::vector<uint8_t>().swap(mip.rgba);
			}
			defVariants.push_back(std::move(*bake));
		}

		if (job.lastOfDef) {
			finishDef(job);
		}
		if (nextJob < jobs.size()) {
			return true;
		}
		finish();
		return false;
	}

	void GroundTileBank::bakeAll() {
		while (bakeNext()) {}
	}

	void GroundTileBank::finishDef(const Job& job) {
		if (defVariants.empty()) {
			return;
		}
		const assets::AssetDefinition* def = assets::AssetRegistry::Get().getDefinition(job.defName);
		GroundTileBake				   bake;
		bake.defName = job.defName;
		bake.periodMeters = def != nullptr ? def->ground.periodMeters : 0.0F;
		bake.pixelsPerMeter = def != nullptr ? def->ground.pixelsPerMeter : 0;
		bake.layerSize = arrayTable[static_cast<size_t>(job.array)].layerSize;
		bake.variants = std::move(defVariants);
		bake.metrics = GroundTileBaker::measure(bake.variants);
		for (const GroundVariantBake& v : bake.variants) {
			bake.totalMs += v.totalMs;
		}
		defVariants.clear();

		// A line a log call: the logger truncates long messages.
		const std::string report = GroundTileBaker::formatReport(bake);
		for (size_t start = 0; start < report.size();) {
			const size_t end = std::min(report.find('\n', start), report.size());
			LOG_INFO(Renderer, "%s", report.substr(start, end - start).c_str());
			start = end + 1;
		}
	}

	void GroundTileBank::finish() {
		pool.reset();
		LOG_INFO(
			Renderer,
			"GroundTileBank: %zu variants baked in %.0f ms (bake %.0f, upload %.0f), %zu error(s); %.1f MiB of texture arrays",
			jobs.size(),
			bakeMs + uploadMs,
			bakeMs,
			uploadMs,
			errorLines.size(),
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

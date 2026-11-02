#pragma once

// Bakes a ground def's tile variants into CPU-side mip chains (docs/technical/ground-textures.md,
// "Baker", "LOD" and "Heights in alpha"). Each variant's generator runs once and each of its paths is
// tessellated once (AssetRegistry::buildPaths, every variant at once on the pool); every mip is then drawn
// from those paths at that mip's density, so a path tagged with setLod drops out at the mip its author
// chose. Each is drawn once into a target covering exactly one period (the paths that cross the tile edge
// are already repeated, so the target's clip is the wrap), supersampled (8x8 for mips 0 and 1, 4x4
// below), Kaiser-downsampled in linear light with an anti-ringing clamp (GroundTileFilter.h), mean-locked
// to a filtered copy of mip 0, and encoded to sRGB 8-bit for GL_SRGB8_ALPHA8. Alpha is each texel's
// height: every path drawn once more at mip 0 in flat gray at its height (2x2 supersampled, blurred half
// a texel), ranked over the def's variants to a uniform histogram, and Kaiser-filtered down the chain. A
// def whose files haven't changed since its last bake comes from the disk cache instead
// (GroundTileCache.h). The runtime uploads the chains; the asset-manager shows them.
//
// buildPaths runs on any thread but a pool worker. bakeHeights, bakeVariant, and bakeDef that calls them,
// are GL thread only, outside any frame: each render opens and flushes its own offscreen batch, and puts
// the coordinate system, viewport and bound target back afterwards.

#include "world/rendering/GroundTileFilter.h"

#include <assets/TessellatedPaths.h>

#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Renderer {
	class CoordinateSystem;
}

namespace assets {
	struct AssetDefinition;
}

namespace renderer {
	struct TessellatedMesh;
}

namespace foundation {
	class TaskPool;
}

namespace worldview {

	class GroundTileCache;

	/// One mip of one variant: sRGB color and linear height in alpha, 8 bits a channel, rows top to
	/// bottom, size x size.
	struct GroundMip {
		int					 size = 0;
		std::vector<uint8_t> rgba;
	};

	/// What the bake drew and measured on one mip of one variant.
	struct GroundMipSample {
		int		  renderSize = 0;			// edge of the supersampled render, px
		int		  supersample = 1;			// render px per texel along each axis
		size_t	  vertexCount = 0;			// vertices the level's meshes hold, the wrap's copies included
		size_t	  meshCount = 0;			// meshes those took, each under 65,536 vertices
		double	  renderMs = 0.0;			// packing the level's meshes, the GPU draw and the readback
		glm::vec3 mean{0.0F};				// linear mean of the encoded mip
		float	  contrast = 0.0F;			// windowed RMS luminance contrast of the encoded mip
		float	  referenceContrast = 0.0F; // the same of Ref_k, the Kaiser downsample of mip 0
		glm::vec3 lockShift{0.0F};			// what the mean lock moved: Ref_k's linear mean minus the render's
		float	  heightSpread = 0.0F;		// standard deviation of the encoded mip's heights (alpha, 0 to 1)
	};

	/// One variant's generator output, tessellated: what every mip of it is drawn from.
	struct GroundVariantPaths {
		uint32_t				 variant = 0;
		uint32_t				 seed = 0;
		assets::TessellatedPaths paths;	  // its generateMs and tessellateMs say what it took
		ground::Plane			 heights; // mip 0's, ranked over the def's variants (bakeHeights)
	};

	struct GroundVariantBake {
		uint32_t					 variant = 0;
		uint32_t					 seed = 0;
		std::vector<GroundMip>		 mips; // mip 0 first
		std::vector<GroundMipSample> samples;
		double						 generateMs = 0.0;	 // the generator run, on a pool worker
		double						 tessellateMs = 0.0; // every path once, on the same worker
		double						 renderMs = 0.0;	 // every level's pack, GPU draw and readback
		double						 filterMs = 0.0;	 // downsample, mean lock, encode, measure
		double						 totalMs = 0.0;		 // render and filter, the GL thread's share
	};

	/// One mip level's acceptance metrics over all of a def's variants (docs/technical/ground-textures.md,
	/// research rules 4 and 5). Each is the worst over variants; a failure is flagged, never fatal.
	struct GroundMipMetrics {
		static constexpr float kMeanTolerancePercent = 0.5F;
		static constexpr float kDeltaELimit = 1.0F;
		static constexpr float kSpreadLimit = 0.5F;
		static constexpr float kContrastFloor = 0.7F;
		static constexpr float kContrastCeiling = 1.0F;

		int	   level = 0;
		int	   size = 0;
		int	   renderSize = 0;
		int	   supersample = 1;
		size_t vertexCount = 0; // the largest over variants
		size_t meshCount = 0;
		double renderMs = 0.0;			// this level's pack, draw and readback, averaged over variants
		float  meanErrorPercent = 0.0F; // |mean_k - mean_0| / mean_0, per channel, in linear light
		float  deltaEToMip0 = 0.0F;		// CIE76 of the means
		float  deltaEToPrevious = 0.0F; // against mip k - 1; 0 for mip 0
		float  variantSpread = 0.0F;	// largest delta E between two variants' means
		float  contrastRatioMin = 1.0F; // windowed contrast over Ref_k's, lowest variant
		float  contrastRatioMax = 1.0F; // and highest
		float  lockShiftPercent = 0.0F; // the mean lock's shift over mip 0's mean, per channel
		float  heightSpread = 0.0F;		// the heights' standard deviation, averaged over variants

		[[nodiscard]] bool meanOk() const { return meanErrorPercent <= kMeanTolerancePercent; }
		[[nodiscard]] bool deltaEOk() const { return deltaEToMip0 < kDeltaELimit && deltaEToPrevious < kDeltaELimit; }
		[[nodiscard]] bool spreadOk() const { return variantSpread < kSpreadLimit; }
		// Mip 0 is its own reference, so only coarser mips are held to the band.
		[[nodiscard]] bool contrastOk() const {
			return level == 0 || (contrastRatioMin >= kContrastFloor && contrastRatioMax <= kContrastCeiling);
		}
		[[nodiscard]] int failures() const {
			return static_cast<int>(!meanOk()) + static_cast<int>(!deltaEOk()) + static_cast<int>(!spreadOk()) +
				   static_cast<int>(!contrastOk());
		}
	};

	struct GroundTileBake {
		std::string					   defName;
		float						   periodMeters = 0.0F;
		uint32_t					   pixelsPerMeter = 0;
		int							   layerSize = 0;
		std::vector<GroundVariantBake> variants;
		std::vector<GroundMipMetrics>  metrics;			  // one a mip level
		double						   pathsMs = 0.0;	  // every variant's paths, wall clock
		double						   heightsMs = 0.0;	  // every variant's height render, filter and ranking
		double						   totalMs = 0.0;	  // the paths, heights, every variant's render and filter, and the store
		bool						   fromCache = false; // read back from disk, so totalMs is the only timing
		std::optional<uint64_t>		   cacheKey;		  // the def's files' key when the bake began; none without a cache
	};

	class GroundTileBaker {
	  public:
		struct Args {
			/// The coordinate system to put back after each render. salal's Primitives takes one but has no
			/// getter, so the caller says which: the app's, or null where there is none (tests, tools).
			Renderer::CoordinateSystem* restoreCoordinateSystem = nullptr;
			/// Largest render target edge; a supersampled mip bigger than this renders in tiles.
			int maxRenderSize = 4096;
			/// Builds the variants' paths, and runs the filters' rows, in parallel when given. A pool runs
			/// one parallelFor at a time, so nothing else may use it while a bake step runs.
			foundation::TaskPool* pool = nullptr;
			/// Where bakes are kept between runs; without one every bake starts from the generator.
			const GroundTileCache* cache = nullptr;
		};

		/// Part of every cache key: bump it when the bake draws or filters differently, so the bakes on
		/// disk from before are dropped.
		static constexpr uint32_t kVersion = 3;
		static constexpr int	  kMaxMipLevels = 7;	 // mips 0 to 6
		static constexpr int	  kMaxLayerSize = 1024;	 // the supersampled mip 0 is then 8192 px a side
		static constexpr float	  kMeanLockSigma = 2.0F; // texels of the mip being locked
		static constexpr int	  kHeightSupersample = 2;
		static constexpr float	  kHeightBlurSigma = 0.5F; // mip 0 texels: the contour a threshold traces isn't jagged

		explicit GroundTileBaker(const Args& args);

		/// The generator seed of a def's variant, salted by its seed group (the def's name unless it names
		/// one), so two defs' variant 0 differ unless they share a group.
		[[nodiscard]] static uint32_t variantSeed(const std::string& seedGroup, uint32_t variant);
		/// Texels along a layer's edge at mip 0: period x pixelsPerMeter.
		[[nodiscard]] static int layerSize(const assets::AssetDefinition& def);
		/// Mips 0 to 6, fewer when the layer reaches 1 texel sooner.
		[[nodiscard]] static int mipCount(int layerSize);
		[[nodiscard]] static int supersampleFor(int level);

		/// A bake's first step: the def's bake from the disk cache when it holds one of the def's files as
		/// they are now (fromCache set, nothing left to do), else the def's bake with its period, density,
		/// layer size and cache key filled and no variants yet. Nullopt (logged) when the def isn't a
		/// bakeable ground def.
		[[nodiscard]] std::optional<GroundTileBake> begin(const std::string& defName) const;

		/// Every variant's paths, one generator run and one tessellation each, all at once on the pool;
		/// sets bake.pathsMs. Nullopt (logged) when a generator fails.
		[[nodiscard]] std::optional<std::vector<GroundVariantPaths>> buildPaths(GroundTileBake& bake) const;

		/// Every variant's mip 0 heights, drawn from its paths and ranked over all of them, into each one's
		/// heights; sets bake.heightsMs. False (logged) when a variant drew nothing or a render target
		/// couldn't be made.
		[[nodiscard]] bool bakeHeights(GroundTileBake& bake, std::span<GroundVariantPaths> variants) const;

		/// One variant's chain drawn from its paths, its heights (bakeHeights) in alpha, or nullopt (logged)
		/// when mip 0 drew nothing, the heights are missing or a render target couldn't be made.
		[[nodiscard]] std::optional<GroundVariantBake> bakeVariant(const GroundTileBake& bake, const GroundVariantPaths& paths) const;

		/// Measures the bake's variants, logs the report (a warning when a metric fails), and stores the
		/// bake in the disk cache under the key begin took.
		void finish(GroundTileBake& bake) const;

		/// begin, then when it isn't cached buildPaths, bakeHeights, every variant's bakeVariant, and finish.
		[[nodiscard]] std::optional<GroundTileBake> bakeDef(const std::string& defName) const;

		/// The metrics over variants already baked (all with the same levels).
		[[nodiscard]] static std::vector<GroundMipMetrics> measure(std::span<const GroundVariantBake> variants);

		/// The metrics and timings as a text table, one line a mip, failures flagged.
		[[nodiscard]] static std::string formatReport(const GroundTileBake& bake);

	  private:
		// The meshes drawn once into a renderSize square covering one period: premultiplied 8-bit sRGB,
		// rows top to bottom. Empty when a render target couldn't be made.
		[[nodiscard]] std::vector<uint8_t>
		render(std::span<const renderer::TessellatedMesh> meshes, float periodMeters, int renderSize) const;

		Args args;
	};

} // namespace worldview

#pragma once

// Bakes a ground def's tile variants into CPU-side mip chains (docs/technical/ground-textures.md,
// "Baker" and "LOD"). Every mip is drawn from the vectors at that mip's density, so a path tagged with
// setLod drops out at the mip its author chose. Each is drawn 3x3 wrapped into a target covering exactly
// one period, supersampled (8x8 for mips 0 and 1, 4x4 below), Kaiser-downsampled in linear light with an
// anti-ringing clamp (GroundTileFilter.h), mean-locked to a filtered copy of mip 0, and encoded to sRGB
// 8-bit for GL_SRGB8_ALPHA8. The runtime uploads the chains; the asset-manager shows them.
//
// GL thread only, outside any frame: each render opens and flushes its own offscreen batch, and puts the
// coordinate system, viewport and bound target back afterwards.

#include "world/rendering/GroundTileFilter.h"

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

	/// One mip of one variant: straight-alpha sRGB, 8 bits a channel, rows top to bottom, size x size.
	struct GroundMip {
		int					 size = 0;
		std::vector<uint8_t> rgba;
	};

	/// What the bake drew and measured on one mip of one variant.
	struct GroundMipSample {
		int		  renderSize = 0;			// edge of the supersampled render, px
		int		  supersample = 1;			// render px per texel along each axis
		size_t	  vertexCount = 0;			// vertices the level's meshes hold (before the wrap copies)
		size_t	  meshCount = 0;			// meshes those took, each under 65,536 vertices
		glm::vec3 mean{0.0F};				// linear mean of the encoded mip
		float	  contrast = 0.0F;			// windowed RMS luminance contrast of the encoded mip
		float	  referenceContrast = 0.0F; // the same of Ref_k, the Kaiser downsample of mip 0
		glm::vec3 lockShift{0.0F};			// what the mean lock moved: Ref_k's linear mean minus the render's
		float	  minAlpha = 1.0F;			// smallest alpha in the render; a ground tile should be opaque
	};

	struct GroundVariantBake {
		uint32_t					 variant = 0;
		uint32_t					 seed = 0;
		std::vector<GroundMip>		 mips; // mip 0 first
		std::vector<GroundMipSample> samples;
		double						 meshMs = 0.0;	 // the generator and tessellation, every level
		double						 renderMs = 0.0; // GPU draw and readback
		double						 filterMs = 0.0; // downsample, mean lock, encode, measure
		double						 totalMs = 0.0;
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
		float  meanErrorPercent = 0.0F; // |mean_k - mean_0| / mean_0, per channel, in linear light
		float  deltaEToMip0 = 0.0F;		// CIE76 of the means
		float  deltaEToPrevious = 0.0F; // against mip k - 1; 0 for mip 0
		float  variantSpread = 0.0F;	// largest delta E between two variants' means
		float  contrastRatioMin = 1.0F; // windowed contrast over Ref_k's, lowest variant
		float  contrastRatioMax = 1.0F; // and highest
		float  lockShiftPercent = 0.0F; // the mean lock's shift over mip 0's mean, per channel

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
		std::vector<GroundMipMetrics>  metrics; // one a mip level
		double						   totalMs = 0.0;
	};

	class GroundTileBaker {
	  public:
		struct Args {
			/// The coordinate system to put back after each render. salal's Primitives takes one but has no
			/// getter, so the caller says which: the app's, or null where there is none (tests, tools).
			Renderer::CoordinateSystem* restoreCoordinateSystem = nullptr;
			/// Largest render target edge; a supersampled mip bigger than this renders in tiles.
			int maxRenderSize = 4096;
			/// Runs the filters' rows in parallel when given.
			foundation::TaskPool* pool = nullptr;
		};

		static constexpr int   kMaxMipLevels = 7;	  // mips 0 to 6
		static constexpr int   kMaxLayerSize = 1024;  // the supersampled mip 0 is then 8192 px a side
		static constexpr float kMeanLockSigma = 2.0F; // texels of the mip being locked

		explicit GroundTileBaker(const Args& args);

		/// The generator seed of a def's variant, salted by the def's name so two defs' variant 0 differ.
		[[nodiscard]] static uint32_t variantSeed(const std::string& defName, uint32_t variant);
		/// Texels along a layer's edge at mip 0: period x pixelsPerMeter.
		[[nodiscard]] static int layerSize(const assets::AssetDefinition& def);
		/// Mips 0 to 6, fewer when the layer reaches 1 texel sooner.
		[[nodiscard]] static int mipCount(int layerSize);
		[[nodiscard]] static int supersampleFor(int level);

		/// One variant's chain, or nullopt (logged) when the def isn't a bakeable ground def or mip 0 drew
		/// nothing.
		[[nodiscard]] std::optional<GroundVariantBake> bakeVariant(const std::string& defName, uint32_t variant) const;

		/// Every variant and the def's metrics. Logs the report, a warning when a metric fails.
		[[nodiscard]] std::optional<GroundTileBake> bakeDef(const std::string& defName) const;

		/// The metrics over variants already baked (all with the same levels).
		[[nodiscard]] static std::vector<GroundMipMetrics> measure(std::span<const GroundVariantBake> variants);

		/// The metrics and timings as a text table, one line a mip, failures flagged.
		[[nodiscard]] static std::string formatReport(const GroundTileBake& bake);

	  private:
		// The level's meshes drawn 3x3 wrapped into a renderSize square covering one period: premultiplied
		// 8-bit sRGB, rows top to bottom. Empty when a render target couldn't be made.
		[[nodiscard]] std::vector<uint8_t>
		renderWrapped(std::span<const renderer::TessellatedMesh> meshes, float periodMeters, int renderSize) const;

		Args args;
	};

} // namespace worldview

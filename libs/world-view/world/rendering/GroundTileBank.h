#pragma once

// The baked ground tiles every land surface paints with (docs/technical/ground-textures.md, "Baker").
// Each ground def's variants are baked by GroundTileBaker into sRGB mip chains and uploaded into
// GL_TEXTURE_2D_ARRAYs (GL_SRGB8_ALPHA8, every level, GL_REPEAT, trilinear), one array per layer size,
// with a per-surface table of where its layers sit and how the tile shader samples them.
//
// Built once the asset registry has loaded, on the GL thread and outside any frame, a variant a call,
// so a scene can keep drawing between them: begin(), then bakeNext() until it returns false.

#include "world/chunk/SurfaceField.h"
#include "world/rendering/GroundTileBaker.h"

#include <assets/AssetDefinition.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Renderer {
	class CoordinateSystem;
}

namespace foundation {
	class TaskPool;
}

namespace worldview {

	class GroundTileBank {
	  public:
		/// The tile shader has a sampler for each (u_groundTiles0 .. 3).
		static constexpr int kMaxArrays = 4;

		struct Args {
			/// The app's coordinate system, put back after each offscreen render.
			Renderer::CoordinateSystem* restoreCoordinateSystem = nullptr;
		};

		/// One GL_TEXTURE_2D_ARRAY: every layer the same size, every level baked from the vectors.
		struct Array {
			unsigned int texture = 0;
			int			 layerSize = 0; // texels along a layer's edge at mip 0
			int			 levels = 0;
			int			 layers = 0;
			size_t		 bytes = 0;
		};

		/// How the tile shader samples one surface. layerCount is 0 for Water, which has no tiles.
		struct Surface {
			int					 array = 0;
			int					 firstLayer = 0;
			int					 layerCount = 0;
			float				 invPeriod = 0.25F;
			float				 texelsPerMeter = 0.0F; // mip 0; the draw's LOD is log2(texelsPerMeter / screen px a meter) + bias
			assets::GroundRotate rotate = assets::GroundRotate::Free;
			assets::GroundMacro	 macro;
		};

		explicit GroundTileBank(const Args& args);
		~GroundTileBank();
		GroundTileBank(const GroundTileBank&) = delete;
		GroundTileBank& operator=(const GroundTileBank&) = delete;
		GroundTileBank(GroundTileBank&&) = delete;
		GroundTileBank& operator=(GroundTileBank&&) = delete;

		/// Plans the bake from the registry's ground defs and allocates the arrays. The registry must have
		/// finished loading. A land surface with no def, or a layer size past kMaxArrays, is an error.
		void begin();

		/// Bakes and uploads the next variant; false once none are left. Logs each def's bake report when
		/// its last variant is in, and the totals (time, bytes) at the end.
		bool bakeNext();

		/// begin() if it hasn't run, then every variant left.
		void bakeAll();

		[[nodiscard]] bool started() const { return isStarted; }
		[[nodiscard]] bool ready() const { return isStarted && nextJob >= jobs.size(); }
		[[nodiscard]] int  bakedVariants() const { return static_cast<int>(nextJob); }
		[[nodiscard]] int  totalVariants() const { return static_cast<int>(jobs.size()); }

		/// Bake failures and land surfaces nothing paints, one line each.
		[[nodiscard]] const std::vector<std::string>& errors() const { return errorLines; }

		[[nodiscard]] std::span<const Array> arrays() const { return {arrayTable.data(), arrayCount}; }
		[[nodiscard]] const Surface&		 surface(world::Surface id) const { return surfaces[static_cast<size_t>(id)]; }
		[[nodiscard]] size_t				 bytes() const;

	  private:
		struct Job {
			std::string	   defName;
			world::Surface surface = world::Surface::Grass;
			uint32_t	   variant = 0;
			int			   array = 0;
			int			   layer = 0;
			bool		   lastOfDef = false;
		};

		void allocateArrays();
		void upload(const Job& job, const GroundVariantBake& bake) const;
		void finishDef(const Job& job);
		void finish();
		void fail(std::string line);

		Args									  args;
		std::unique_ptr<foundation::TaskPool>	  pool;
		std::array<Array, kMaxArrays>			  arrayTable{};
		size_t									  arrayCount = 0;
		std::array<Surface, world::kSurfaceCount> surfaces{};
		std::vector<Job>						  jobs;
		size_t									  nextJob = 0;
		bool									  isStarted = false;
		std::vector<GroundVariantBake>			  defVariants; // the current def's, for its report
		std::vector<std::string>				  errorLines;
		double									  bakeMs = 0.0;
		double									  uploadMs = 0.0;
	};

} // namespace worldview

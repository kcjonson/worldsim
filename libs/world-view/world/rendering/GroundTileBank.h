#pragma once

// The baked ground tiles every land surface paints with (docs/technical/ground-textures.md, "Baker").
// Each ground def's variants are baked by GroundTileBaker into sRGB mip chains and uploaded into
// GL_TEXTURE_2D_ARRAYs (GL_SRGB8_ALPHA8, every level, GL_REPEAT, trilinear), one array per layer size,
// with a per-surface table of where its layers sit and how the tile shader samples them.
//
// Built once the asset registry has loaded, on the GL thread and outside any frame, a step a call, so a
// scene can keep drawing between them: begin(), then bakeNext() until it returns false. The first step
// looks every def up in the disk cache (GroundTileCache, beside the exe) at once, on the pool. A step
// uploads every def in a row that the cache held, and at the first it didn't, builds that def's paths
// on the pool; each later step bakes and uploads one of its variants, and the last stores the def in the
// cache.

#include "world/chunk/SurfaceField.h"
#include "world/rendering/GroundTileBaker.h"
#include "world/rendering/GroundTileCache.h"

#include <assets/AssetDefinition.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
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

		/// Runs the next step; false once none are left. Logs the totals (time, bytes) at the end; the baker
		/// logs each def's report.
		bool bakeNext();

		/// begin() if it hasn't run, then every step left.
		void bakeAll();

		[[nodiscard]] bool started() const { return isStarted; }
		[[nodiscard]] bool ready() const { return isStarted && nextDef >= defs.size(); }
		/// Variants in their layers (baked or read from the cache), or given up on with their def.
		[[nodiscard]] int bakedVariants() const {
			return doneVariants + (current.has_value() ? static_cast<int>(current->variants.size()) : 0);
		}
		[[nodiscard]] int totalVariants() const { return variantTotal; }

		/// Bake failures and land surfaces nothing paints, one line each.
		[[nodiscard]] const std::vector<std::string>& errors() const { return errorLines; }

		[[nodiscard]] std::span<const Array> arrays() const { return {arrayTable.data(), arrayCount}; }
		[[nodiscard]] const Surface&		 surface(world::Surface id) const { return surfaces[static_cast<size_t>(id)]; }
		[[nodiscard]] size_t				 bytes() const;

	  private:
		struct Def {
			std::string defName;
			int			array = 0;
			int			firstLayer = 0;
			uint32_t	variants = 0;
		};

		[[nodiscard]] GroundTileBaker baker() const;
		// Every def's cache lookup on the pool at once: reading the cached bakes is most of a warm bake.
		void lookUpAll(const GroundTileBaker& groundBaker);
		// The next def's cache lookup: true when it wasn't cached and its paths are built, so its variants
		// are left to bake; false when it's done (read from the cache and uploaded) or failed.
		bool startDef(const GroundTileBaker& groundBaker);
		void bakeVariant(const GroundTileBaker& groundBaker);
		void endDef();
		void allocateArrays();
		void upload(const Def& def, const GroundVariantBake& bake) const;
		void finish();
		void fail(std::string line);

		Args									  args;
		GroundTileCache							  cache;
		std::unique_ptr<foundation::TaskPool>	  pool;
		std::array<Array, kMaxArrays>			  arrayTable{};
		size_t									  arrayCount = 0;
		std::array<Surface, world::kSurfaceCount> surfaces{};
		std::vector<Def>						  defs;
		size_t									  nextDef = 0;
		std::vector<std::optional<GroundTileBake>> lookups;		// each def's, from lookUpAll, until startDef takes it
		bool									  lookedUp = false;
		std::optional<GroundTileBake>			  current;		// the def being baked, its variants so far
		std::vector<GroundVariantPaths>			  currentPaths; // and every variant's paths
		int										  doneVariants = 0;
		int										  variantTotal = 0;
		bool									  isStarted = false;
		std::vector<std::string>				  errorLines;

		std::chrono::steady_clock::time_point startTime;
		int									  cachedDefs = 0;
		int									  bakedDefs = 0;
		double								  lookupMs = 0.0; // lookUpAll: keys, and reading the cached defs
		double								  pathsMs = 0.0;
		double								  renderMs = 0.0; // bakeVariant: render and filter
		double								  finishMs = 0.0; // measure, report and store
		double								  uploadMs = 0.0;
	};

} // namespace worldview

#pragma once

#include <polygon/Polygon.h>
#include <vector/Types.h>

#include <cstdint>
#include <vector>

namespace assets {

	/// A selection silhouette: the filled outer outline of what a mesh draws (the union of its triangles, so it
	/// takes in fills AND stroke bands, e.g. a reed's stroke stem), in the mesh's frame in integer millimetres.
	/// `rings` is the outline (holes filled, separate bodies kept apart) used for the selection outline; `hitRegion`
	/// is the same closed by a wider radius, so gaps between separate bodies are clickable (whole-clump) and narrow
	/// notches forgive a little, used for hit-testing.
	struct AssetSilhouette {
		std::vector<geometry::Ring> rings;
		std::vector<geometry::Ring> hitRegion;
		bool						valid = false;
	};

	/// A triangle whose vertices are all below this alpha (a cast shadow) is not part of the silhouette, so it
	/// is neither outlined, clicked, nor counted as canopy.
	inline constexpr float kSilhouetteMinAlpha = 0.5F;

	/// Lengths of a silhouette as fractions of the longer side of its box (L): the outline is simplified to within
	/// L / 512 and closed by L / 512, so gaps under about L / 256 close and wiggles under L / 512 smooth out (a
	/// mature tree of about 6 m: 12 mm); the hit region is the outline closed by L / 64, bridging gaps up to
	/// about L / 32, and simplified to within L / 256.
	inline constexpr std::int64_t kSilhouetteToleranceDivisor = 512;
	inline constexpr std::int64_t kSilhouetteCloseDivisor = 512;
	inline constexpr std::int64_t kHitRegionCloseDivisor = 64;
	inline constexpr std::int64_t kHitRegionToleranceDivisor = 256;

	/// The silhouette of a mesh's triangles, those under kSilhouetteMinAlpha left out. `valid` is false only
	/// when nothing opaque is drawn.
	[[nodiscard]] AssetSilhouette silhouetteOfMesh(const renderer::TessellatedMesh& mesh);

	/// The silhouette of filled shapes in millimetres, any winding, known to cover what is drawn (a parts item's
	/// lifted footprints grown by the outline's half width): their union, and its hit region as silhouetteOfMesh's.
	[[nodiscard]] AssetSilhouette silhouetteOfShapes(std::vector<geometry::Ring> shapes);

} // namespace assets

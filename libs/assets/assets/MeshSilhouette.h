#pragma once

#include <polygon/Polygon.h>
#include <vector/Types.h>

#include <cstdint>
#include <vector>

namespace assets {

	/// A selection silhouette: the filled outer outline of what a mesh draws (the union of its triangles, so it
	/// takes in fills AND stroke bands, e.g. a reed's stroke stem), in the mesh's frame in integer millimetres.
	/// `rings` is the outline (holes filled, separate bodies kept apart) used for the selection outline; `hitRegion`
	/// is the same closed by a wider radius and holding all of `rings`, so gaps between separate bodies are clickable
	/// (whole-clump) and narrow notches forgive a little, used for hit-testing. Every opaque triangle lies inside `rings`
	/// or within geometry::silhouetteBoundMm of it.
	struct AssetSilhouette {
		std::vector<geometry::Ring> rings;
		std::vector<geometry::Ring> hitRegion;
		bool						valid = false;
	};

	/// A triangle whose vertices are all below this alpha (a cast shadow) is not part of the silhouette, so it
	/// is neither outlined, clicked, nor counted as canopy.
	inline constexpr float kSilhouetteMinAlpha = 0.5F;

	/// Lengths of a silhouette as fractions of the longer side of its box (L), so they scale with the object the
	/// way the old mask's cell did (L / 256).
	/// - The outline is simplified with a tolerance of L / 512, half that cell: a 6 m tree's 12 mm is 1.3 px at
	///   zoom 14 (112 px/m), a third of the outline's 4 px stroke, so the stroke covers the simplification. End to
	///   end, what is drawn lies inside the outline or within geometry::silhouetteBoundMm of it: the tolerance plus
	///   a millimetre for each union level, about 23 mm on a 6 m tree.
	/// - It is closed by L / 512, so gaps under about one cell (L / 256) fill, as the mask's sampling filled them.
	/// - The hit region closes by L / 64, the mask's 4-cell close, bridging gaps up to about L / 32 between a
	///   clump's bodies; it is simplified to L / 256 (one cell) since it is never drawn, and holds the outline.
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

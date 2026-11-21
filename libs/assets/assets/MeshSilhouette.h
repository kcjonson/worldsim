#pragma once

#include <polygon/Polygon.h>
#include <vector/Types.h>

#include <vector>

namespace assets {

	/// A selection silhouette: the filled outer outline of what a mesh draws (rasterized from its
	/// triangles, so it captures fills AND stroke bands, e.g. a reed's stroke stem), in the mesh's frame in
	/// integer millimeters. `rings` is the crisp outline (holes filled, disjoint blobs kept separate) used for
	/// the selection outline and later shadows; `hitRegion` is the same closed with a small morphological
	/// radius so gaps between disjoint blobs are clickable (whole-clump) and narrow concavities forgive
	/// slightly, used for hit-testing.
	struct AssetSilhouette {
		std::vector<geometry::Ring> rings;
		std::vector<geometry::Ring> hitRegion;
		bool						valid = false;
	};

	/// A triangle whose vertices are all below this alpha (a cast shadow) is not part of the silhouette, so it
	/// is neither outlined, clicked, nor counted as canopy.
	inline constexpr float kSilhouetteMinAlpha = 0.5F;

	/// The silhouette of a mesh's triangles, those under kSilhouetteMinAlpha left out. `valid` is false only
	/// when nothing opaque is drawn.
	[[nodiscard]] AssetSilhouette silhouetteOfMesh(const renderer::TessellatedMesh& mesh);

} // namespace assets

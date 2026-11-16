// Tests for AssetRegistry::getSilhouette (Story B): the per-template selection
// silhouette, plus the tessellator coverage-oracle cross-check that geometry
// can't run (it can't link the renderer). The silhouette is rasterized from the
// tessellated mesh triangles, so it must ENCLOSE the region the tessellator fills
// (it fills holes, so silhouette >= fill), and stroke-only assets get a real
// silhouette from their stroke bands (no bounds-rect fallback).

#include "assets/AssetRegistry.h"
#include "assets/AssetRegistry.test.h"

#include <core/Vec2i64.h>
#include <predicates/Predicates.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using namespace assets;

namespace {

	bool insideAny(const std::vector<geometry::Ring>& rings, const geometry::Vec2i64& p) {
		for (const auto& r : rings) {
			if (geometry::pointInPolygon(p, r) == geometry::PointInPolygon::Inside) {
				return true;
			}
		}
		return false;
	}

} // namespace

// The silhouette must enclose everything the tessellator fills: every filled
// triangle's centroid (strictly interior to the rendered fill) must land inside
// the silhouette, and a point well outside the mesh must not.
TEST(AssetRegistrySilhouette, EnclosesTessellatedFill) {
	auto& reg = AssetRegistry::Get();
	reg.clear();
	if (!loadShippedLibrary(reg)) {
		GTEST_SKIP() << "assets/world not found";
	}

	// Find a Simple fill asset with a non-trivial mesh and a valid silhouette.
	std::string chosen;
	for (const auto& name : reg.getDefinitionNames()) {
		const AssetDefinition* def = reg.getDefinition(name);
		if (def == nullptr || def->assetType != AssetType::Simple) {
			continue;
		}
		const renderer::TessellatedMesh* mesh = reg.getTemplate(name, TreeTraits{});
		if (mesh == nullptr || mesh->getTriangleCount() < 20) {
			continue;
		}
		const AssetSilhouette* sil = reg.getSilhouette(name, TreeTraits{});
		if (sil != nullptr && sil->valid && !sil->rings.empty()) {
			chosen = name;
			break;
		}
	}
	ASSERT_FALSE(chosen.empty()) << "no Simple fill asset with a usable mesh + silhouette";

	const renderer::TessellatedMesh* mesh = reg.getTemplate(chosen, TreeTraits{});
	const AssetSilhouette*			 sil  = reg.getSilhouette(chosen, TreeTraits{});
	ASSERT_NE(mesh, nullptr);
	ASSERT_NE(sil, nullptr);

	// Every non-degenerate opaque triangle centroid should be inside the silhouette. Allow
	// a small slack for raster staircasing clipping sliver triangles at the boundary.
	const auto faint = [&](uint16_t i) { return mesh->hasColors() && mesh->colors[i].a < AssetRegistry::kSilhouetteMinAlpha; };
	int		   inside = 0;
	int		   total  = 0;
	for (std::size_t t = 0; t < mesh->getTriangleCount(); ++t) {
		const uint16_t i0 = mesh->indices[3 * t + 0];
		const uint16_t i1 = mesh->indices[3 * t + 1];
		const uint16_t i2 = mesh->indices[3 * t + 2];
		if (faint(i0) && faint(i1) && faint(i2)) {
			continue; // shadows are left out of the silhouette
		}
		const auto& a = mesh->vertices[i0];
		const auto& b = mesh->vertices[i1];
		const auto& c = mesh->vertices[i2];
		const float area2 =
			std::abs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y));
		if (area2 < 1e-4F) {
			continue; // sliver/degenerate; centroid ill-defined vs the raster grid
		}
		const Foundation::Vec2 centroid{(a.x + b.x + c.x) / 3.0F, (a.y + b.y + c.y) / 3.0F};
		++total;
		if (insideAny(sil->rings, geometry::quantize(centroid))) {
			++inside;
		}
	}
	ASSERT_GT(total, 0);
	EXPECT_GE(static_cast<double>(inside) / total, 0.9) << chosen << ": " << inside << "/" << total << " centroids enclosed";

	// A point far outside the mesh bbox is outside the silhouette.
	float maxX = mesh->vertices[0].x;
	float maxY = mesh->vertices[0].y;
	for (const auto& v : mesh->vertices) {
		maxX = std::max(maxX, v.x);
		maxY = std::max(maxY, v.y);
	}
	EXPECT_FALSE(insideAny(sil->rings, geometry::quantize({maxX + 10.0F, maxY + 10.0F})));

	reg.clear();
}

// A stroke-only asset (SVG entirely fill="none", e.g. PlantFiber) still gets a real
// silhouette: rasterizing the tessellated STROKE BANDS captures the drawn strands, so
// no bounds-rect fallback is needed. Guards that stroke geometry reaches the silhouette.
TEST(AssetRegistrySilhouette, StrokeOnlyGetsRealSilhouette) {
	auto& reg = AssetRegistry::Get();
	reg.clear();
	if (!loadShippedLibrary(reg)) {
		GTEST_SKIP() << "assets/world not found";
	}

	if (reg.getDefinition("PlantFiber") == nullptr) {
		reg.clear();
		GTEST_SKIP() << "PlantFiber def not present";
	}

	const AssetSilhouette* sil = reg.getSilhouette("PlantFiber", TreeTraits{});
	ASSERT_NE(sil, nullptr);
	EXPECT_TRUE(sil->valid);
	EXPECT_FALSE(sil->rings.empty());
	EXPECT_FALSE(sil->hitRegion.empty());

	reg.clear();
}

// A cast shadow (under kSilhouetteMinAlpha) is drawn but is not part of the asset: it is
// neither outlined nor clickable.
TEST(AssetRegistrySilhouette, ShadowsAreLeftOut) {
	const TempAssetFolder folder;
	folder.write("Shaded/Shaded.xml", R"(<?xml version="1.0"?><AssetDefinitions><AssetDef>
  <defName>Flora_TestShaded</defName>
  <assetType>procedural</assetType>
  <generator><scriptPath>gen.lua</scriptPath></generator>
</AssetDef></AssetDefinitions>)");
	folder.write("Shaded/gen.lua", R"(
local function square(cx, alpha)
  local p = Path()
  p:addVertex(cx - 1, -1) p:addVertex(cx + 1, -1) p:addVertex(cx + 1, 1) p:addVertex(cx - 1, 1)
  p:setColor(0.2, 0.3, 0.1, alpha)
  asset:addPath(p)
end
square(5, 0.3)
square(0, 1.0)
)");
	auto& reg = AssetRegistry::Get();
	reg.clear();
	reg.loadDefinitionsFromFolder(folder.root.string());

	const AssetSilhouette* sil = reg.getSilhouette("Flora_TestShaded", TreeTraits{});
	ASSERT_NE(sil, nullptr);
	ASSERT_TRUE(sil->valid);
	EXPECT_TRUE(insideAny(sil->rings, geometry::quantize({0.0F, 0.0F})));
	EXPECT_FALSE(insideAny(sil->rings, geometry::quantize({5.0F, 0.0F})));
	EXPECT_FALSE(insideAny(sil->hitRegion, geometry::quantize({5.0F, 0.0F})));
	reg.clear();
}

// Tests for AssetRegistry::getSilhouette (Story B): the per-defName selection
// silhouette, plus the tessellator coverage-oracle cross-check that geometry
// can't run (it can't link the renderer). The silhouette must ENCLOSE the
// region the tessellator fills (it fills holes, so silhouette >= fill), and
// stroke-only assets must fall back to the mesh-bounds rectangle.

#include "assets/AssetRegistry.h"

#include <core/Vec2i64.h>
#include <predicates/Predicates.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <thread>

using namespace engine::assets;

namespace {

	// <root>/libs/engine/assets/AssetRegistrySilhouette.test.cpp -> walk up to <root>.
	std::filesystem::path projectRoot() {
		std::filesystem::path p = __FILE__;
		return p.parent_path().parent_path().parent_path().parent_path();
	}

	bool insideAny(const std::vector<geometry::Ring>& rings, const geometry::Vec2i64& p) {
		for (const auto& r : rings) {
			if (geometry::pointInPolygon(p, r) == geometry::PointInPolygon::Inside) {
				return true;
			}
		}
		return false;
	}

	bool loadWorld(AssetRegistry& reg) {
		const std::filesystem::path world = projectRoot() / "assets" / "world";
		if (!std::filesystem::exists(world)) {
			return false;
		}
		reg.setSharedScriptsPath(projectRoot() / "assets" / "shared" / "scripts");
		reg.beginLoadAsync(world.string());
		for (int i = 0; i < 1000 && !reg.isLoadComplete(); ++i) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		return reg.isLoadComplete();
	}

} // namespace

// The silhouette must enclose everything the tessellator fills: every filled
// triangle's centroid (strictly interior to the rendered fill) must land inside
// the silhouette, and a point well outside the mesh must not.
TEST(AssetRegistrySilhouette, EnclosesTessellatedFill) {
	auto& reg = AssetRegistry::Get();
	reg.clear();
	if (!loadWorld(reg)) {
		GTEST_SKIP() << "assets/world not found";
	}

	// Find a Simple fill asset with a non-trivial mesh and a valid silhouette.
	std::string chosen;
	for (const auto& name : reg.getDefinitionNames()) {
		const AssetDefinition* def = reg.getDefinition(name);
		if (def == nullptr || def->assetType != AssetType::Simple) {
			continue;
		}
		const renderer::TessellatedMesh* mesh = reg.getTemplate(name);
		if (mesh == nullptr || mesh->getTriangleCount() < 20) {
			continue;
		}
		const AssetSilhouette* sil = reg.getSilhouette(name);
		if (sil != nullptr && sil->valid && !sil->rings.empty()) {
			chosen = name;
			break;
		}
	}
	ASSERT_FALSE(chosen.empty()) << "no Simple fill asset with a usable mesh + silhouette";

	const renderer::TessellatedMesh* mesh = reg.getTemplate(chosen);
	const AssetSilhouette*			 sil  = reg.getSilhouette(chosen);
	ASSERT_NE(mesh, nullptr);
	ASSERT_NE(sil, nullptr);

	// Every non-degenerate triangle centroid should be inside the silhouette. Allow
	// a small slack for raster staircasing clipping sliver triangles at the boundary.
	int inside = 0;
	int total  = 0;
	for (std::size_t t = 0; t < mesh->getTriangleCount(); ++t) {
		const auto& a = mesh->vertices[mesh->indices[3 * t + 0]];
		const auto& b = mesh->vertices[mesh->indices[3 * t + 1]];
		const auto& c = mesh->vertices[mesh->indices[3 * t + 2]];
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
	if (!loadWorld(reg)) {
		GTEST_SKIP() << "assets/world not found";
	}

	if (reg.getDefinition("PlantFiber") == nullptr) {
		reg.clear();
		GTEST_SKIP() << "PlantFiber def not present";
	}

	const AssetSilhouette* sil = reg.getSilhouette("PlantFiber");
	ASSERT_NE(sil, nullptr);
	EXPECT_TRUE(sil->valid);
	EXPECT_FALSE(sil->rings.empty());
	EXPECT_FALSE(sil->hitRegion.empty());

	reg.clear();
}

// Tests for SVG ground defs: data-lod and data-height resolved down the tree, user units as meters
// whatever the preview size, and a def's mips keeping exactly the shapes at or below their density in
// document order. What the bake refuses is in AssetValidator.test.cpp, an SVG def's bake in
// GroundTileBaker.test.cpp and its cache key in GroundTileCache.test.cpp.

#include "assets/GroundSvg.h"
#include "assets/AssetRegistry.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace assets;

namespace {

	namespace fs = std::filesystem;

	GeneratedAsset read(const std::string& svg, std::vector<std::string>& issues) {
		return readGroundSvg(
			{.svg = std::as_bytes(std::span(svg)), .periodMeters = 4.0F, .pixelsPerMeter = 64, .name = "test.svg"}, issues
		);
	}

	// A unit square at (x, y) in the 4 m tile, filled with red `r` so a test can tell the shapes apart.
	std::string square(float x, float y, int r, const std::string& attributes = "") {
		return "<rect x=\"" + std::to_string(x) + "\" y=\"" + std::to_string(y) + "\" width=\"1\" height=\"1\" fill=\"rgb(" +
			   std::to_string(r) + ",0,0)\" " + attributes + "/>";
	}

	std::string tile(const std::string& inner) {
		return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 4 4\">" + inner + "</svg>";
	}

	float redOf(const GeneratedPath& path) {
		return path.fillColor.r * 255.0F;
	}

} // namespace

TEST(GroundSvgTest, DataLodIsTheLargestOnAShapeAndItsAncestors) {
	std::vector<std::string> issues;
	const GeneratedAsset	 out = read(
		tile(
			square(0, 0, 1) + "<g data-lod=\"8\">" + square(1, 0, 2) + square(2, 0, 3, "data-lod=\"4\"") + "<g data-lod=\"2\">" +
			square(3, 0, 4, "data-lod=\"32\"") + square(0, 1, 5) + "</g>" + "</g>" + square(1, 1, 6, "data-lod=\"16\"")
		),
		issues
	);
	EXPECT_TRUE(issues.empty());
	ASSERT_EQ(out.paths.size(), 6U);
	const std::vector<float> lods{0.0F, 8.0F, 8.0F, 32.0F, 8.0F, 16.0F};
	for (size_t i = 0; i < lods.size(); ++i) {
		EXPECT_FLOAT_EQ(redOf(out.paths[i]), static_cast<float>(i + 1)) << "document order";
		EXPECT_FLOAT_EQ(out.paths[i].minPixelsPerMeter, lods[i]) << "shape " << i;
	}
}

TEST(GroundSvgTest, DataHeightIsTheNearestOnAShapeOrItsAncestors) {
	std::vector<std::string> issues;
	const GeneratedAsset	 out = read(
		tile(
			square(0, 0, 1) + "<g data-height=\"0.3\">" + square(1, 0, 2) + square(2, 0, 3, "data-height=\"0.8\"") +
			"<g data-height=\"0.1\">" + square(3, 0, 4) + "</g><g>" + square(0, 1, 5) + "</g>" + "</g>" +
			square(1, 1, 6, "data-height=\"0\"")
		),
		issues
	);
	EXPECT_TRUE(issues.empty());
	ASSERT_EQ(out.paths.size(), 6U);
	EXPECT_FALSE(out.paths[0].height.has_value()) << "unset, so it takes its paint-order rank in the bake";
	const std::vector<float> heights{0.0F, 0.3F, 0.8F, 0.1F, 0.3F, 0.0F};
	for (size_t i = 1; i < heights.size(); ++i) {
		ASSERT_TRUE(out.paths[i].height.has_value()) << "shape " << i;
		EXPECT_FLOAT_EQ(*out.paths[i].height, heights[i]) << "shape " << i;
	}
}

TEST(GroundSvgTest, UserUnitsAreMetersWhateverThePreviewSize) {
	std::vector<std::string> issues;
	const GeneratedAsset	 out = read(
		"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"512px\" height=\"512px\" viewBox=\"0,0,4,4\">"
			"<g transform=\"translate(0.5 0)\"><rect x=\"1\" y=\"1\" width=\"2\" height=\"2\" fill=\"#ff0000\" "
			"fill-opacity=\"0.5\"/></g></svg>",
		issues
	);
	EXPECT_TRUE(issues.empty());
	ASSERT_EQ(out.paths.size(), 1U);
	float minX = 99.0F;
	float maxX = -99.0F;
	for (const Foundation::Vec2& v : out.paths[0].vertices) {
		minX = std::min(minX, v.x);
		maxX = std::max(maxX, v.x);
		EXPECT_GE(v.y, 1.0F - 1e-5F);
		EXPECT_LE(v.y, 3.0F + 1e-5F);
	}
	EXPECT_NEAR(minX, 1.5F, 1e-5F);
	EXPECT_NEAR(maxX, 3.5F, 1e-5F);
	EXPECT_FLOAT_EQ(out.paths[0].fillColor.r, 1.0F);
	EXPECT_NEAR(out.paths[0].fillColor.a, 0.5F, 0.01F);
}

namespace {

	struct TempDir {
		fs::path root;

		TempDir() {
			static std::atomic<int> counter{0};
			root = fs::temp_directory_path() /
				   ("ground_svg_" + std::to_string(reinterpret_cast<uintptr_t>(&counter)) + "_" + std::to_string(counter++));
			fs::create_directories(root);
		}
		~TempDir() {
			std::error_code ec;
			fs::remove_all(root, ec);
		}
		TempDir(const TempDir&) = delete;
		TempDir& operator=(const TempDir&) = delete;

		void write(const std::string& relPath, const std::string& content) const {
			const fs::path p = root / relPath;
			fs::create_directories(p.parent_path());
			std::ofstream(p, std::ios::binary) << content;
		}
	};

} // namespace

TEST(GroundSvgTest, AMipKeepsTheShapesAtOrBelowItsDensityInDocumentOrder) {
	// Shape 3 crosses the right edge, so its copy follows it.
	const TempDir t;
	t.write(
		"Tile/Tile.xml",
		"<?xml version=\"1.0\"?><AssetDefinitions><AssetDef><defName>Ground_Tile</defName><role>ground</role>"
		"<assetType>simple</assetType><svgPath>tile.svg</svgPath><surface>Grass</surface><period>4</period></AssetDef></AssetDefinitions>"
	);
	t.write(
		"Tile/tile.svg",
		tile(
			square(0.5F, 0.5F, 1) + "<g data-lod=\"32\">" + square(1, 1, 2) + "</g>" + square(3.5F, 2, 3, "data-lod=\"8\"") +
			square(2, 2, 4) + "<g data-lod=\"4\">" + square(0.5F, 2.5F, 5, "data-lod=\"16\"") + "</g>"
		)
	);
	auto& registry = AssetRegistry::Get();
	registry.clearDefinitions();
	registry.loadDefinitionsFromFolder(t.root.string());
	ASSERT_NE(registry.getDefinition("Ground_Tile"), nullptr);
	EXPECT_EQ(registry.getDefinition("Ground_Tile")->variantCount, 1U) << "one variant an <svgPath>";

	TessellatedPaths paths;
	ASSERT_TRUE(registry.buildPaths("Ground_Tile", 0, paths));
	const std::vector<std::pair<int, float>> built{{1, 0.0F}, {2, 32.0F}, {3, 8.0F}, {3, 8.0F}, {4, 0.0F}, {5, 16.0F}};
	ASSERT_EQ(paths.paths.size(), built.size());
	for (size_t i = 0; i < built.size(); ++i) {
		EXPECT_FLOAT_EQ(paths.colors[paths.paths[i].firstVertex].r * 255.0F, static_cast<float>(built[i].first)) << "path " << i;
		EXPECT_FLOAT_EQ(paths.paths[i].minPixelsPerMeter, built[i].second) << "path " << i;
	}

	for (const float density : {4.0F, 8.0F, 16.0F, 32.0F, AssetRegistry::kFullDetail}) {
		std::vector<float> expected; // each kept path's red, once a vertex, in paint order
		for (const TessellatedPaths::Path& path : paths.paths) {
			if (path.minPixelsPerMeter <= density) {
				expected.insert(expected.end(), path.vertexCount, paths.colors[path.firstVertex].r);
			}
		}
		std::vector<renderer::TessellatedMesh> meshes;
		ASSERT_TRUE(paths.meshesAt(density, meshes)) << density;
		std::vector<float> drawn;
		for (const renderer::TessellatedMesh& mesh : meshes) {
			for (const Foundation::Color& c : mesh.colors) {
				drawn.push_back(c.r);
			}
		}
		EXPECT_EQ(drawn, expected) << density;
	}
	const auto keptAt = [&paths](float density) {
		size_t kept = 0;
		for (const TessellatedPaths::Path& path : paths.paths) {
			kept += path.minPixelsPerMeter <= density ? 1 : 0;
		}
		return kept;
	};
	EXPECT_EQ(keptAt(4.0F), 2U);
	EXPECT_EQ(keptAt(8.0F), 4U);
	EXPECT_EQ(keptAt(16.0F), 5U);
	EXPECT_EQ(keptAt(32.0F), 6U);
	registry.clearDefinitions();
}

// Tests for the asset side of ground tile variants: the ground role and its schema, the Lua
// noise, setLod and setHeight bindings, paint-order heights, and meshes too big for 16-bit indices.

#include "assets/AssetDefinition.h"
#include "assets/AssetRegistry.h"
#include "assets/lua/LuaEngine.h"

#include <random/HashNoise.h>
#include <vector/Tessellator.h>
#include <world/chunk/Chunk.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace assets;

namespace {

	namespace fs = std::filesystem;

	struct TempDir {
		fs::path root;

		TempDir() {
			static std::atomic<int> counter{0};
			root = fs::temp_directory_path() / ("ground_assets_" + std::to_string(reinterpret_cast<uintptr_t>(&counter)) + "_" + std::to_string(counter++));
			fs::create_directories(root);
		}

		~TempDir() {
			std::error_code ec;
			fs::remove_all(root, ec);
		}

		TempDir(const TempDir&)			   = delete;
		TempDir& operator=(const TempDir&) = delete;

		void write(const std::string& relPath, const std::string& content) const {
			const fs::path p = root / relPath;
			fs::create_directories(p.parent_path());
			std::ofstream(p) << content;
		}
	};

	std::string groundXml(const std::string& defName, const std::string& body) {
		return "<?xml version=\"1.0\"?><AssetDefinitions><AssetDef><defName>" + defName + "</defName><role>ground</role>" +
			   "<assetType>procedural</assetType><generator><scriptPath>gen.lua</scriptPath></generator>" + body + "</AssetDef></AssetDefinitions>";
	}

	// Load a one-def ground asset whose generator is `script`; returns the registry's copy of the def.
	const AssetDefinition* loadGround(const TempDir& t, const std::string& body, const std::string& script = "-- empty\n") {
		t.write("Tile/Tile.xml", groundXml("Ground_Tile", body));
		t.write("Tile/gen.lua", script);
		auto& reg = AssetRegistry::Get();
		reg.clearDefinitions();
		reg.loadDefinitionsFromFolder(t.root.string());
		return reg.getDefinition("Ground_Tile");
	}

	// Run a Lua generator script and return what it drew; `error` gets the engine's message on failure.
	GeneratedAsset runLua(const std::string& script, bool* ok = nullptr, std::string* error = nullptr) {
		const TempDir t;
		t.write("gen.lua", script);
		LuaEngine engine;
		EXPECT_TRUE(engine.initialize());
		GeneratedAsset out;
		const bool	   result = engine.executeGenerator((t.root / "gen.lua").string(), GenerationContext{}, GeneratorParams{}, out);
		if (ok != nullptr) {
			*ok = result;
		}
		if (error != nullptr) {
			*error = engine.getLastError();
		}
		return out;
	}

} // namespace

TEST(GroundAssetTest, DefaultsApplyWhenOnlySurfaceIsGiven) {
	const TempDir		   t;
	const AssetDefinition* def = loadGround(t, "<surface>Dirt</surface>");
	ASSERT_NE(def, nullptr);

	EXPECT_EQ(def->role, AssetRole::Ground);
	ASSERT_TRUE(def->ground.surface.has_value());
	EXPECT_EQ(*def->ground.surface, world::Surface::Dirt);
	EXPECT_FLOAT_EQ(def->ground.periodMeters, 4.0F);
	EXPECT_EQ(def->ground.pixelsPerMeter, 64U);
	EXPECT_EQ(def->ground.rotate, GroundRotate::Free);
	EXPECT_EQ(def->variantCount, 8U);
	EXPECT_FLOAT_EQ(def->ground.macro.amplitude, 0.0F);
	EXPECT_EQ(def->ground.macro.dry, glm::vec3(1.0F));
	EXPECT_EQ(def->ground.macro.lush, glm::vec3(1.0F));
	EXPECT_TRUE(def->placement.biomes.empty()) << "placement only spawns defs that have <biome> rules";
}

TEST(GroundAssetTest, EveryFieldParses) {
	const TempDir		   t;
	const AssetDefinition* def = loadGround(
		t,
		"<surface>GrassTall</surface><period>2</period><pixelsPerMeter>128</pixelsPerMeter><rotate>quarter</rotate>"
		"<variantCount>12</variantCount>"
		"<macro><dry>0.9,0.8,0.6</dry><lush>0.5,0.9,0.4</lush><amplitude>0.2</amplitude><wavelengths>120,30</wavelengths></macro>"
	);
	ASSERT_NE(def, nullptr);

	ASSERT_TRUE(def->ground.surface.has_value());
	EXPECT_EQ(*def->ground.surface, world::Surface::GrassTall);
	EXPECT_FLOAT_EQ(def->ground.periodMeters, 2.0F);
	EXPECT_EQ(def->ground.pixelsPerMeter, 128U);
	EXPECT_EQ(def->ground.rotate, GroundRotate::Quarter);
	EXPECT_EQ(def->variantCount, 12U);
	EXPECT_EQ(def->ground.macro.dry, glm::vec3(0.9F, 0.8F, 0.6F));
	EXPECT_EQ(def->ground.macro.lush, glm::vec3(0.5F, 0.9F, 0.4F));
	EXPECT_FLOAT_EQ(def->ground.macro.amplitude, 0.2F);
	EXPECT_EQ(def->ground.macro.wavelengthsMeters, glm::vec2(120.0F, 30.0F));
}

TEST(GroundAssetTest, RotateModesParse) {
	const TempDir t;
	const auto	  rotateOf = [&t](const char* value) {
		   const AssetDefinition* def = loadGround(t, std::string("<surface>Dirt</surface><rotate>") + value + "</rotate>");
		   return def != nullptr ? def->ground.rotate : GroundRotate::Free;
	};
	EXPECT_EQ(rotateOf("quarter"), GroundRotate::Quarter);
	EXPECT_EQ(rotateOf("none"), GroundRotate::None);
	EXPECT_EQ(rotateOf("mirror"), GroundRotate::Mirror);
	EXPECT_EQ(rotateOf("Mirror"), GroundRotate::Mirror);
}

TEST(GroundAssetTest, BadSurfaceAndMacroKeepDefaults) {
	const TempDir		   t;
	const AssetDefinition* def = loadGround(t, "<surface>Lava</surface><rotate>spin</rotate><macro><dry>1,2</dry></macro>");
	ASSERT_NE(def, nullptr);

	EXPECT_FALSE(def->ground.surface.has_value());
	EXPECT_EQ(def->ground.rotate, GroundRotate::Free);
	EXPECT_EQ(def->ground.macro.dry, glm::vec3(1.0F));
}

TEST(GroundAssetTest, SurfaceNamesRoundTrip) {
	for (uint8_t i = 0; i < static_cast<uint8_t>(world::Surface::Count); ++i) {
		const auto surface = static_cast<world::Surface>(i);
		EXPECT_EQ(world::surfaceFromString(world::surfaceToString(surface)), surface);
	}
	EXPECT_FALSE(world::surfaceFromString("Lava").has_value());
	EXPECT_FALSE(world::surfaceFromString("").has_value());
}

TEST(GroundLuaTest, Noise2MatchesSalalAndSeedsDiffer) {
	bool		   ok = false;
	GeneratedAsset out = runLua(
		"local a, b = Path(), Path()\n"
		"for i = 0, 63 do\n"
		"    local x, y = i * 0.25 + 0.125, i * 0.375 - 5.0\n"
		"    a:addVertex(noise2(x, y, 11), noise2(x, y, 11))\n"
		"    b:addVertex(noise2(x, y, 12), 0)\n"
		"end\n"
		"asset:addPath(a)\n"
		"asset:addPath(b)\n",
		&ok
	);
	ASSERT_TRUE(ok);
	ASSERT_EQ(out.paths.size(), 2U);

	float peak		= 0.0F;
	bool  seedsDiffer = false;
	for (size_t i = 0; i < 64; ++i) {
		const float x = static_cast<float>(i) * 0.25F + 0.125F;
		const float y = static_cast<float>(i) * 0.375F - 5.0F;
		const auto& same = out.paths[0].vertices[i];
		EXPECT_EQ(same.x, same.y) << "noise2 must be deterministic for a seed";
		EXPECT_EQ(same.x, foundation::gradientNoise2(x, y, 11)) << "noise2 is salal's gradientNoise2";
		peak		= std::max(peak, std::abs(same.x));
		seedsDiffer = seedsDiffer || out.paths[1].vertices[i].x != same.x;
	}
	EXPECT_LE(peak, 1.01F);
	EXPECT_GT(peak, 0.3F) << "noise2 should vary, not return a constant";
	EXPECT_TRUE(seedsDiffer);
}

TEST(GroundLuaTest, PeriodicNoise2RepeatsEveryPeriod) {
	bool		   ok = false;
	GeneratedAsset out = runLua(
		// Coordinates are multiples of 1/16, so adding a period is exact in float. They cross the
		// period seam (cell 7 -> 8) and negative cells, and the period is given as a float.
		"local P = 8\n"
		"local alongX, alongY, both, plain, wrapped = Path(), Path(), Path(), Path(), Path()\n"
		"for i = 0, 63 do\n"
		"    local x, y = i * 0.1875 - 3.0, i * 0.3125 + 1.0\n"
		"    local v = periodicNoise2(x, y, P / 1.0, 7)\n"
		"    alongX:addVertex(v, periodicNoise2(x + P, y, P, 7))\n"
		"    alongY:addVertex(v, periodicNoise2(x, y + P, P, 7))\n"
		"    both:addVertex(v, periodicNoise2(x - 2 * P, y + 3 * P, P, 7))\n"
		"    local px, py = i * 0.1875 + 0.5, i * 0.3125 + 0.25\n"
		"    plain:addVertex(periodicNoise2(px, py, 1024, 7), noise2(px, py, 7))\n"
		"    local sx, sy = 7.0 + i / 64, 0.5 + i / 32\n" // inside cell 7, whose far corner wraps to cell 0
		"    wrapped:addVertex(periodicNoise2(sx, sy, 8, 7), noise2(sx, sy, 7))\n"
		"end\n"
		"asset:addPath(alongX)\nasset:addPath(alongY)\nasset:addPath(both)\nasset:addPath(plain)\nasset:addPath(wrapped)\n",
		&ok
	);
	ASSERT_TRUE(ok);
	ASSERT_EQ(out.paths.size(), 5U);

	for (size_t i = 0; i < 64; ++i) {
		EXPECT_EQ(out.paths[0].vertices[i].x, out.paths[0].vertices[i].y) << "x + period, sample " << i;
		EXPECT_EQ(out.paths[1].vertices[i].x, out.paths[1].vertices[i].y) << "y + period, sample " << i;
		EXPECT_EQ(out.paths[2].vertices[i].x, out.paths[2].vertices[i].y) << "both axes, sample " << i;
		EXPECT_EQ(out.paths[3].vertices[i].x, out.paths[3].vertices[i].y) 		<< "in cells 0 to period - 2 nothing wraps, so it is plain noise2, sample " << i;
	}
	const bool wrapChangesField = std::any_of(out.paths[4].vertices.begin(), out.paths[4].vertices.end(), [](const auto& v) { return v.x != v.y; });
	EXPECT_TRUE(wrapChangesField) << "in the last cell the wrapped lattice must differ from plain noise2";
}

TEST(GroundLuaTest, PeriodicNoise2TakesAPeriodAtInt32Max) {
	// i % period + period overflows int32 here; with the wrap done right the positive cells (all of
	// them far below period - 1) are plain noise2, and the negative ones wrap to the far end of the period.
	bool		   ok = false;
	GeneratedAsset out = runLua(
		"local positive, negative = Path(), Path()\n"
		"for i = 0, 15 do\n"
		"    local x, y = i * 0.75 + 0.5, i * 0.25 + 0.5\n"
		"    positive:addVertex(periodicNoise2(x, y, 2147483647, 7), noise2(x, y, 7))\n"
		"    negative:addVertex(periodicNoise2(-x, -y, 2147483647, 7), 0)\n"
		"end\n"
		"asset:addPath(positive)\nasset:addPath(negative)\n",
		&ok
	);
	ASSERT_TRUE(ok);
	ASSERT_EQ(out.paths.size(), 2U);
	for (size_t i = 0; i < 16; ++i) {
		EXPECT_EQ(out.paths[0].vertices[i].x, out.paths[0].vertices[i].y) << "sample " << i;
		EXPECT_LE(std::abs(out.paths[1].vertices[i].x), 1.01F) << "negative sample " << i;
	}
}

TEST(GroundLuaTest, PeriodicNoise2RejectsAPeriodThatIsNotAWholeNumber) {
	for (const char* period : {"0", "-3", "2.5"}) {
		bool		ok = true;
		std::string error;
		runLua(std::string("local v = periodicNoise2(0.5, 0.5, ") + period + ", 1)\n", &ok, &error);
		EXPECT_FALSE(ok) << period;
		EXPECT_NE(error.find("period"), std::string::npos) << error;
	}
}

TEST(GroundLuaTest, SetLodLandsOnTheGeneratedPath) {
	bool		   ok = false;
	GeneratedAsset out = runLua(
		"local always, fine = Path(), Path()\n"
		"fine:setLod(24)\n"
		"asset:addPath(always)\nasset:addPath(fine)\n",
		&ok
	);
	ASSERT_TRUE(ok);
	ASSERT_EQ(out.paths.size(), 2U);
	EXPECT_FLOAT_EQ(out.paths[0].minPixelsPerMeter, 0.0F);
	EXPECT_FLOAT_EQ(out.paths[1].minPixelsPerMeter, 24.0F);

	out.paths[1].clear();
	EXPECT_FLOAT_EQ(out.paths[1].minPixelsPerMeter, 0.0F);
}

TEST(GroundLuaTest, SetHeightLandsOnTheGeneratedPathWithinZeroToOne) {
	bool		   ok = false;
	GeneratedAsset out = runLua(
		"local unset, set = Path(), Path()\n"
		"set:setHeight(0.7)\n"
		"asset:addPath(unset)\nasset:addPath(set)\n",
		&ok
	);
	ASSERT_TRUE(ok);
	ASSERT_EQ(out.paths.size(), 2U);
	EXPECT_FALSE(out.paths[0].height.has_value());
	ASSERT_TRUE(out.paths[1].height.has_value());
	EXPECT_FLOAT_EQ(*out.paths[1].height, 0.7F);
	out.paths[1].clear();
	EXPECT_FALSE(out.paths[1].height.has_value());

	for (const char* height : {"-0.1", "1.5"}) {
		std::string error;
		runLua(std::string("Path():setHeight(") + height + ")\n", &ok, &error);
		EXPECT_FALSE(ok) << height;
		EXPECT_NE(error.find("setHeight"), std::string::npos) << error;
	}
}

namespace {

	// A Lua body that adds `count` triangles, path i spanning x in [i, i + 0.5].
	std::string trianglesScript(int count) {
		return "for i = 0, " + std::to_string(count - 1) +
			   " do\n"
			   "    local p = Path()\n"
			   "    p:addVertex(i, 0)\n"
			   "    p:addVertex(i + 0.5, 0)\n"
			   "    p:addVertex(i, 0.5)\n"
			   "    p:setColor(0.2, 0.5, 0.3, 1.0)\n"
			   "    p:close()\n"
			   "    asset:addPath(p)\n"
			   "end\n";
	}

} // namespace

TEST(GroundMeshTest, AssetOverSixtyFiveKVerticesSplitsIntoValidMeshes) {
	constexpr int kTriangles = 22000; // 66000 vertices, past the 65535 a 16-bit index reaches

	const TempDir t;
	ASSERT_NE(loadGround(t, "<surface>Grass</surface>", trianglesScript(kTriangles)), nullptr);
	auto& reg = AssetRegistry::Get();

	std::vector<renderer::TessellatedMesh> meshes;
	ASSERT_TRUE(reg.buildMeshes("Ground_Tile", 0, meshes));
	ASSERT_EQ(meshes.size(), 2U);

	size_t vertexTotal = 0;
	for (const auto& mesh : meshes) {
		EXPECT_LE(mesh.vertices.size(), 65535U);
		EXPECT_EQ(mesh.colors.size(), mesh.vertices.size());
		ASSERT_FALSE(mesh.indices.empty());
		EXPECT_EQ(mesh.indices.size() % 3, 0U);
		EXPECT_LT(*std::max_element(mesh.indices.begin(), mesh.indices.end()), mesh.vertices.size()) << "an index must point into its own mesh";
		vertexTotal += mesh.vertices.size();
	}
	EXPECT_EQ(vertexTotal, static_cast<size_t>(kTriangles) * 3U) << "no path may be lost in the split";

	// Paths stay in order and none straddles the break: path i lives at x in [i, i + 0.5].
	const auto maxX = [](const renderer::TessellatedMesh& m) {
		return std::max_element(m.vertices.begin(), m.vertices.end(), [](const auto& a, const auto& b) { return a.x < b.x; })->x;
	};
	const auto minX = [](const renderer::TessellatedMesh& m) {
		return std::min_element(m.vertices.begin(), m.vertices.end(), [](const auto& a, const auto& b) { return a.x < b.x; })->x;
	};
	EXPECT_FLOAT_EQ(minX(meshes[0]), 0.0F);
	EXPECT_FLOAT_EQ(maxX(meshes[0]) + 0.5F, minX(meshes[1]) + 0.0F) << "the second mesh starts at the next whole path";
	EXPECT_FLOAT_EQ(maxX(meshes[1]), static_cast<float>(kTriangles - 1) + 0.5F);

	renderer::TessellatedMesh single;
	EXPECT_FALSE(reg.buildMesh("Ground_Tile", 0, single)) << "buildMesh draws one mesh, so an asset that needs two fails instead of wrapping";
	EXPECT_TRUE(single.vertices.empty());
	reg.clearDefinitions();
}

TEST(GroundMeshTest, SmallAssetIsOneMeshThroughEitherCall) {
	const TempDir t;
	ASSERT_NE(loadGround(t, "<surface>Grass</surface>", trianglesScript(40)), nullptr);
	auto& reg = AssetRegistry::Get();

	std::vector<renderer::TessellatedMesh> meshes;
	ASSERT_TRUE(reg.buildMeshes("Ground_Tile", 0, meshes));
	ASSERT_EQ(meshes.size(), 1U);

	renderer::TessellatedMesh single;
	ASSERT_TRUE(reg.buildMesh("Ground_Tile", 0, single));
	EXPECT_EQ(single.vertices.size(), meshes[0].vertices.size());
	EXPECT_EQ(single.indices, meshes[0].indices);
	reg.clearDefinitions();
}

TEST(GroundMeshTest, PixelDensityDropsPathsThatNeedMore) {
	const TempDir t;
	ASSERT_NE(
		loadGround(
			t,
			"<surface>Grass</surface>",
			"local base, fine = Path(), Path()\n"
			"for _, p in ipairs({base, fine}) do\n"
			"    p:addVertex(0, 0)\n    p:addVertex(1, 0)\n    p:addVertex(0, 1)\n    p:close()\n"
			"end\n"
			"fine:setLod(24)\n"
			"asset:addPath(base)\nasset:addPath(fine)\n"
		),
		nullptr
	);
	auto& reg = AssetRegistry::Get();

	const auto vertexCountAt = [&reg](float pixelsPerMeter) {
		std::vector<renderer::TessellatedMesh> meshes;
		EXPECT_TRUE(reg.buildMeshes("Ground_Tile", 0, meshes, pixelsPerMeter));
		return meshes.empty() ? size_t{0} : meshes[0].vertices.size();
	};
	EXPECT_EQ(vertexCountAt(6.0F), 3U) << "below the threshold only the always-drawn path remains";
	EXPECT_EQ(vertexCountAt(24.0F), 6U) << "at the threshold the fine path is back";
	EXPECT_EQ(vertexCountAt(AssetRegistry::kFullDetail), 6U);
	reg.clearDefinitions();
}

TEST(GroundMeshTest, PathsBuiltOnceMatchARunPerDensity) {
	// 23,000 random triangles, 69,000 vertices at full detail so the packing splits, tagged 0, 8, 24 and
	// 64 in turn, all inside the tile so the wrap adds nothing and the reference below needs none.
	const TempDir t;
	ASSERT_NE(
		loadGround(
			t,
			"<surface>Grass</surface><period>4</period>",
			"local lods = {0, 8, 24, 64}\n"
			"for i = 0, 22999 do\n"
			"    local x, y = 0.3 + math.random() * 3.2, 0.3 + math.random() * 3.2\n"
			"    local p = Path()\n"
			"    p:addVertex(x, y)\n    p:addVertex(x + math.random() * 0.2, y)\n    p:addVertex(x, y + 0.05)\n"
			"    p:setColor(math.random(), math.random(), 0.3, 1.0)\n"
			"    p:setLod(lods[i % 4 + 1])\n"
			"    p:close()\n"
			"    asset:addPath(p)\n"
			"end\n"
		),
		nullptr
	);
	auto& reg = AssetRegistry::Get();
	constexpr uint32_t kSeed = 77;

	TessellatedPaths once;
	ASSERT_TRUE(reg.buildPaths("Ground_Tile", kSeed, once));
	GeneratedAsset generated;
	ASSERT_TRUE(reg.generateAsset("Ground_Tile", kSeed, generated));

	for (const float density : {AssetRegistry::kFullDetail, 24.0F, 2.0F}) {
		// The per-mip path as it was: every kept path tessellated at this density, packed in order.
		std::vector<renderer::TessellatedMesh> reference;
		renderer::Tessellator				   tessellator;
		for (const GeneratedPath& path : generated.paths) {
			renderer::TessellatedMesh pathMesh;
			if (path.minPixelsPerMeter > density || !tessellator.Tessellate(renderer::VectorPath(path.vertices, path.isClosed), pathMesh)) {
				continue;
			}
			if (reference.empty() || reference.back().vertices.size() + pathMesh.vertices.size() > 65535U) {
				reference.emplace_back();
			}
			renderer::TessellatedMesh& mesh = reference.back();
			const auto				   base = static_cast<uint16_t>(mesh.vertices.size());
			for (const auto& v : pathMesh.vertices) {
				mesh.vertices.push_back(v);
				mesh.colors.push_back(path.fillColor);
			}
			for (const uint16_t i : pathMesh.indices) {
				mesh.indices.push_back(static_cast<uint16_t>(base + i));
			}
		}

		std::vector<renderer::TessellatedMesh> fromOnce;
		std::vector<renderer::TessellatedMesh> perDensity;
		ASSERT_TRUE(once.meshesAt(density, fromOnce)) << density;
		ASSERT_TRUE(reg.buildMeshes("Ground_Tile", kSeed, perDensity, density)) << density;
		ASSERT_EQ(fromOnce.size(), reference.size()) << density;
		ASSERT_EQ(perDensity.size(), reference.size()) << density;
		for (size_t m = 0; m < reference.size(); ++m) {
			for (const auto* built : {&fromOnce[m], &perDensity[m]}) {
				ASSERT_EQ(built->vertices.size(), reference[m].vertices.size()) << density << " mesh " << m;
				EXPECT_EQ(built->indices, reference[m].indices) << density << " mesh " << m;
				EXPECT_EQ(0, std::memcmp(built->vertices.data(), reference[m].vertices.data(), reference[m].vertices.size() * sizeof(Foundation::Vec2)));
				EXPECT_EQ(0, std::memcmp(built->colors.data(), reference[m].colors.data(), reference[m].colors.size() * sizeof(Foundation::Color)));
			}
		}
	}
	EXPECT_GT(once.vertices.size(), 65535U) << "full detail has to split into meshes for the packing to be tested";
	reg.clearDefinitions();
}

TEST(GroundMeshTest, PathsCrossingTheTileEdgeWrapRightAfterTheOriginal) {
	const TempDir t;
	ASSERT_NE(
		loadGround(
			t,
			"<surface>Grass</surface><period>4</period>",
			"local function tri(x, y)\n"
			"    local p = Path()\n"
			"    p:addVertex(x, y)\n    p:addVertex(x + 0.5, y)\n    p:addVertex(x, y + 0.5)\n    p:close()\n"
			"    asset:addPath(p)\n"
			"end\n"
			"tri(3.75, 1)\n"
			"tri(1, 1)\n"
			"tri(-0.25, -0.25)\n"
		),
		nullptr
	);
	std::vector<renderer::TessellatedMesh> meshes;
	ASSERT_TRUE(AssetRegistry::Get().buildMeshes("Ground_Tile", 0, meshes));
	ASSERT_EQ(meshes.size(), 1U);
	const auto& v = meshes[0].vertices;

	// Each path is one triangle, three vertices in path order; its lowest corner names it.
	std::vector<std::pair<float, float>> corners;
	for (size_t i = 0; i + 2 < v.size(); i += 3) {
		corners.emplace_back(std::min({v[i].x, v[i + 1].x, v[i + 2].x}), std::min({v[i].y, v[i + 1].y, v[i + 2].y}));
	}
	const std::vector<std::pair<float, float>> expected{
		{3.75F, 1.0F},	  // crosses the right edge
		{-0.25F, 1.0F},	  // its copy, before the next path
		{1.0F, 1.0F},	  // inside: no copy
		{-0.25F, -0.25F}, // crosses the top-left corner
		{-0.25F, 3.75F},
		{3.75F, -0.25F},
		{3.75F, 3.75F},
	};
	EXPECT_EQ(corners, expected);
	AssetRegistry::Get().clearDefinitions();
}

TEST(GroundMeshTest, UnsetHeightsTakeTheirPaintOrderRank) {
	// Four paths, the second with a height of its own, the third crossing the right edge.
	const TempDir t;
	ASSERT_NE(
		loadGround(
			t,
			"<surface>Grass</surface><period>4</period>",
			"local function tri(x, y)\n"
			"    local p = Path()\n"
			"    p:addVertex(x, y)\n    p:addVertex(x + 0.5, y)\n    p:addVertex(x, y + 0.5)\n    p:setColor(0.3, 0.6, 0.2, 0.5)\n"
			"    p:close()\n"
			"    return p\n"
			"end\n"
			"asset:addPath(tri(1, 1))\n"
			"local set = tri(2, 1)\n"
			"set:setHeight(0.1)\n"
			"asset:addPath(set)\n"
			"asset:addPath(tri(3.75, 2))\n"
			"asset:addPath(tri(1, 3))\n"
		),
		nullptr
	);
	TessellatedPaths paths;
	ASSERT_TRUE(AssetRegistry::Get().buildPaths("Ground_Tile", 0, paths));
	ASSERT_EQ(paths.paths.size(), 5U) << "the edge-crossing path and its copy";
	const std::vector<float> heights{0.25F, 0.1F, 0.75F, 0.75F, 1.0F};
	for (size_t i = 0; i < heights.size(); ++i) {
		EXPECT_FLOAT_EQ(paths.paths[i].height, heights[i]) << "path " << i << ": its rank among the generator's 4, the copy its original's";
	}

	std::vector<renderer::TessellatedMesh> meshes;
	ASSERT_TRUE(paths.heightMeshesAt(AssetRegistry::kFullDetail, meshes));
	ASSERT_EQ(meshes.size(), 1U);
	ASSERT_EQ(meshes[0].colors.size(), 15U);
	const auto expectGray = [](const Foundation::Color& c, float height) {
		EXPECT_FLOAT_EQ(c.r, height);
		EXPECT_FLOAT_EQ(c.g, height);
		EXPECT_FLOAT_EQ(c.b, height);
		EXPECT_FLOAT_EQ(c.a, 0.5F) << "its fill's alpha kept";
	};
	expectGray(meshes[0].colors[3], 0.1F);
	expectGray(meshes[0].colors[14], 1.0F);
	AssetRegistry::Get().clearDefinitions();
}

TEST(GroundAssetTest, SeedGroupDefaultsToTheDefName) {
	const TempDir t;
	EXPECT_EQ(loadGround(t, "<surface>Grass</surface>")->ground.seedGroup, "Ground_Tile");
	EXPECT_EQ(loadGround(t, "<surface>Grass</surface><seedGroup>grass</seedGroup>")->ground.seedGroup, "grass");
	AssetRegistry::Get().clearDefinitions();
}

TEST(GroundAssetTest, TransitionsStartFromTheDefaultBlockAndOverridesKeepWhatTheyLeaveOut) {
	const TempDir	   t;
	const auto		   at = [](const AssetDefinition* def, world::Surface lower) { return def->ground.transitions[static_cast<size_t>(lower)]; };
	const GroundTransition schema;

	const AssetDefinition* bare = loadGround(t, "<surface>Rock</surface>");
	ASSERT_NE(bare, nullptr);
	EXPECT_EQ(at(bare, world::Surface::Sand), schema) << "no <transition>: every pair takes the schema's defaults";
	EXPECT_FLOAT_EQ(schema.widthM, 0.4F);
	EXPECT_FLOAT_EQ(schema.softness, 0.1F);
	EXPECT_FLOAT_EQ(schema.breakup, 0.5F);

	const AssetDefinition* def = loadGround(
		t,
		"<surface>Rock</surface>"
		"<transition><width>0.5</width><softness>0.05</softness><shadow>0.25</shadow></transition>"
		"<transition over=\"Sand\"><width>0.7</width><offset>-0.2</offset><ridge>0.06</ridge></transition>"
	);
	ASSERT_NE(def, nullptr);
	const GroundTransition grass = at(def, world::Surface::Grass);
	EXPECT_FLOAT_EQ(grass.widthM, 0.5F);
	EXPECT_FLOAT_EQ(grass.offsetM, 0.0F);
	EXPECT_FLOAT_EQ(grass.softness, 0.05F);
	EXPECT_FLOAT_EQ(grass.breakup, 0.5F) << "a field the default block leaves out keeps the schema's";
	EXPECT_FLOAT_EQ(grass.shadow, 0.25F);

	const GroundTransition sand = at(def, world::Surface::Sand);
	EXPECT_FLOAT_EQ(sand.widthM, 0.7F);
	EXPECT_FLOAT_EQ(sand.offsetM, -0.2F);
	EXPECT_FLOAT_EQ(sand.ridge, 0.06F);
	EXPECT_FLOAT_EQ(sand.softness, 0.05F) << "an override's left-out fields keep the default block's";
	EXPECT_FLOAT_EQ(sand.shadow, 0.25F);
	AssetRegistry::Get().clearDefinitions();
}

TEST(GroundAssetTest, ABrokenTransitionBlockIsDroppedAndItsPairKeepsTheDefault) {
	const TempDir		   t;
	const AssetDefinition* def = loadGround(
		t,
		"<surface>Snow</surface>"
		"<transition><width>1.2</width><offset>0.2</offset><tint>0.25</tint></transition>"
		"<transition over=\"Rock\"><width>1.4</width><offset>0.2</offset></transition>" // past the 1.5 m reach
		"<transition over=\"Mud\"><width>0.25</width><offset>0</offset><stain>lots</stain></transition>"
	);
	ASSERT_NE(def, nullptr);
	for (const world::Surface lower : {world::Surface::Rock, world::Surface::Mud}) {
		const GroundTransition& pair = def->ground.transitions[static_cast<size_t>(lower)];
		EXPECT_FLOAT_EQ(pair.widthM, 1.2F) << world::surfaceToString(lower);
		EXPECT_FLOAT_EQ(pair.tint, 0.25F) << world::surfaceToString(lower);
		EXPECT_FLOAT_EQ(pair.stain, 0.0F) << world::surfaceToString(lower);
	}
	AssetRegistry::Get().clearDefinitions();
}

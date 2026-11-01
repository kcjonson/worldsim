// The ground tile baker end to end on a small def: the mip chain's shape, the registry's wrap across the tile edge, a setLod path
// leaving the coarse mips, and tiled renders matching one big one. Needs a GL 3.3 context; skips on a
// headless box, like the other GL tests. The filters have their own tests (GroundTileFilter.test.cpp).

#include "world/rendering/GroundTileBaker.h"

#include "BaseShader.h"

#include <assets/AssetRegistry.h>
#include <backend/gl/GLBackend.h>
#include <primitives/Primitives.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using namespace worldview;

namespace {

	namespace fs = std::filesystem;

	struct GlFixture {
		GLFWwindow* window = nullptr;
		bool		ok = false;

		GlFixture() {
			if (glfwInit() != GLFW_TRUE) {
				return;
			}
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
			glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
			glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
			window = glfwCreateWindow(64, 64, "ground-bake-test", nullptr, nullptr);
			if (window == nullptr) {
				glfwTerminate();
				return;
			}
			glfwMakeContextCurrent(window);
			std::unique_ptr<Renderer::backend::gl::GLBackend> backend = Renderer::backend::gl::GLBackend::create();
			if (backend == nullptr) {
				glfwDestroyWindow(window);
				window = nullptr;
				glfwTerminate();
				return;
			}
			Renderer::Primitives::init(std::move(backend), base_shader::source());
			ok = true;
		}

		~GlFixture() {
			if (ok) {
				Renderer::Primitives::shutdown();
			}
			if (window != nullptr) {
				glfwDestroyWindow(window);
				glfwTerminate();
			}
		}

		GlFixture(const GlFixture&) = delete;
		GlFixture& operator=(const GlFixture&) = delete;
	};

	struct TempDir {
		fs::path root;

		TempDir() {
			static std::atomic<int> counter{0};
			root = fs::temp_directory_path() / ("ground_bake_" + std::to_string(counter++) + "_" + std::to_string(std::rand()));
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
			std::ofstream(p) << content;
		}
	};

	// A 1 m tile at 32 px/m (32 texel layers, mips 32 to 1): a dark base, a light square crossing the
	// right edge at mid height, and a red square in the middle tagged setLod(20), so it is in mip 0
	// (32 texels a meter) and not mip 1 (16).
	constexpr const char* kScript = R"(
local function rect(x0, y0, x1, y1, r, g, b)
    local p = Path()
    p:addVertex(x0, y0)
    p:addVertex(x1, y0)
    p:addVertex(x1, y1)
    p:addVertex(x0, y1)
    p:setColor(r, g, b, 1.0)
    p:close()
    return p
end
asset:addPath(rect(0, 0, 1, 1, 0.1, 0.1, 0.1))
asset:addPath(rect(0.875, 0.375, 1.125, 0.625, 0.9, 0.9, 0.9))
local fine = rect(0.4375, 0.4375, 0.5625, 0.5625, 0.9, 0.1, 0.1)
fine:setLod(20)
asset:addPath(fine)
)";

	void loadTestDef(const TempDir& t) {
		t.write(
			"Tile/Tile.xml",
			"<?xml version=\"1.0\"?><AssetDefinitions><AssetDef><defName>Ground_BakeTest</defName><role>ground</role>"
			"<assetType>procedural</assetType><generator><scriptPath>gen.lua</scriptPath></generator>"
			"<surface>Snow</surface><period>1</period><pixelsPerMeter>32</pixelsPerMeter><variantCount>2</variantCount>"
			"</AssetDef></AssetDefinitions>"
		);
		t.write("Tile/gen.lua", kScript);
		auto& registry = assets::AssetRegistry::Get();
		registry.clearDefinitions();
		registry.loadDefinitionsFromFolder(t.root.string());
	}

	// One channel of a texel of a mip, decoded to linear light.
	float linearAt(const GroundMip& mip, int x, int y, int channel) {
		return ground::srgbToLinear(static_cast<float>(mip.rgba[((static_cast<size_t>(y) * mip.size) + x) * 4 + channel]) / 255.0F);
	}

} // namespace

TEST(GroundTileBakerTest, SeedsAreSaltedByDefAndVariant) {
	EXPECT_NE(GroundTileBaker::variantSeed("Ground_Grass", 0), GroundTileBaker::variantSeed("Ground_Grass", 1));
	EXPECT_NE(GroundTileBaker::variantSeed("Ground_Grass", 0), GroundTileBaker::variantSeed("Ground_Snow", 0));
	EXPECT_EQ(GroundTileBaker::variantSeed("Ground_Grass", 3), GroundTileBaker::variantSeed("Ground_Grass", 3));
	EXPECT_EQ(GroundTileBaker::mipCount(512), 7) << "capped at mip 6";
	EXPECT_EQ(GroundTileBaker::mipCount(32), 6) << "32 down to 1";
	EXPECT_EQ(GroundTileBaker::supersampleFor(1), 8);
	EXPECT_EQ(GroundTileBaker::supersampleFor(2), 4);
}

TEST(GroundTileBakerTest, BakesAWrappedChainAndDropsLodPaths) {
	GlFixture gl;
	if (!gl.ok) {
		GTEST_SKIP() << "No GL context (headless CI)";
	}
	const TempDir t;
	loadTestDef(t);

	const GroundTileBaker				baker({});
	const std::optional<GroundTileBake> bake = baker.bakeDef("Ground_BakeTest");
	ASSERT_TRUE(bake.has_value());
	ASSERT_EQ(bake->variants.size(), 2U);
	const GroundVariantBake& v = bake->variants[0];
	ASSERT_EQ(v.mips.size(), 6U);
	for (size_t k = 0; k < v.mips.size(); ++k) {
		EXPECT_EQ(v.mips[k].size, 32 >> k);
		EXPECT_EQ(v.mips[k].rgba.size(), static_cast<size_t>(v.mips[k].size) * v.mips[k].size * 4);
		EXPECT_FLOAT_EQ(v.samples[k].minAlpha, 1.0F) << "the base covers the period at mip " << k;
	}
	EXPECT_EQ(v.samples[0].renderSize, 256);
	EXPECT_EQ(v.samples[2].renderSize, 32);

	const GroundMip& mip0 = v.mips[0];
	const float		 light = ground::srgbToLinear(0.9F);
	const float		 dark = ground::srgbToLinear(0.1F);
	EXPECT_NEAR(linearAt(mip0, 30, 16, 1), light, 0.01F) << "the light square inside the right edge";
	EXPECT_NEAR(linearAt(mip0, 1, 16, 1), light, 0.01F) << "and the part past it, wrapped in at the left";
	EXPECT_NEAR(linearAt(mip0, 1, 3, 1), dark, 0.005F) << "the base above it";
	EXPECT_NEAR(linearAt(mip0, 16, 16, 0), light, 0.01F) << "the setLod(20) square is in mip 0";

	const GroundMip& mip1 = v.mips[1];
	EXPECT_LT(linearAt(mip1, 8, 8, 0), 0.5F * linearAt(mip0, 16, 16, 0)) << "and gone from mip 1, but for the mean lock's faint tint";
	EXPECT_GT(linearAt(mip1, 8, 8, 0), linearAt(mip1, 8, 2, 0)) << "the tint keeps its light where it was";
	EXPECT_LT(bake->metrics[1].meanErrorPercent, 0.5F) << "the lock holds mip 1's mean to mip 0's";
	assets::AssetRegistry::Get().clearDefinitions();
}

TEST(GroundTileBakerTest, TiledRenderMatchesOneTarget) {
	GlFixture gl;
	if (!gl.ok) {
		GTEST_SKIP() << "No GL context (headless CI)";
	}
	const TempDir t;
	loadTestDef(t);

	const std::optional<GroundVariantBake> whole = GroundTileBaker({}).bakeVariant("Ground_BakeTest", 1);
	const std::optional<GroundVariantBake> tiled = GroundTileBaker({.maxRenderSize = 48}).bakeVariant("Ground_BakeTest", 1);
	ASSERT_TRUE(whole.has_value());
	ASSERT_TRUE(tiled.has_value());
	ASSERT_EQ(whole->mips.size(), tiled->mips.size());
	for (size_t k = 0; k < whole->mips.size(); ++k) {
		ASSERT_EQ(whole->mips[k].rgba.size(), tiled->mips[k].rgba.size());
		int worst = 0;
		for (size_t i = 0; i < whole->mips[k].rgba.size(); ++i) {
			worst = std::max(worst, std::abs(static_cast<int>(whole->mips[k].rgba[i]) - static_cast<int>(tiled->mips[k].rgba[i])));
		}
		EXPECT_LE(worst, 1) << "mip " << k << " (a 256 px render in 48 px tiles, the last ones partial)";
	}
	assets::AssetRegistry::Get().clearDefinitions();
}

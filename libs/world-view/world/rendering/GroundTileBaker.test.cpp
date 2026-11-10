// The ground tile baker end to end on a small def: the mip chain's shape, the registry's wrap across the tile edge, a setLod path
// leaving the coarse mips, heights in alpha by paint order or setHeight, tiled renders matching one big
// one, a bake on the pool matching one in turn, and the disk cache giving a bake back until the script
// changes. Needs a GL 3.3 context; skips on a
// headless box, like the other GL tests. The filters and the cache file have their own tests
// (GroundTileFilter.test.cpp, GroundTileCache.test.cpp).

#include "world/rendering/GroundTileBaker.h"

#include "BaseShader.h"
#include "world/rendering/GlTest.test.h"
#include "world/rendering/GroundTileBank.h"
#include "world/rendering/GroundTileCache.h"

#include <assets/AssetRegistry.h>
#include <assets/IAssetGenerator.h>
#include <backend/gl/GLBackend.h>
#include <primitives/Primitives.h>
#include <threading/TaskPool.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

using namespace worldview;

namespace {

	namespace fs = std::filesystem;

	struct GlFixture {
		GLFWwindow* window = nullptr;
		bool		ok = false;
		std::string failure;

		GlFixture() {
			failure = worldsim::test::startGlfw();
			if (!failure.empty()) {
				return;
			}
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
			glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
			glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
			window = glfwCreateWindow(64, 64, "ground-bake-test", nullptr, nullptr);
			if (window == nullptr) {
				failure = "glfwCreateWindow failed for a 3.3 core forward-compatible context: " + worldsim::test::glfwLastError();
				return;
			}
			glfwMakeContextCurrent(window);
			std::unique_ptr<Renderer::backend::gl::GLBackend> backend = Renderer::backend::gl::GLBackend::create();
			if (backend == nullptr) {
				failure = "GLBackend::create failed";
				glfwDestroyWindow(window);
				window = nullptr;
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

TEST(GroundTileBakerTest, SeedsAreSaltedBySeedGroupAndVariant) {
	EXPECT_NE(GroundTileBaker::variantSeed("grass", 0), GroundTileBaker::variantSeed("grass", 1));
	EXPECT_NE(GroundTileBaker::variantSeed("grass", 0), GroundTileBaker::variantSeed("Ground_Snow", 0));
	EXPECT_EQ(GroundTileBaker::variantSeed("grass", 3), GroundTileBaker::variantSeed("grass", 3)) << "one group, one layout a variant";
	EXPECT_EQ(GroundTileBaker::mipCount(512), 7) << "capped at mip 6";
	EXPECT_EQ(GroundTileBaker::mipCount(32), 6) << "32 down to 1";
	EXPECT_EQ(GroundTileBaker::supersampleFor(1), 8);
	EXPECT_EQ(GroundTileBaker::supersampleFor(2), 4);
}

TEST(GroundTileBakerTest, BakesAWrappedChainAndDropsLodPaths) {
	GlFixture gl;
	SKIP_OR_FAIL_WITHOUT_GL(gl);
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

	// No path set a height, so each takes its paint order: the base lowest, the light square (its copy
	// across the edge too), the setLod square on top.
	const auto heightAt = [&mip0](int x, int y) { return mip0.rgba[((static_cast<size_t>(y) * mip0.size) + x) * 4 + 3]; };
	EXPECT_LT(heightAt(1, 3), heightAt(30, 16));
	EXPECT_LT(heightAt(1, 3), heightAt(1, 16));
	EXPECT_LT(heightAt(30, 16), heightAt(16, 16));
	assets::AssetRegistry::Get().clearDefinitions();
}

TEST(GroundTileBakerTest, HeightsRankByPaintOrderUnlessSet) {
	GlFixture gl;
	SKIP_OR_FAIL_WITHOUT_GL(gl);
	// A base and two squares over it, the second set to height 0: below the base, though painted last.
	const TempDir t;
	t.write(
		"Tile/Tile.xml",
		"<?xml version=\"1.0\"?><AssetDefinitions><AssetDef><defName>Ground_BakeTest</defName><role>ground</role>"
		"<assetType>procedural</assetType><generator><scriptPath>gen.lua</scriptPath></generator>"
		"<surface>Snow</surface><period>1</period><pixelsPerMeter>32</pixelsPerMeter><variantCount>2</variantCount>"
		"</AssetDef></AssetDefinitions>"
	);
	t.write(
		"Tile/gen.lua",
		std::string(kScript).substr(0, std::string(kScript).find("asset:addPath")) +
			"asset:addPath(rect(0, 0, 1, 1, 0.5, 0.5, 0.5))\n"
			"asset:addPath(rect(0.125, 0.125, 0.375, 0.375, 0.5, 0.5, 0.5))\n"
			"local sunk = rect(0.625, 0.625, 0.875, 0.875, 0.5, 0.5, 0.5)\n"
			"sunk:setHeight(0)\n"
			"asset:addPath(sunk)\n"
	);
	auto& registry = assets::AssetRegistry::Get();
	registry.clearDefinitions();
	registry.loadDefinitionsFromFolder(t.root.string());

	const std::optional<GroundTileBake> bake = GroundTileBaker({}).bakeDef("Ground_BakeTest");
	ASSERT_TRUE(bake.has_value());
	const GroundVariantBake& v = bake->variants[0];
	const GroundMip&		 mip0 = v.mips[0];
	const auto heightAt = [&mip0](int x, int y) { return mip0.rgba[((static_cast<size_t>(y) * mip0.size) + x) * 4 + 3]; };
	EXPECT_LT(heightAt(24, 24), heightAt(16, 2)) << "the set square under the base";
	EXPECT_LT(heightAt(16, 2), heightAt(8, 8)) << "the base under the square painted over it";
	EXPECT_GT(v.samples[0].heightSpread, 0.1F) << "ranked heights spread across 0 to 1";

	// Down the chain the heights are filtered, so each mip keeps mip 0's mean height.
	const auto meanHeight = [](const GroundMip& mip) {
		double sum = 0.0;
		for (size_t i = 3; i < mip.rgba.size(); i += 4) {
			sum += mip.rgba[i];
		}
		return sum / (static_cast<double>(mip.size) * mip.size * 255.0);
	};
	for (size_t k = 1; k < v.mips.size(); ++k) {
		EXPECT_NEAR(meanHeight(v.mips[k]), meanHeight(mip0), 1.0 / 255.0) << "mip " << k;
	}
	registry.clearDefinitions();
}

TEST(GroundTileBakerTest, TiledRenderMatchesOneTarget) {
	GlFixture gl;
	SKIP_OR_FAIL_WITHOUT_GL(gl);
	const TempDir t;
	loadTestDef(t);

	const GroundTileBaker							baker({});
	std::optional<GroundTileBake>					bake = baker.begin("Ground_BakeTest");
	ASSERT_TRUE(bake.has_value());
	std::optional<std::vector<GroundVariantPaths>> paths = baker.buildPaths(*bake);
	ASSERT_TRUE(paths.has_value());
	ASSERT_TRUE(baker.bakeHeights(*bake, *paths));
	const std::optional<GroundVariantBake> whole = baker.bakeVariant(*bake, (*paths)[1]);
	const std::optional<GroundVariantBake> tiled = GroundTileBaker({.maxRenderSize = 48}).bakeVariant(*bake, (*paths)[1]);
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

TEST(GroundTileBakerTest, ParallelBakeMatchesSerialAndTheCacheGivesItBack) {
	GlFixture gl;
	SKIP_OR_FAIL_WITHOUT_GL(gl);
	// Four variants that differ: each seed scatters its own squares, some across the edge.
	const TempDir t;
	t.write(
		"Tile/Tile.xml",
		"<?xml version=\"1.0\"?><AssetDefinitions><AssetDef><defName>Ground_BakeTest</defName><role>ground</role>"
		"<assetType>procedural</assetType><generator><scriptPath>gen.lua</scriptPath></generator>"
		"<surface>Snow</surface><period>1</period><pixelsPerMeter>32</pixelsPerMeter><variantCount>4</variantCount>"
		"</AssetDef></AssetDefinitions>"
	);
	const std::string script = std::string(kScript) +
							   "for i = 1, 40 do\n"
							   "    local x, y = math.random(), math.random()\n"
							   "    local s = rect(x, y, x + 0.1, y + 0.1, math.random(), math.random(), math.random())\n"
							   "    s:setLod(math.random(0, 40))\n"
							   "    asset:addPath(s)\n"
							   "end\n";
	t.write("Tile/gen.lua", script);
	auto& registry = assets::AssetRegistry::Get();
	registry.clearDefinitions();
	registry.loadDefinitionsFromFolder(t.root.string());

	const std::optional<GroundTileBake> serial = GroundTileBaker({}).bakeDef("Ground_BakeTest");
	foundation::TaskPool				pool(4);
	const GroundTileCache				cache(t.root / "cache");
	const GroundTileBaker				cached({.pool = &pool, .cache = &cache});
	const std::optional<GroundTileBake> parallel = cached.bakeDef("Ground_BakeTest");
	ASSERT_TRUE(serial.has_value());
	ASSERT_TRUE(parallel.has_value());
	EXPECT_FALSE(parallel->fromCache);
	ASSERT_EQ(serial->variants.size(), 4U);
	ASSERT_EQ(parallel->variants.size(), 4U);
	EXPECT_NE(serial->variants[0].mips[0].rgba, serial->variants[1].mips[0].rgba) << "the variants must differ for the match to mean anything";

	const auto expectSameMips = [](const GroundTileBake& a, const GroundTileBake& b, const char* what) {
		for (size_t v = 0; v < a.variants.size(); ++v) {
			ASSERT_EQ(a.variants[v].mips.size(), b.variants[v].mips.size()) << what;
			for (size_t k = 0; k < a.variants[v].mips.size(); ++k) {
				EXPECT_EQ(a.variants[v].mips[k].rgba, b.variants[v].mips[k].rgba) << what << ", variant " << v << " mip " << k;
			}
		}
	};
	expectSameMips(*serial, *parallel, "parallel against serial");

	const std::optional<GroundTileBake> again = cached.bakeDef("Ground_BakeTest");
	ASSERT_TRUE(again.has_value());
	EXPECT_TRUE(again->fromCache);
	expectSameMips(*parallel, *again, "the cache against the bake it stored");
	for (size_t k = 0; k < parallel->metrics.size(); ++k) {
		EXPECT_EQ(parallel->metrics[k].contrastRatioMin, again->metrics[k].contrastRatioMin) << "the metrics come back too, mip " << k;
		EXPECT_EQ(parallel->metrics[k].vertexCount, again->metrics[k].vertexCount);
	}

	t.write("Tile/gen.lua", script + "-- an edit\n");
	const std::optional<GroundTileBake> edited = cached.bakeDef("Ground_BakeTest");
	ASSERT_TRUE(edited.has_value());
	EXPECT_FALSE(edited->fromCache) << "editing the script bakes again";
	const std::optional<GroundTileBake> afterEdit = cached.bakeDef("Ground_BakeTest");
	ASSERT_TRUE(afterEdit.has_value());
	EXPECT_TRUE(afterEdit->fromCache) << "and stores the new bake";
	registry.clearDefinitions();
}

namespace {

	// A Snow def at 512 m x 16 px/m: a valid period, but an 8192 texel layer, past the 1024 the baker takes.
	void loadOversizedDef(const TempDir& t) {
		t.write(
			"Big/Big.xml",
			"<?xml version=\"1.0\"?><AssetDefinitions><AssetDef><defName>Ground_Oversized</defName><role>ground</role>"
			"<assetType>procedural</assetType><generator><scriptPath>gen.lua</scriptPath></generator>"
			"<surface>Snow</surface><period>512</period><pixelsPerMeter>16</pixelsPerMeter>"
			"</AssetDef></AssetDefinitions>"
		);
		t.write("Big/gen.lua", "");
		auto& registry = assets::AssetRegistry::Get();
		registry.clearDefinitions();
		registry.loadDefinitionsFromFolder(t.root.string());
	}

} // namespace

TEST(GroundTileBakerTest, AnOversizedLayerIsRefusedBeforeAnythingIsBuilt) {
	const TempDir t;
	loadOversizedDef(t);
	const assets::AssetDefinition* def = assets::AssetRegistry::Get().getDefinition("Ground_Oversized");
	ASSERT_NE(def, nullptr);

	EXPECT_EQ(GroundTileBaker::layerSize(*def), 0);
	EXPECT_FALSE(GroundTileBaker({}).begin("Ground_Oversized").has_value());
	assets::AssetRegistry::Get().clearDefinitions();
}

TEST(GroundTileBankTest, AnOversizedLayerAllocatesNoArray) {
	GlFixture gl;
	SKIP_OR_FAIL_WITHOUT_GL(gl);
	const TempDir t;
	loadOversizedDef(t);

	GroundTileBank bank({});
	bank.begin();
	EXPECT_TRUE(bank.arrays().empty());
	EXPECT_EQ(bank.bytes(), 0U);
	EXPECT_EQ(bank.totalVariants(), 0);
	bool named = false;
	for (const std::string& line : bank.errors()) {
		named = named || line.find("Ground_Oversized") != std::string::npos;
	}
	EXPECT_TRUE(named) << "the def is named in the bank's errors";
	assets::AssetRegistry::Get().clearDefinitions();
}

TEST(GroundTileBakerTest, AVariantCountPastTheValidatorsIsRefusedBeforeAnythingIsGenerated) {
	// The asset-manager's Bake view could reach begin with a def that failed validation; 500 variants would
	// run 500 generators at once.
	for (const char* count : {"0", "33", "500"}) {
		const TempDir t;
		t.write(
			"Tile/Tile.xml",
			std::string(
				"<?xml version=\"1.0\"?><AssetDefinitions><AssetDef><defName>Ground_Many</defName><role>ground</role>"
				"<assetType>procedural</assetType><generator><scriptPath>gen.lua</scriptPath></generator>"
				"<surface>Snow</surface><period>1</period><pixelsPerMeter>32</pixelsPerMeter><variantCount>"
			) + count +
				"</variantCount></AssetDef></AssetDefinitions>"
		);
		t.write("Tile/gen.lua", "error('the generator must not run')\n");
		auto& registry = assets::AssetRegistry::Get();
		registry.clearDefinitions();
		registry.loadDefinitionsFromFolder(t.root.string());
		EXPECT_FALSE(GroundTileBaker({}).begin("Ground_Many").has_value()) << count;
		registry.clearDefinitions();
	}
}

namespace {

	class ThrowingGenerator : public assets::IAssetGenerator {
	  public:
		bool generate(
			const assets::GenerationContext& /*ctx*/,
			const assets::GeneratorParams& /*params*/,
			assets::GeneratedAsset& /*out*/
		) override {
			throw std::runtime_error("the generator blew up");
		}
		[[nodiscard]] assets::AssetComplexity getComplexity() const override { return assets::AssetComplexity::Simple; }
		[[nodiscard]] assets::AnimationType	  getAnimationType() const override { return assets::AnimationType::None; }
		[[nodiscard]] const char*			  getName() const override { return "GroundBankThrowTest"; }
	};

} // namespace

TEST(GroundTileBankTest, AThrowingStepFailsItsDefWithTheMessageAndTheBakeMovesOn) {
	GlFixture gl;
	SKIP_OR_FAIL_WITHOUT_GL(gl);
	assets::GeneratorRegistry::Get().registerGenerator("GroundBankThrowTest", [] { return std::make_unique<ThrowingGenerator>(); });
	const TempDir t;
	t.write(
		"Tile/Tile.xml",
		"<?xml version=\"1.0\"?><AssetDefinitions><AssetDef><defName>Ground_Throws</defName><role>ground</role>"
		"<assetType>procedural</assetType><generator><name>GroundBankThrowTest</name></generator>"
		"<surface>Snow</surface><period>1</period><pixelsPerMeter>32</pixelsPerMeter><variantCount>2</variantCount>"
		"</AssetDef></AssetDefinitions>"
	);
	auto& registry = assets::AssetRegistry::Get();
	registry.clearDefinitions();
	registry.loadDefinitionsFromFolder(t.root.string());

	GroundTileBank bank({});
	int			   steps = 0;
	while (bank.bakeNext()) {
		ASSERT_LT(++steps, 8) << "a step that threw must not run again";
	}
	EXPECT_TRUE(bank.ready());
	bool named = false;
	for (const std::string& line : bank.errors()) {
		named = named || (line.find("Ground_Throws") != std::string::npos && line.find("the generator blew up") != std::string::npos);
	}
	EXPECT_TRUE(named) << "the def and what it threw are in the bank's errors, for the splash to show";
	registry.clearDefinitions();
}

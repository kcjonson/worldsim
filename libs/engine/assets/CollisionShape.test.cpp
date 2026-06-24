// Tests for CollisionShape parsing in AssetRegistry.

#include "assets/AssetDefinition.h"
#include "assets/AssetRegistry.h"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

using namespace engine::assets;

namespace {

	namespace fs = std::filesystem;

	struct TempAssets {
		fs::path root;

		TempAssets() {
			static std::atomic<int> counter{0};
			const std::string		token =
				std::to_string(reinterpret_cast<uintptr_t>(&counter)) + "_" + std::to_string(counter++);
			root = fs::temp_directory_path() / ("collision_test_" + token);
			fs::create_directories(root);
		}

		~TempAssets() {
			std::error_code ec;
			fs::remove_all(root, ec);
		}

		TempAssets(const TempAssets&)			 = delete;
		TempAssets& operator=(const TempAssets&) = delete;

		// Write <root>/<folder>/<folder>.xml so the registry's primary-file filter accepts it.
		void writeXml(const std::string& folder, const std::string& body) {
			const fs::path dir = root / folder;
			fs::create_directories(dir);
			std::ofstream(dir / (folder + ".xml")) << body;
		}
	};

	std::string wrapDef(const std::string& inner) {
		return "<?xml version=\"1.0\"?><AssetDefinitions><AssetDef>"
			   "<defName>TestAsset</defName>"
			   "<assetType>procedural</assetType>"
			   "<generator><name>Stub</name></generator>"
			   + inner
			   + "</AssetDef></AssetDefinitions>";
	}

} // namespace

TEST(CollisionShapeTest, AbsentCollisionIsNone) {
	auto&	   reg = AssetRegistry::Get();
	TempAssets t;
	t.writeXml("Absent", wrapDef(""));

	reg.clearDefinitions();
	reg.loadDefinitionsFromFolder(t.root.string());
	const AssetDefinition* def = reg.getDefinition("TestAsset");
	ASSERT_NE(def, nullptr);

	EXPECT_EQ(def->collision.type, CollisionShapeType::None);
	EXPECT_FALSE(def->collision.blocks());
}

TEST(CollisionShapeTest, RectParsesMinMaxToCenterAndHalfExtents) {
	auto&	   reg = AssetRegistry::Get();
	TempAssets t;
	t.writeXml("Rect", wrapDef("<collision><rect minX=\"-0.2\" minY=\"0.1\" maxX=\"0.6\" maxY=\"0.5\"/></collision>"));

	reg.clearDefinitions();
	reg.loadDefinitionsFromFolder(t.root.string());
	const AssetDefinition* def = reg.getDefinition("TestAsset");
	ASSERT_NE(def, nullptr);

	EXPECT_EQ(def->collision.type, CollisionShapeType::Rect);
	// center = ((min+max)/2), halfExtents = ((max-min)/2)
	EXPECT_FLOAT_EQ(def->collision.offsetMeters.x, 0.2F);
	EXPECT_FLOAT_EQ(def->collision.offsetMeters.y, 0.3F);
	EXPECT_FLOAT_EQ(def->collision.halfExtentsMeters.x, 0.4F);
	EXPECT_FLOAT_EQ(def->collision.halfExtentsMeters.y, 0.2F);
	EXPECT_TRUE(def->collision.blocks());
}

TEST(CollisionShapeTest, RectInvertedOrDegenerateIsNone) {
	auto&	   reg = AssetRegistry::Get();
	TempAssets t;
	// maxX <= minX (inverted) and maxY <= minY (degenerate) -> ignored.
	t.writeXml("RectBad", wrapDef("<collision><rect minX=\"1.0\" minY=\"1.0\" maxX=\"0.0\" maxY=\"1.0\"/></collision>"));

	reg.clearDefinitions();
	reg.loadDefinitionsFromFolder(t.root.string());
	const AssetDefinition* def = reg.getDefinition("TestAsset");
	ASSERT_NE(def, nullptr);

	EXPECT_EQ(def->collision.type, CollisionShapeType::None);
	EXPECT_FALSE(def->collision.blocks());
}

TEST(CollisionShapeTest, RectCornersLocalAreCcw) {
	CollisionShape shape;
	shape.type			   = CollisionShapeType::Rect;
	shape.offsetMeters	   = {1.0F, 2.0F};
	shape.halfExtentsMeters = {0.5F, 0.25F};

	const std::array<glm::vec2, 4> corners = shape.rectCornersLocal();
	// CCW from bottom-left: (-hx,-hy), (+hx,-hy), (+hx,+hy), (-hx,+hy), about center.
	EXPECT_FLOAT_EQ(corners[0].x, 0.5F);
	EXPECT_FLOAT_EQ(corners[0].y, 1.75F);
	EXPECT_FLOAT_EQ(corners[1].x, 1.5F);
	EXPECT_FLOAT_EQ(corners[1].y, 1.75F);
	EXPECT_FLOAT_EQ(corners[2].x, 1.5F);
	EXPECT_FLOAT_EQ(corners[2].y, 2.25F);
	EXPECT_FLOAT_EQ(corners[3].x, 0.5F);
	EXPECT_FLOAT_EQ(corners[3].y, 2.25F);

	// Shoelace > 0 confirms CCW winding.
	float area2 = 0.0F;
	for (int i = 0; i < 4; ++i) {
		const glm::vec2& a = corners[i];
		const glm::vec2& b = corners[(i + 1) % 4];
		area2 += a.x * b.y - b.x * a.y;
	}
	EXPECT_GT(area2, 0.0F);
}

TEST(CollisionShapeTest, PolygonParsesPoints) {
	auto&	   reg = AssetRegistry::Get();
	TempAssets t;
	t.writeXml(
		"Polygon",
		wrapDef(
			"<collision><polygon>"
			"<point x=\"0\" y=\"0\"/>"
			"<point x=\"1\" y=\"0\"/>"
			"<point x=\"0.5\" y=\"1\"/>"
			"</polygon></collision>"
		)
	);

	reg.clearDefinitions();
	reg.loadDefinitionsFromFolder(t.root.string());
	const AssetDefinition* def = reg.getDefinition("TestAsset");
	ASSERT_NE(def, nullptr);

	EXPECT_EQ(def->collision.type, CollisionShapeType::Polygon);
	ASSERT_EQ(def->collision.pointsMeters.size(), 3U);
	EXPECT_FLOAT_EQ(def->collision.pointsMeters[0].x, 0.0F);
	EXPECT_FLOAT_EQ(def->collision.pointsMeters[1].x, 1.0F);
	EXPECT_FLOAT_EQ(def->collision.pointsMeters[2].x, 0.5F);
	EXPECT_FLOAT_EQ(def->collision.pointsMeters[2].y, 1.0F);
	EXPECT_TRUE(def->collision.blocks());
}

TEST(CollisionShapeTest, PolygonTooFewPointsIsNone) {
	auto&	   reg = AssetRegistry::Get();
	TempAssets t;
	t.writeXml(
		"TooFewPoints",
		wrapDef(
			"<collision><polygon>"
			"<point x=\"0\" y=\"0\"/>"
			"<point x=\"1\" y=\"0\"/>"
			"</polygon></collision>"
		)
	);

	reg.clearDefinitions();
	reg.loadDefinitionsFromFolder(t.root.string());
	const AssetDefinition* def = reg.getDefinition("TestAsset");
	ASSERT_NE(def, nullptr);

	EXPECT_EQ(def->collision.type, CollisionShapeType::None);
	EXPECT_FALSE(def->collision.blocks());
}

TEST(CollisionShapeTest, EmptyCollisionBlockIsNone) {
	auto&	   reg = AssetRegistry::Get();
	TempAssets t;
	t.writeXml("EmptyCollision", wrapDef("<collision></collision>"));

	reg.clearDefinitions();
	reg.loadDefinitionsFromFolder(t.root.string());
	const AssetDefinition* def = reg.getDefinition("TestAsset");
	ASSERT_NE(def, nullptr);

	EXPECT_EQ(def->collision.type, CollisionShapeType::None);
	EXPECT_FALSE(def->collision.blocks());
}

// End-to-end: the tree defs carry no XML <collision>, so these exercise the
// Lua-emit (asset:setCollisionRect) + eager-capture path. Half-extent each axis
// is trunkWidth/2 (read pre-randomization), so the footprint is a stable square.
TEST(CollisionShapeTest, OakTreeHasRectHalfExtent0_1) {
	namespace fs = std::filesystem;

	auto&	   reg = AssetRegistry::Get();
	fs::path   p   = fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
	fs::path   oak = p / "assets" / "world" / "flora" / "OakTree";
	if (!fs::exists(oak)) {
		GTEST_SKIP() << "assets/world not found";
	}

	reg.clearDefinitions();
	reg.setSharedScriptsPath(p / "assets" / "shared" / "scripts");
	reg.loadDefinitions((oak / "OakTree.xml").string());

	const AssetDefinition* def = reg.getDefinition("Flora_TreeOak");
	ASSERT_NE(def, nullptr);
	EXPECT_EQ(def->collision.type, CollisionShapeType::Rect);
	EXPECT_FLOAT_EQ(def->collision.halfExtentsMeters.x, 0.1F);
	EXPECT_FLOAT_EQ(def->collision.halfExtentsMeters.y, 0.1F);
}

TEST(CollisionShapeTest, MapleTreeHasRectHalfExtent0_075) {
	namespace fs = std::filesystem;

	auto&	   reg = AssetRegistry::Get();
	fs::path   p   = fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
	fs::path   maple = p / "assets" / "world" / "flora" / "MapleTree";
	if (!fs::exists(maple)) {
		GTEST_SKIP() << "assets/world not found";
	}

	reg.clearDefinitions();
	reg.setSharedScriptsPath(p / "assets" / "shared" / "scripts");
	reg.loadDefinitions((maple / "MapleTree.xml").string());

	const AssetDefinition* def = reg.getDefinition("Flora_TreeMaple");
	ASSERT_NE(def, nullptr);
	EXPECT_EQ(def->collision.type, CollisionShapeType::Rect);
	EXPECT_FLOAT_EQ(def->collision.halfExtentsMeters.x, 0.075F);
	EXPECT_FLOAT_EQ(def->collision.halfExtentsMeters.y, 0.075F);
}

TEST(CollisionShapeTest, PalmTreeHasRectHalfExtent0_06) {
	namespace fs = std::filesystem;

	auto&	   reg = AssetRegistry::Get();
	fs::path   p   = fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
	fs::path   palm = p / "assets" / "world" / "flora" / "PalmTree";
	if (!fs::exists(palm)) {
		GTEST_SKIP() << "assets/world not found";
	}

	reg.clearDefinitions();
	reg.setSharedScriptsPath(p / "assets" / "shared" / "scripts");
	reg.loadDefinitions((palm / "PalmTree.xml").string());

	const AssetDefinition* def = reg.getDefinition("Flora_TreePalm");
	ASSERT_NE(def, nullptr);
	EXPECT_EQ(def->collision.type, CollisionShapeType::Rect);
	EXPECT_FLOAT_EQ(def->collision.halfExtentsMeters.x, 0.06F);
	EXPECT_FLOAT_EQ(def->collision.halfExtentsMeters.y, 0.06F);
}

TEST(CollisionShapeTest, PineTreeHasRectHalfExtent0_09) {
	namespace fs = std::filesystem;

	auto&	   reg = AssetRegistry::Get();
	fs::path   p   = fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
	fs::path   pine = p / "assets" / "world" / "flora" / "PineTree";
	if (!fs::exists(pine)) {
		GTEST_SKIP() << "assets/world not found";
	}

	reg.clearDefinitions();
	reg.setSharedScriptsPath(p / "assets" / "shared" / "scripts");
	reg.loadDefinitions((pine / "PineTree.xml").string());

	const AssetDefinition* def = reg.getDefinition("Flora_TreePine");
	ASSERT_NE(def, nullptr);
	EXPECT_EQ(def->collision.type, CollisionShapeType::Rect);
	EXPECT_FLOAT_EQ(def->collision.halfExtentsMeters.x, 0.09F);
	EXPECT_FLOAT_EQ(def->collision.halfExtentsMeters.y, 0.09F);
}

TEST(CollisionShapeTest, RectDegenerateYIsNone) {
	auto&	   reg = AssetRegistry::Get();
	TempAssets t;
	// Valid x range but equal y (zero height) -> degenerate, should be ignored.
	t.writeXml("RectBadY", wrapDef("<collision><rect minX=\"0\" minY=\"0\" maxX=\"1\" maxY=\"0\"/></collision>"));

	reg.clearDefinitions();
	reg.loadDefinitionsFromFolder(t.root.string());
	const AssetDefinition* def = reg.getDefinition("TestAsset");
	ASSERT_NE(def, nullptr);

	EXPECT_EQ(def->collision.type, CollisionShapeType::None);
	EXPECT_FALSE(def->collision.blocks());
}

TEST(CollisionShapeTest, XmlCollisionWinsOverLuaEmit) {
	namespace fs = std::filesystem;

	auto&	  reg = AssetRegistry::Get();
	fs::path  p   = fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
	fs::path  sharedScripts = p / "assets" / "shared" / "scripts";
	if (!fs::exists(sharedScripts)) {
		GTEST_SKIP() << "assets/shared/scripts not found";
	}

	TempAssets t;
	// Same generator as OakTree (deciduous.lua with trunkWidth=0.5 -> would emit half-extents 0.25),
	// but XML <collision> supplies half-extents 0.1. XML must win.
	t.writeXml(
		"XmlWins",
		"<?xml version=\"1.0\"?>"
		"<AssetDefinitions><AssetDef>"
		"<defName>TestXmlWins</defName>"
		"<assetType>procedural</assetType>"
		"<generator>"
		"<scriptPath>@shared/deciduous.lua</scriptPath>"
		"<params><trunkWidth>0.5</trunkWidth><trunkHeight>1.5</trunkHeight>"
		"<canopyRadius>0.8</canopyRadius><branchCount>4</branchCount></params>"
		"</generator>"
		"<collision><rect minX=\"-0.1\" minY=\"-0.1\" maxX=\"0.1\" maxY=\"0.1\"/></collision>"
		"</AssetDef></AssetDefinitions>"
	);

	reg.clearDefinitions();
	reg.setSharedScriptsPath(sharedScripts);
	reg.loadDefinitionsFromFolder(t.root.string());
	const AssetDefinition* def = reg.getDefinition("TestXmlWins");
	ASSERT_NE(def, nullptr);

	// XML half-extents are 0.1; Lua would have emitted 0.25. XML must win.
	EXPECT_EQ(def->collision.type, CollisionShapeType::Rect);
	EXPECT_FLOAT_EQ(def->collision.halfExtentsMeters.x, 0.1F);
	EXPECT_FLOAT_EQ(def->collision.halfExtentsMeters.y, 0.1F);
}

TEST(CollisionShapeTest, LuaDegenerateRectIsNone) {
	namespace fs = std::filesystem;

	auto&	  reg = AssetRegistry::Get();
	TempAssets t;

	// Write a local Lua script that calls setCollisionRect with zero half-extents
	// (degenerate) and also adds one trivial path so generateAsset succeeds.
	const std::string luaBody =
		"function generate(asset, params)\n"
		"    local p = asset:createPath()\n"
		"    p:moveTo(0, 0)\n"
		"    p:lineTo(1, 0)\n"
		"    p:lineTo(0, 1)\n"
		"    p:close()\n"
		"    asset:setCollisionRect(0, 0, 0, 0)\n"
		"end\n";

	// Write the .lua file into the temp asset folder so the registry resolves it
	// as a local (non-@shared) script path relative to the asset's baseFolder.
	const fs::path assetDir = t.root / "DegenerateRect";
	fs::create_directories(assetDir);
	std::ofstream(assetDir / "degenerate.lua") << luaBody;
	std::ofstream(assetDir / "DegenerateRect.xml") <<
		"<?xml version=\"1.0\"?>"
		"<AssetDefinitions><AssetDef>"
		"<defName>TestDegenerateRect</defName>"
		"<assetType>procedural</assetType>"
		"<generator><scriptPath>degenerate.lua</scriptPath></generator>"
		"</AssetDef></AssetDefinitions>";

	reg.clearDefinitions();
	reg.loadDefinitionsFromFolder(t.root.string());
	const AssetDefinition* def = reg.getDefinition("TestDegenerateRect");
	ASSERT_NE(def, nullptr);

	// The guard in setCollisionRect should have rejected the zero-area rect.
	EXPECT_EQ(def->collision.type, CollisionShapeType::None);
	EXPECT_FALSE(def->collision.blocks());
}

TEST(CollisionShapeTest, BerryBushHasNoCollision) {
	namespace fs = std::filesystem;

	auto&	  reg = AssetRegistry::Get();
	fs::path  p   = fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
	fs::path  bush = p / "assets" / "world" / "flora" / "BerryBush";
	if (!fs::exists(bush)) {
		GTEST_SKIP() << "assets/world not found";
	}

	reg.clearDefinitions();
	reg.setSharedScriptsPath(p / "assets" / "shared" / "scripts");
	reg.loadDefinitions((bush / "BerryBush.xml").string());

	// BerryBush defName - find whatever loaded
	const auto names = reg.getDefinitionNames();
	ASSERT_FALSE(names.empty());
	const AssetDefinition* def = reg.getDefinition(names[0]);
	ASSERT_NE(def, nullptr);
	EXPECT_EQ(def->collision.type, CollisionShapeType::None);
	EXPECT_FALSE(def->collision.blocks());
}

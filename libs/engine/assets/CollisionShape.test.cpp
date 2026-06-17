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

TEST(CollisionShapeTest, CircleParsesRadiusAndOffset) {
	auto&	   reg = AssetRegistry::Get();
	TempAssets t;
	t.writeXml("Circle", wrapDef("<collision><circle radius=\"0.1\" offsetX=\"0.2\" offsetY=\"-0.05\"/></collision>"));

	reg.clearDefinitions();
	reg.loadDefinitionsFromFolder(t.root.string());
	const AssetDefinition* def = reg.getDefinition("TestAsset");
	ASSERT_NE(def, nullptr);

	EXPECT_EQ(def->collision.type, CollisionShapeType::Circle);
	EXPECT_FLOAT_EQ(def->collision.radiusMeters, 0.1F);
	EXPECT_FLOAT_EQ(def->collision.offsetMeters.x, 0.2F);
	EXPECT_FLOAT_EQ(def->collision.offsetMeters.y, -0.05F);
	EXPECT_TRUE(def->collision.blocks());
}

TEST(CollisionShapeTest, CircleDefaultOffsetIsZero) {
	auto&	   reg = AssetRegistry::Get();
	TempAssets t;
	t.writeXml("CircleDefaultOffset", wrapDef("<collision><circle radius=\"0.5\"/></collision>"));

	reg.clearDefinitions();
	reg.loadDefinitionsFromFolder(t.root.string());
	const AssetDefinition* def = reg.getDefinition("TestAsset");
	ASSERT_NE(def, nullptr);

	EXPECT_EQ(def->collision.type, CollisionShapeType::Circle);
	EXPECT_FLOAT_EQ(def->collision.radiusMeters, 0.5F);
	EXPECT_FLOAT_EQ(def->collision.offsetMeters.x, 0.0F);
	EXPECT_FLOAT_EQ(def->collision.offsetMeters.y, 0.0F);
}

TEST(CollisionShapeTest, CircleZeroRadiusIsNone) {
	auto&	   reg = AssetRegistry::Get();
	TempAssets t;
	t.writeXml("ZeroRadius", wrapDef("<collision><circle radius=\"0\"/></collision>"));

	reg.clearDefinitions();
	reg.loadDefinitionsFromFolder(t.root.string());
	const AssetDefinition* def = reg.getDefinition("TestAsset");
	ASSERT_NE(def, nullptr);

	EXPECT_EQ(def->collision.type, CollisionShapeType::None);
	EXPECT_FALSE(def->collision.blocks());
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

// Sanity-check that the tree XML files load with the expected radii.
TEST(CollisionShapeTest, OakTreeHasCircleRadius0_1) {
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
	EXPECT_EQ(def->collision.type, CollisionShapeType::Circle);
	EXPECT_FLOAT_EQ(def->collision.radiusMeters, 0.1F);
}

TEST(CollisionShapeTest, MapleTreeHasCircleRadius0_075) {
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
	EXPECT_EQ(def->collision.type, CollisionShapeType::Circle);
	EXPECT_FLOAT_EQ(def->collision.radiusMeters, 0.075F);
}

TEST(CollisionShapeTest, PalmTreeHasCircleRadius0_06) {
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
	EXPECT_EQ(def->collision.type, CollisionShapeType::Circle);
	EXPECT_FLOAT_EQ(def->collision.radiusMeters, 0.06F);
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

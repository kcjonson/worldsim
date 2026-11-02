// The ground bake's disk cache: a stored bake comes back byte for byte, the key moves when the def's
// script does or its XML is reloaded, every def name gets a safe file of its own, and a damaged file is
// ignored until the next store rewrites it. No GL: the bake is made up. GroundTileBaker.test.cpp runs the
// cache behind a real bake.

#include "world/rendering/GroundTileCache.h"

#include <assets/AssetRegistry.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace worldview;

namespace {

	namespace fs = std::filesystem;

	struct TempDir {
		fs::path root;

		TempDir() {
			static std::atomic<int> counter{0};
			root = fs::temp_directory_path() / ("ground_cache_" + std::to_string(counter++) + "_" + std::to_string(std::rand()));
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

	std::string defXml(const std::string& params = "", const std::string& inner = "") {
		return "<?xml version=\"1.0\"?><AssetDefinitions><AssetDef><defName>Ground_CacheTest</defName><role>ground</role>"
			   "<assetType>procedural</assetType><generator><scriptPath>gen.lua</scriptPath><params>" +
			   params +
			   "</params></generator>"
			   "<surface>Snow</surface><period>1</period><pixelsPerMeter>32</pixelsPerMeter><variantCount>2</variantCount>" +
			   inner + "</AssetDef></AssetDefinitions>";
	}

	const assets::AssetDefinition& loadDef(const TempDir& t) {
		t.write("Tile/Tile.xml", defXml());
		t.write("Tile/gen.lua", "-- a generator\n");
		auto& registry = assets::AssetRegistry::Get();
		registry.clearDefinitions();
		registry.loadDefinitionsFromFolder(t.root.string());
		return *registry.getDefinition("Ground_CacheTest");
	}

	// A 32 texel layer's two variants, mips 32 down to 1, each byte and sample field telling them apart.
	GroundTileBake madeUpBake() {
		GroundTileBake bake{.defName = "Ground_CacheTest", .periodMeters = 1.0F, .pixelsPerMeter = 32, .layerSize = 32};
		for (uint32_t v = 0; v < 2; ++v) {
			GroundVariantBake& variant = bake.variants.emplace_back();
			variant.variant = v;
			variant.seed = 1000 + v;
			for (int level = 0; level < 6; ++level) {
				const int  size = 32 >> level;
				GroundMip& mip = variant.mips.emplace_back(GroundMip{.size = size, .rgba = {}});
				for (int i = 0; i < size * size * 4; ++i) {
					mip.rgba.push_back(static_cast<uint8_t>((i * 7) + (level * 13) + v));
				}
				variant.samples.push_back({
					.renderSize = size * 8,
					.supersample = 8,
					.vertexCount = static_cast<size_t>(100 * (level + 1)),
					.meshCount = 1,
					.mean = glm::vec3(0.1F * static_cast<float>(level), 0.2F, 0.3F + static_cast<float>(v)),
					.contrast = 0.5F,
					.referenceContrast = 0.6F,
					.lockShift = glm::vec3(0.01F),
					.heightSpread = 0.25F + (0.01F * static_cast<float>(level)),
				});
			}
		}
		return bake;
	}

} // namespace

TEST(GroundTileCacheTest, AStoredBakeComesBackWhole) {
	const TempDir				   t;
	const assets::AssetDefinition& def = loadDef(t);
	const GroundTileCache		   cache(t.root / "cache");
	const std::optional<uint64_t>  key = GroundTileCache::keyFor(def, 4096);
	ASSERT_TRUE(key.has_value());

	const GroundTileBake stored = madeUpBake();
	ASSERT_TRUE(cache.store(stored, *key));
	ASSERT_TRUE(cache.store(stored, *key)) << "a second store replaces the file";
	const std::optional<GroundTileBake> loaded = cache.load(def, *key);
	ASSERT_TRUE(loaded.has_value());
	EXPECT_TRUE(loaded->fromCache);
	EXPECT_EQ(loaded->layerSize, 32);
	ASSERT_EQ(loaded->variants.size(), 2U);
	for (size_t v = 0; v < 2; ++v) {
		EXPECT_EQ(loaded->variants[v].seed, stored.variants[v].seed);
		ASSERT_EQ(loaded->variants[v].mips.size(), 6U);
		for (size_t k = 0; k < 6; ++k) {
			EXPECT_EQ(loaded->variants[v].mips[k].size, stored.variants[v].mips[k].size);
			EXPECT_EQ(loaded->variants[v].mips[k].rgba, stored.variants[v].mips[k].rgba) << "variant " << v << " mip " << k;
			EXPECT_EQ(loaded->variants[v].samples[k].mean, stored.variants[v].samples[k].mean);
			EXPECT_EQ(loaded->variants[v].samples[k].vertexCount, stored.variants[v].samples[k].vertexCount);
			EXPECT_EQ(loaded->variants[v].samples[k].referenceContrast, stored.variants[v].samples[k].referenceContrast);
			EXPECT_EQ(loaded->variants[v].samples[k].heightSpread, stored.variants[v].samples[k].heightSpread);
		}
	}
	EXPECT_EQ(loaded->metrics.size(), 6U) << "the metrics are measured again from the samples";
	assets::AssetRegistry::Get().clearDefinitions();
}

TEST(GroundTileCacheTest, TheKeyFollowsTheLoadedDefAndTheScript) {
	const TempDir t;
	auto&		  registry = assets::AssetRegistry::Get();
	const auto	  keyNow = [&registry]() {
		   return GroundTileCache::keyFor(*registry.getDefinition("Ground_CacheTest"), 4096);
	};
	const auto reload = [&registry, &t]() {
		registry.clearDefinitions();
		registry.loadDefinitionsFromFolder(t.root.string());
	};
	const GroundTileCache cache(t.root / "cache");
	loadDef(t);
	const uint64_t original = keyNow().value();
	EXPECT_EQ(keyNow().value(), original) << "the same def, the same key";
	EXPECT_NE(GroundTileCache::keyFor(*registry.getDefinition("Ground_CacheTest"), 2048).value(), original)
		<< "the render tile size is in it";
	ASSERT_TRUE(cache.store(madeUpBake(), original));

	// The generator reads its script from disk on every run, so an edit moves the key at once.
	t.write("Tile/gen.lua", "-- a generator, edited\n");
	const uint64_t scriptEdited = keyNow().value();
	EXPECT_NE(scriptEdited, original);
	EXPECT_FALSE(cache.load(*registry.getDefinition("Ground_CacheTest"), scriptEdited).has_value())
		<< "a bake stored under the old key is stale";

	// The generator runs the params parsed at load, so an XML edit moves the key once it is reloaded, not before.
	t.write("Tile/Tile.xml", defXml("<density>3</density>"));
	EXPECT_EQ(keyNow().value(), scriptEdited) << "until a reload the bake still runs the def as loaded";
	reload();
	const uint64_t paramsEdited = keyNow().value();
	EXPECT_NE(paramsEdited, scriptEdited);

	t.write(
		"Tile/Tile.xml",
		defXml(
			"<!-- a note --><density>3</density>",
			"<rotate>mirror</rotate><macro><amplitude>0.4</amplitude></macro>"
			"<transition><width>0.8</width></transition><transition over=\"Dirt\"><tint>0.3</tint></transition>"
		)
	);
	reload();
	EXPECT_EQ(keyNow().value(), paramsEdited) << "a comment, <rotate>, <macro> or a <transition> block never re-bakes";

	fs::remove(t.root / "Tile" / "gen.lua");
	EXPECT_FALSE(keyNow().has_value()) << "no script, nothing to key a bake on";
	registry.clearDefinitions();
}

TEST(GroundTileCacheTest, EveryNameGetsItsOwnFileInsideTheDirectory) {
	const fs::path		  directory = fs::path("cache") / "ground";
	const GroundTileCache cache(directory);
	const auto			  lower = [](std::string s) {
		   std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		   return s;
	};

	const fs::path grass = cache.pathFor("Ground_Grass");
	EXPECT_EQ(grass, cache.pathFor("Ground_Grass")) << "one name, one file, every run";
	EXPECT_EQ(grass.parent_path(), directory);
	EXPECT_EQ(grass.filename().string().rfind("Ground_Grass-", 0), 0U) << grass;
	EXPECT_EQ(grass.extension(), ".gtile");
	EXPECT_NE(lower(cache.pathFor("ground_grass").filename().string()), lower(grass.filename().string()))
		<< "names that differ only in case don't share a file on a case-blind file system";

	for (const std::string& name :
		 std::vector<std::string>{"../../escape", "..", "a:b", "a/b\\c", "Ground Grass?*", std::string(300, 'x')}) {
		const fs::path path = cache.pathFor(name);
		EXPECT_EQ(path.parent_path(), directory) << name;
		const std::string file = path.filename().string();
		EXPECT_LE(file.size(), 80U) << name;
		EXPECT_EQ(file.find(".."), std::string::npos) << name;
		for (const char c : path.stem().string()) {
			EXPECT_TRUE(std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-') << name << ": " << file;
		}
	}
	EXPECT_NE(cache.pathFor("a:b"), cache.pathFor("a/b")) << "names that sanitize alike still get files of their own";
}

TEST(GroundTileCacheTest, ADamagedFileIsIgnoredUntilStoredAgain) {
	const TempDir				   t;
	const assets::AssetDefinition& def = loadDef(t);
	const GroundTileCache		   cache(t.root / "cache");
	const uint64_t				   key = GroundTileCache::keyFor(def, 4096).value();
	ASSERT_TRUE(cache.store(madeUpBake(), key));
	const fs::path	file = cache.pathFor(def.defName);
	const uintmax_t size = fs::file_size(file);

	{
		std::fstream damaged(file, std::ios::binary | std::ios::in | std::ios::out);
		damaged.seekp(static_cast<std::streamoff>(size / 2));
		damaged.put('\x5A');
		damaged.seekp(static_cast<std::streamoff>(size / 2) + 1);
		damaged.put('\xA5');
	}
	EXPECT_FALSE(cache.load(def, key).has_value()) << "a flipped byte fails the checksum";

	ASSERT_TRUE(cache.store(madeUpBake(), key));
	fs::resize_file(file, size - 100);
	EXPECT_FALSE(cache.load(def, key).has_value()) << "a truncated file";

	ASSERT_TRUE(cache.store(madeUpBake(), key));
	EXPECT_TRUE(cache.load(def, key).has_value()) << "and the next store makes it good";
	assets::AssetRegistry::Get().clearDefinitions();
}

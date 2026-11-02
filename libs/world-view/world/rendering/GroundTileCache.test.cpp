// The ground bake's disk cache: a stored bake comes back byte for byte, the key moves when the def's
// script or XML does, and a damaged file is ignored until the next store rewrites it. No GL: the bake is
// made up. GroundTileBaker.test.cpp runs the cache behind a real bake.

#include "world/rendering/GroundTileCache.h"

#include <assets/AssetRegistry.h>

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

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

	std::string defXml(const std::string& inner = "") {
		return "<?xml version=\"1.0\"?><AssetDefinitions><AssetDef><defName>Ground_CacheTest</defName><role>ground</role>"
			   "<assetType>procedural</assetType><generator><scriptPath>gen.lua</scriptPath></generator>"
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

TEST(GroundTileCacheTest, EditingTheScriptOrXmlMovesTheKey) {
	const TempDir				   t;
	const assets::AssetDefinition& def = loadDef(t);
	const GroundTileCache		   cache(t.root / "cache");
	const uint64_t				   original = GroundTileCache::keyFor(def, 4096).value();
	EXPECT_EQ(GroundTileCache::keyFor(def, 4096).value(), original) << "the same files, the same key";
	EXPECT_NE(GroundTileCache::keyFor(def, 2048).value(), original) << "the render tile size is in it";
	ASSERT_TRUE(cache.store(madeUpBake(), original));

	t.write("Tile/gen.lua", "-- a generator, edited\n");
	const uint64_t scriptEdited = GroundTileCache::keyFor(def, 4096).value();
	EXPECT_NE(scriptEdited, original);
	EXPECT_FALSE(cache.load(def, scriptEdited).has_value()) << "a bake stored under the old key is stale";

	t.write("Tile/Tile.xml", defXml("<rotate>mirror</rotate>"));
	const uint64_t xmlEdited = GroundTileCache::keyFor(def, 4096).value();
	EXPECT_NE(xmlEdited, scriptEdited);
	t.write(
		"Tile/Tile.xml",
		defXml("<rotate>mirror</rotate><!-- a note --><transition><width>0.8</width></transition><transition over=\"Dirt\"><tint>0.3</tint></transition>")
	);
	EXPECT_EQ(GroundTileCache::keyFor(def, 4096).value(), xmlEdited) << "a comment or a <transition> block never re-bakes";

	fs::remove(t.root / "Tile" / "gen.lua");
	EXPECT_FALSE(GroundTileCache::keyFor(def, 4096).has_value()) << "no script, nothing to key a bake on";
	assets::AssetRegistry::Get().clearDefinitions();
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

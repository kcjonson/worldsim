#include "worldgen/io/PlanetIO.h"

#include <utils/WorldHash.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>

namespace worldgen {
namespace {

constexpr uint32_t kTestSubdivision = 8;
// Goldberg grid: 10*n*n owned vertices + 2 poles.
constexpr uint32_t kTestTileCount = 10u * kTestSubdivision * kTestSubdivision + 2u; // 642

GeneratedWorld makeTestWorld() {
	GeneratedWorld world;

	world.params.starMass = 1.25;
	world.params.starRadius = 1.1;
	world.params.starTemperature = 6100.0;
	world.params.starAge = 3.2e9;
	world.params.planetRadius = 0.9;
	world.params.planetMass = 0.8;
	world.params.rotationRate = 1.5;
	world.params.tectonicPlateCount = 7;
	world.params.waterAmount = 0.55;
	world.params.atmosphereStrength = 1.2;
	world.params.planetAge = 2.7e9;
	world.params.semiMajorAxis = 1.3;
	world.params.eccentricity = 0.04;
	world.params.seed = 0xDEADBEEFCAFEF00DULL;
	world.params.gridSubdivision = kTestSubdivision;

	world.derived = derive(world.params);
	world.grid = std::make_shared<const SphereGrid>(kTestSubdivision);
	world.seaLevelMeters = -123.5f;

	world.data.allocate(kTestTileCount);
	for (uint32_t i = 0; i < kTestTileCount; ++i) {
		world.data.elevation[i] = static_cast<float>(i) * 0.5f - 100.0f;
		world.data.temperatureMean[i] = static_cast<int16_t>(static_cast<int>(i % 600) - 300);
		world.data.precipitation[i] = static_cast<uint16_t>((i * 7) % 3000);
		world.data.biome[i] = static_cast<uint8_t>(i % static_cast<uint32_t>(Biome::Count));
		world.data.flags[i] = static_cast<uint8_t>(i % 256);
		world.data.crustAge[i] = static_cast<uint16_t>((i * 13) % 220);
		world.data.orogenyAge[i] = (i % 10 == 0) ? uint16_t{65535} : static_cast<uint16_t>((i * 17) % 500);
	}
	world.validFields = static_cast<uint32_t>(WorldField::Elevation) |
		static_cast<uint32_t>(WorldField::TemperatureMean) |
		static_cast<uint32_t>(WorldField::Precipitation) |
		static_cast<uint32_t>(WorldField::Biome) |
		static_cast<uint32_t>(WorldField::Flags) |
		static_cast<uint32_t>(WorldField::CrustAge) |
		static_cast<uint32_t>(WorldField::OrogenyAge);

	world.plates.push_back({{0.0, 0.0, 1.0}, 0.015f, true});
	world.plates.push_back({{0.6, -0.8, 0.0}, -0.007f, false});
	world.plates.push_back({{-0.577, 0.577, 0.577}, 0.021f, true});

	world.summary.landFraction = 0.31f;
	world.summary.meanTemperatureC = 14.2f;
	world.summary.riverTileCount = 42;
	world.summary.habitability = 0.66f;
	for (size_t i = 0; i < world.summary.biomeHistogram.size(); ++i) {
		world.summary.biomeHistogram[i] = static_cast<uint32_t>(i * 3 + 1);
	}

	world.worldHash = computeWorldDataHash(world.validFields, world.data);

	return world;
}

void flipByteAt(const std::filesystem::path& path, std::streamoff offset) {
	std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
	ASSERT_TRUE(f.is_open());
	f.seekg(offset);
	char c = 0;
	f.read(&c, 1);
	ASSERT_TRUE(f.good());
	c = static_cast<char>(c ^ 0xFF);
	f.seekp(offset);
	f.write(&c, 1);
	ASSERT_TRUE(f.good());
}

class PlanetIOTest : public ::testing::Test {
  protected:
	void SetUp() override {
		const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
		filePath = std::filesystem::temp_directory_path() /
			(std::string("worldsim-planetio-") + info->name() + ".wspl");
		std::filesystem::remove(filePath);
	}

	void TearDown() override {
		std::error_code ec;
		std::filesystem::remove(filePath, ec);
		std::filesystem::path tempPath = filePath;
		tempPath += ".tmp";
		std::filesystem::remove(tempPath, ec);
	}

	std::filesystem::path filePath;
};

TEST_F(PlanetIOTest, RoundtripPreservesAllFields) {
	GeneratedWorld world = makeTestWorld();

	ASSERT_TRUE(savePlanet(world, filePath));
	ASSERT_TRUE(std::filesystem::exists(filePath));
	std::filesystem::path tempPath = filePath;
	tempPath += ".tmp";
	EXPECT_FALSE(std::filesystem::exists(tempPath)) << "temp file left behind";

	auto loaded = loadPlanet(filePath);
	ASSERT_NE(loaded, nullptr);

	const PlanetParams& a = world.params;
	const PlanetParams& b = loaded->params;
	EXPECT_EQ(a.starMass, b.starMass);
	EXPECT_EQ(a.starRadius, b.starRadius);
	EXPECT_EQ(a.starTemperature, b.starTemperature);
	EXPECT_EQ(a.starAge, b.starAge);
	EXPECT_EQ(a.planetRadius, b.planetRadius);
	EXPECT_EQ(a.planetMass, b.planetMass);
	EXPECT_EQ(a.rotationRate, b.rotationRate);
	EXPECT_EQ(a.tectonicPlateCount, b.tectonicPlateCount);
	EXPECT_EQ(a.waterAmount, b.waterAmount);
	EXPECT_EQ(a.atmosphereStrength, b.atmosphereStrength);
	EXPECT_EQ(a.planetAge, b.planetAge);
	EXPECT_EQ(a.semiMajorAxis, b.semiMajorAxis);
	EXPECT_EQ(a.eccentricity, b.eccentricity);
	EXPECT_EQ(a.seed, b.seed);
	EXPECT_EQ(a.gridSubdivision, b.gridSubdivision);

	EXPECT_EQ(world.seaLevelMeters, loaded->seaLevelMeters);
	EXPECT_EQ(world.validFields, loaded->validFields);
	EXPECT_EQ(world.worldHash, loaded->worldHash);

	EXPECT_EQ(world.summary.landFraction, loaded->summary.landFraction);
	EXPECT_EQ(world.summary.meanTemperatureC, loaded->summary.meanTemperatureC);
	EXPECT_EQ(world.summary.riverTileCount, loaded->summary.riverTileCount);
	EXPECT_EQ(world.summary.habitability, loaded->summary.habitability);
	EXPECT_EQ(world.summary.biomeHistogram, loaded->summary.biomeHistogram);

	ASSERT_EQ(world.plates.size(), loaded->plates.size());
	for (size_t i = 0; i < world.plates.size(); ++i) {
		EXPECT_EQ(world.plates[i].eulerPole.x, loaded->plates[i].eulerPole.x);
		EXPECT_EQ(world.plates[i].eulerPole.y, loaded->plates[i].eulerPole.y);
		EXPECT_EQ(world.plates[i].eulerPole.z, loaded->plates[i].eulerPole.z);
		EXPECT_EQ(world.plates[i].angularSpeed, loaded->plates[i].angularSpeed);
		EXPECT_EQ(world.plates[i].isContinental, loaded->plates[i].isContinental);
	}

	// Valid arrays roundtrip exactly; invalid ones come back as fresh
	// allocate() defaults.
	EXPECT_EQ(world.data.elevation, loaded->data.elevation);
	EXPECT_EQ(world.data.temperatureMean, loaded->data.temperatureMean);
	EXPECT_EQ(world.data.precipitation, loaded->data.precipitation);
	EXPECT_EQ(world.data.biome, loaded->data.biome);
	EXPECT_EQ(world.data.flags, loaded->data.flags);
	EXPECT_EQ(world.data.crustAge, loaded->data.crustAge);
	EXPECT_EQ(world.data.orogenyAge, loaded->data.orogenyAge);

	WorldData defaults;
	defaults.allocate(kTestTileCount);
	EXPECT_EQ(defaults.temperatureRange, loaded->data.temperatureRange);
	EXPECT_EQ(defaults.windDir, loaded->data.windDir);
	EXPECT_EQ(defaults.windSpeed, loaded->data.windSpeed);
	EXPECT_EQ(defaults.plateId, loaded->data.plateId);
	EXPECT_EQ(defaults.boundaryType, loaded->data.boundaryType);
	EXPECT_EQ(defaults.boundaryDistance, loaded->data.boundaryDistance);
	EXPECT_EQ(defaults.waterDepth, loaded->data.waterDepth);
	EXPECT_EQ(defaults.flowAccum, loaded->data.flowAccum);
	EXPECT_EQ(defaults.downhill, loaded->data.downhill);
	EXPECT_EQ(defaults.snowCover, loaded->data.snowCover);

	// Derived values recomputed from params, grid rebuilt from subdivision.
	const DerivedPlanetValues expectedDerived = derive(world.params);
	EXPECT_EQ(expectedDerived.planetRadiusMeters, loaded->derived.planetRadiusMeters);
	EXPECT_EQ(expectedDerived.gravity, loaded->derived.gravity);
	EXPECT_EQ(expectedDerived.solarConstant, loaded->derived.solarConstant);
	EXPECT_EQ(expectedDerived.equilibriumTemperatureK, loaded->derived.equilibriumTemperatureK);
	EXPECT_EQ(expectedDerived.rotationPeriodSeconds, loaded->derived.rotationPeriodSeconds);
	EXPECT_EQ(expectedDerived.lapseRateCPerKm, loaded->derived.lapseRateCPerKm);

	ASSERT_NE(loaded->grid, nullptr);
	EXPECT_EQ(kTestSubdivision, loaded->grid->subdivision());
	EXPECT_EQ(kTestTileCount, loaded->grid->tileCount());
}

// std::filesystem::rename must replace an existing destination on every
// platform (overwriting a cached planet, e.g. regenerating quickstart)
TEST_F(PlanetIOTest, SaveOverwritesExistingFile) {
	GeneratedWorld first = makeTestWorld();
	ASSERT_TRUE(savePlanet(first, filePath));

	GeneratedWorld second = makeTestWorld();
	second.params.seed = first.params.seed + 1;
	for (auto& e : second.data.elevation) {
		e += 100.0f;
	}
	second.worldHash = computeWorldDataHash(second.validFields, second.data);
	ASSERT_TRUE(savePlanet(second, filePath));

	auto loaded = loadPlanet(filePath);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(loaded->params.seed, second.params.seed);
	EXPECT_EQ(loaded->data.elevation, second.data.elevation);
}

TEST_F(PlanetIOTest, LoadNonexistentFileReturnsNull) {
	EXPECT_EQ(loadPlanet(filePath), nullptr);
}

TEST_F(PlanetIOTest, LoadTruncatedFileReturnsNull) {
	GeneratedWorld world = makeTestWorld();
	ASSERT_TRUE(savePlanet(world, filePath));

	const auto fullSize = std::filesystem::file_size(filePath);
	std::filesystem::resize_file(filePath, fullSize / 2);
	EXPECT_EQ(loadPlanet(filePath), nullptr);

	// Truncated mid-magic as well.
	ASSERT_TRUE(savePlanet(world, filePath));
	std::filesystem::resize_file(filePath, 3);
	EXPECT_EQ(loadPlanet(filePath), nullptr);
}

TEST_F(PlanetIOTest, CorruptedArrayByteFailsHashCheck) {
	GeneratedWorld world = makeTestWorld();
	ASSERT_TRUE(savePlanet(world, filePath));

	// The last array written is orogenyAge (642 * 2 = 1284 bytes), so a byte 10
	// from the end lands inside an array region covered by the hash.
	const auto fileSize = std::filesystem::file_size(filePath);
	flipByteAt(filePath, static_cast<std::streamoff>(fileSize) - 10);

	EXPECT_EQ(loadPlanet(filePath), nullptr);
}

TEST_F(PlanetIOTest, BadMagicReturnsNull) {
	GeneratedWorld world = makeTestWorld();
	ASSERT_TRUE(savePlanet(world, filePath));
	flipByteAt(filePath, 0);
	EXPECT_EQ(loadPlanet(filePath), nullptr);
}

TEST_F(PlanetIOTest, UnsupportedVersionReturnsNull) {
	GeneratedWorld world = makeTestWorld();
	ASSERT_TRUE(savePlanet(world, filePath));
	flipByteAt(filePath, 4); // corrupts low byte of the uint32 format version to a non-4 value
	EXPECT_EQ(loadPlanet(filePath), nullptr);
}

// Version 1 files (pre-hex-grid) must be rejected — downhill semantics and tile
// count changed incompatibly when converting to the Goldberg hex grid.
TEST_F(PlanetIOTest, VersionOneFileReturnsNull) {
	GeneratedWorld world = makeTestWorld();
	ASSERT_TRUE(savePlanet(world, filePath));

	// Overwrite the format version field (offset 4, uint32 LE) with 1.
	std::fstream f(filePath, std::ios::binary | std::ios::in | std::ios::out);
	ASSERT_TRUE(f.is_open());
	f.seekp(4);
	const uint8_t v1[4] = {1, 0, 0, 0};
	f.write(reinterpret_cast<const char*>(v1), 4);
	ASSERT_TRUE(f.good());
	f.close();

	EXPECT_EQ(loadPlanet(filePath), nullptr);
}

// Version 2 files (pre-crustAge/orogenyAge) must be rejected — callers rely on
// the auto-regenerate path that fires when loadPlanet returns nullptr.
TEST_F(PlanetIOTest, VersionTwoFileReturnsNull) {
	GeneratedWorld world = makeTestWorld();
	ASSERT_TRUE(savePlanet(world, filePath));

	// Overwrite the format version field (offset 4, uint32 LE) with 2.
	std::fstream f(filePath, std::ios::binary | std::ios::in | std::ios::out);
	ASSERT_TRUE(f.is_open());
	f.seekp(4);
	const uint8_t v2[4] = {2, 0, 0, 0};
	f.write(reinterpret_cast<const char*>(v2), 4);
	ASSERT_TRUE(f.good());
	f.close();

	EXPECT_EQ(loadPlanet(filePath), nullptr);
}

// Version 3 files (pre-iceThickness/iceFlow) must be rejected — callers rely on
// the auto-regenerate path that fires when loadPlanet returns nullptr.
TEST_F(PlanetIOTest, VersionThreeFileReturnsNull) {
	GeneratedWorld world = makeTestWorld();
	ASSERT_TRUE(savePlanet(world, filePath));

	// Overwrite the format version field (offset 4, uint32 LE) with 3.
	std::fstream f(filePath, std::ios::binary | std::ios::in | std::ios::out);
	ASSERT_TRUE(f.is_open());
	f.seekp(4);
	const uint8_t v3[4] = {3, 0, 0, 0};
	f.write(reinterpret_cast<const char*>(v3), 4);
	ASSERT_TRUE(f.good());
	f.close();

	EXPECT_EQ(loadPlanet(filePath), nullptr);
}

} // namespace
} // namespace worldgen

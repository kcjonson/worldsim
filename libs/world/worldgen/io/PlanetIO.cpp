#include "worldgen/io/PlanetIO.h"

#include <utils/Log.h>
#include <utils/WorldHash.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>
#include <type_traits>
#include <vector>

static_assert(std::endian::native == std::endian::little,
	"Planet file format is little-endian; big-endian targets are unsupported");
static_assert(std::numeric_limits<float>::is_iec559 && sizeof(float) == 4,
	"Planet file format requires IEEE-754 binary32 float");
static_assert(std::numeric_limits<double>::is_iec559 && sizeof(double) == 8,
	"Planet file format requires IEEE-754 binary64 double");

namespace worldgen {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'P', 'L'};
constexpr uint32_t kFormatVersion = 1;
// loadPlanet allocates every WorldData array at tileCount (10*n^2) elements,
// so the cap is bounded by what fits in memory today: 2048 is ~42M tiles
// (~1.1 GB), headroom over the UI maximum of 1449. Raise only once the
// mmap/streamed planet database exists (see status.md).
constexpr uint32_t kMaxSubdivision = 2048;
constexpr uint32_t kMaxPlateCount = 1u << 16;

struct Writer {
	std::ostream& out;

	template <typename T>
	void scalar(T v) {
		static_assert(std::is_trivially_copyable_v<T>);
		out.write(reinterpret_cast<const char*>(&v), sizeof(T));
	}

	void bytes(const void* data, size_t len) {
		out.write(static_cast<const char*>(data), static_cast<std::streamsize>(len));
	}

	template <typename T>
	void span(const std::vector<T>& v) {
		static_assert(std::is_trivially_copyable_v<T>);
		out.write(reinterpret_cast<const char*>(v.data()),
			static_cast<std::streamsize>(v.size() * sizeof(T)));
	}
};

struct Reader {
	std::istream& in;

	template <typename T>
	void scalar(T& v) {
		static_assert(std::is_trivially_copyable_v<T>);
		in.read(reinterpret_cast<char*>(&v), sizeof(T));
	}

	void bytes(void* data, size_t len) {
		in.read(static_cast<char*>(data), static_cast<std::streamsize>(len));
	}

	template <typename T>
	void span(std::vector<T>& v) {
		static_assert(std::is_trivially_copyable_v<T>);
		in.read(reinterpret_cast<char*>(v.data()),
			static_cast<std::streamsize>(v.size() * sizeof(T)));
	}

	bool good() const { return static_cast<bool>(in); }
};

} // namespace

bool savePlanet(const GeneratedWorld& world, const std::filesystem::path& path) {
	const uint32_t n = world.params.gridSubdivision;
	if (n == 0 || n > kMaxSubdivision) {
		LOG_ERROR(World, "savePlanet: invalid gridSubdivision %u", n);
		return false;
	}
	const uint32_t tileCount = 10u * n * n;

	bool arraysValid = true;
	forEachFieldArray(world.data, [&](WorldField field, const auto& arr) {
		if ((world.validFields & static_cast<uint32_t>(field)) && arr.size() != tileCount) {
			LOG_ERROR(World, "savePlanet: field bit 0x%x has %zu elements, expected %u",
				static_cast<uint32_t>(field), arr.size(), tileCount);
			arraysValid = false;
		}
	});
	if (!arraysValid) {
		return false;
	}
	if (world.plates.size() > kMaxPlateCount) {
		LOG_ERROR(World, "savePlanet: plate count %zu exceeds limit %u",
			world.plates.size(), kMaxPlateCount);
		return false;
	}

	std::error_code ec;
	if (path.has_parent_path()) {
		std::filesystem::create_directories(path.parent_path(), ec);
		if (ec) {
			LOG_ERROR(World, "savePlanet: cannot create directory %s: %s",
				path.parent_path().string().c_str(), ec.message().c_str());
			return false;
		}
	}

	// Write to a sibling temp file, then rename into place so a crash or
	// I/O failure never leaves a truncated file at the target path.
	std::filesystem::path tempPath = path;
	tempPath += ".tmp";

	{
		std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
		if (!out) {
			LOG_ERROR(World, "savePlanet: cannot open %s for writing",
				tempPath.string().c_str());
			return false;
		}
		Writer w{out};

		w.bytes(kMagic, sizeof(kMagic));
		w.scalar(kFormatVersion);

		const PlanetParams& p = world.params;
		w.scalar(p.starMass);
		w.scalar(p.starRadius);
		w.scalar(p.starTemperature);
		w.scalar(p.starAge);
		w.scalar(p.planetRadius);
		w.scalar(p.planetMass);
		w.scalar(p.rotationRate);
		w.scalar(static_cast<int32_t>(p.tectonicPlateCount));
		w.scalar(p.waterAmount);
		w.scalar(p.atmosphereStrength);
		w.scalar(p.planetAge);
		w.scalar(p.semiMajorAxis);
		w.scalar(p.eccentricity);
		w.scalar(p.seed);
		w.scalar(p.gridSubdivision);

		w.scalar(world.seaLevelMeters);
		w.scalar(world.validFields);
		w.scalar(world.worldHash);

		const WorldSummary& s = world.summary;
		w.scalar(s.landFraction);
		w.scalar(s.meanTemperatureC);
		w.scalar(s.riverTileCount);
		w.scalar(s.habitability);
		w.scalar(static_cast<uint32_t>(s.biomeHistogram.size()));
		for (uint32_t count : s.biomeHistogram) {
			w.scalar(count);
		}

		w.scalar(static_cast<uint32_t>(world.plates.size()));
		for (const PlateInfo& plate : world.plates) {
			w.scalar(plate.eulerPole.x);
			w.scalar(plate.eulerPole.y);
			w.scalar(plate.eulerPole.z);
			w.scalar(plate.angularSpeed);
			w.scalar(static_cast<uint8_t>(plate.isContinental ? 1 : 0));
		}

		w.scalar(tileCount);
		forEachFieldArray(world.data, [&](WorldField field, const auto& arr) {
			if (world.validFields & static_cast<uint32_t>(field)) {
				w.span(arr);
			}
		});

		out.flush();
		if (!out) {
			LOG_ERROR(World, "savePlanet: write failed for %s", tempPath.string().c_str());
			out.close();
			std::filesystem::remove(tempPath, ec);
			return false;
		}
	}

	std::filesystem::rename(tempPath, path, ec);
	if (ec) {
		LOG_ERROR(World, "savePlanet: rename %s -> %s failed: %s",
			tempPath.string().c_str(), path.string().c_str(), ec.message().c_str());
		std::filesystem::remove(tempPath, ec);
		return false;
	}
	return true;
}

std::shared_ptr<const GeneratedWorld> loadPlanet(const std::filesystem::path& path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		LOG_ERROR(World, "loadPlanet: cannot open %s", path.string().c_str());
		return nullptr;
	}
	Reader r{in};

	char magic[4] = {};
	r.bytes(magic, sizeof(magic));
	if (!r.good() || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
		LOG_ERROR(World, "loadPlanet: %s is not a planet file (bad magic)",
			path.string().c_str());
		return nullptr;
	}

	uint32_t version = 0;
	r.scalar(version);
	if (!r.good() || version != kFormatVersion) {
		LOG_ERROR(World, "loadPlanet: %s has unsupported format version %u (expected %u)",
			path.string().c_str(), version, kFormatVersion);
		return nullptr;
	}

	auto world = std::make_shared<GeneratedWorld>();
	PlanetParams& p = world->params;
	int32_t tectonicPlateCount = 0;
	r.scalar(p.starMass);
	r.scalar(p.starRadius);
	r.scalar(p.starTemperature);
	r.scalar(p.starAge);
	r.scalar(p.planetRadius);
	r.scalar(p.planetMass);
	r.scalar(p.rotationRate);
	r.scalar(tectonicPlateCount);
	r.scalar(p.waterAmount);
	r.scalar(p.atmosphereStrength);
	r.scalar(p.planetAge);
	r.scalar(p.semiMajorAxis);
	r.scalar(p.eccentricity);
	r.scalar(p.seed);
	r.scalar(p.gridSubdivision);
	p.tectonicPlateCount = tectonicPlateCount;

	if (!r.good()) {
		LOG_ERROR(World, "loadPlanet: %s truncated in header", path.string().c_str());
		return nullptr;
	}
	const uint32_t n = p.gridSubdivision;
	if (n == 0 || n > kMaxSubdivision) {
		LOG_ERROR(World, "loadPlanet: %s has invalid gridSubdivision %u",
			path.string().c_str(), n);
		return nullptr;
	}

	r.scalar(world->seaLevelMeters);
	r.scalar(world->validFields);
	r.scalar(world->worldHash);
	if (!r.good()) {
		LOG_ERROR(World, "loadPlanet: %s truncated in header", path.string().c_str());
		return nullptr;
	}
	if (world->validFields & ~kAllWorldFields) {
		LOG_ERROR(World, "loadPlanet: %s has unknown field bits 0x%x",
			path.string().c_str(), world->validFields & ~kAllWorldFields);
		return nullptr;
	}

	WorldSummary& s = world->summary;
	r.scalar(s.landFraction);
	r.scalar(s.meanTemperatureC);
	r.scalar(s.riverTileCount);
	r.scalar(s.habitability);
	uint32_t histogramCount = 0;
	r.scalar(histogramCount);
	if (!r.good() || histogramCount != s.biomeHistogram.size()) {
		LOG_ERROR(World, "loadPlanet: %s biome histogram size %u, expected %zu",
			path.string().c_str(), histogramCount, s.biomeHistogram.size());
		return nullptr;
	}
	for (uint32_t& count : s.biomeHistogram) {
		r.scalar(count);
	}

	uint32_t plateCount = 0;
	r.scalar(plateCount);
	if (!r.good() || plateCount > kMaxPlateCount) {
		LOG_ERROR(World, "loadPlanet: %s has invalid plate count %u",
			path.string().c_str(), plateCount);
		return nullptr;
	}
	world->plates.resize(plateCount);
	for (PlateInfo& plate : world->plates) {
		uint8_t continental = 0;
		r.scalar(plate.eulerPole.x);
		r.scalar(plate.eulerPole.y);
		r.scalar(plate.eulerPole.z);
		r.scalar(plate.angularSpeed);
		r.scalar(continental);
		plate.isContinental = continental != 0;
	}

	uint32_t tileCount = 0;
	r.scalar(tileCount);
	const uint32_t expectedTiles = 10u * n * n;
	if (!r.good() || tileCount != expectedTiles) {
		LOG_ERROR(World, "loadPlanet: %s tile count %u does not match subdivision %u (expected %u)",
			path.string().c_str(), tileCount, n, expectedTiles);
		return nullptr;
	}

	world->data.allocate(tileCount);
	forEachFieldArray(world->data, [&](WorldField field, auto& arr) {
		if (world->validFields & static_cast<uint32_t>(field)) {
			r.span(arr);
		}
	});
	if (!r.good()) {
		LOG_ERROR(World, "loadPlanet: %s truncated in field arrays", path.string().c_str());
		return nullptr;
	}
	if (in.peek() != std::istream::traits_type::eof()) {
		LOG_ERROR(World, "loadPlanet: %s has trailing data after field arrays",
			path.string().c_str());
		return nullptr;
	}

	const uint64_t recomputed = computeWorldDataHash(world->validFields, world->data);
	if (recomputed != world->worldHash) {
		LOG_ERROR(World, "loadPlanet: %s hash mismatch (stored %llx, recomputed %llx)",
			path.string().c_str(),
			static_cast<unsigned long long>(world->worldHash),
			static_cast<unsigned long long>(recomputed));
		return nullptr;
	}

	world->derived = derive(p);
	world->grid = std::make_shared<const SphereGrid>(n);

	LOG_INFO(World, "loadPlanet: loaded %s (%u tiles, %u plates, fields 0x%x)",
		path.string().c_str(), tileCount, plateCount, world->validFields);
	return world;
}

} // namespace worldgen

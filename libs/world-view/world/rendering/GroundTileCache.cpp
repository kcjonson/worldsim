#include "world/rendering/GroundTileCache.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>
#include <utils/Log.h>
#include <utils/ResourcePath.h>
#include <utils/WorldHash.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace worldview {

	namespace {

		namespace fs = std::filesystem;

		constexpr uint32_t kMagic = 0x54475357U; // "WSGT" read as little-endian bytes
		constexpr uint32_t kFormatVersion = 2;	 // the file layout below; part of the key

		std::optional<std::vector<uint8_t>> readBytes(const fs::path& path) {
			std::error_code	 ec;
			const uintmax_t	 size = fs::file_size(path, ec);
			std::ifstream	 file(path, std::ios::binary);
			if (ec || !file) {
				return std::nullopt;
			}
			std::vector<uint8_t> bytes(static_cast<size_t>(size));
			file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			if (static_cast<size_t>(file.gcount()) != bytes.size()) {
				return std::nullopt;
			}
			return bytes;
		}

		// Catches a truncated or damaged file, not tampering: a word at a time, so 11 MB costs a few ms.
		uint64_t checksum(std::span<const uint8_t> bytes) {
			uint64_t h = 0x9E3779B97F4A7C15ULL ^ bytes.size();
			size_t	 i = 0;
			for (; i + 8 <= bytes.size(); i += 8) {
				uint64_t word = 0;
				std::memcpy(&word, bytes.data() + i, 8);
				h = (h ^ word) * 0xBF58476D1CE4E5B9ULL;
				h ^= h >> 31;
			}
			for (; i < bytes.size(); ++i) {
				h = (h ^ bytes[i]) * 0x94D049BB133111EBULL;
			}
			return h ^ (h >> 29);
		}

		class Writer {
		  public:
			template <typename T> void put(const T& value) {
				static_assert(std::is_trivially_copyable_v<T>);
				const auto* p = reinterpret_cast<const uint8_t*>(&value);
				bytes.insert(bytes.end(), p, p + sizeof(T));
			}
			void putBytes(std::span<const uint8_t> data) { bytes.insert(bytes.end(), data.begin(), data.end()); }
			void putText(std::string_view text) {
				put(static_cast<uint32_t>(text.size()));
				bytes.insert(bytes.end(), text.begin(), text.end());
			}

			std::vector<uint8_t> bytes;
		};

		// Reads fields in order; any read past the end clears ok and returns zeros from then on.
		class Reader {
		  public:
			explicit Reader(std::span<const uint8_t> data)
				: bytes(data) {}

			template <typename T> T get() {
				static_assert(std::is_trivially_copyable_v<T>);
				T value{};
				if (const std::span<const uint8_t> raw = take(sizeof(T)); !raw.empty()) {
					std::memcpy(&value, raw.data(), sizeof(T));
				}
				return value;
			}
			std::span<const uint8_t> take(size_t count) {
				if (!ok || count > bytes.size() - at) {
					ok = false;
					return {};
				}
				const std::span<const uint8_t> out = bytes.subspan(at, count);
				at += count;
				return out;
			}
			std::string getText() {
				const std::span<const uint8_t> raw = take(get<uint32_t>());
				return {raw.begin(), raw.end()};
			}
			[[nodiscard]] bool atEnd() const { return ok && at == bytes.size(); }

			bool ok = true;

		  private:
			std::span<const uint8_t> bytes;
			size_t					 at = 0;
		};

		void putSample(Writer& w, const GroundMipSample& s) {
			w.put(static_cast<int32_t>(s.renderSize));
			w.put(static_cast<int32_t>(s.supersample));
			w.put(static_cast<uint64_t>(s.vertexCount));
			w.put(static_cast<uint64_t>(s.meshCount));
			w.put(s.mean);
			w.put(s.contrast);
			w.put(s.referenceContrast);
			w.put(s.lockShift);
			w.put(s.heightSpread);
		}

		GroundMipSample getSample(Reader& r) {
			GroundMipSample s;
			s.renderSize = r.get<int32_t>();
			s.supersample = r.get<int32_t>();
			s.vertexCount = static_cast<size_t>(r.get<uint64_t>());
			s.meshCount = static_cast<size_t>(r.get<uint64_t>());
			s.mean = r.get<glm::vec3>();
			s.contrast = r.get<float>();
			s.referenceContrast = r.get<float>();
			s.lockShift = r.get<glm::vec3>();
			s.heightSpread = r.get<float>();
			return s;
		}

	} // namespace

	GroundTileCache::GroundTileCache(std::filesystem::path newDirectory)
		: directory(std::move(newDirectory)) {}

	std::filesystem::path GroundTileCache::defaultDirectory() {
		return Foundation::getExecutableDir() / "cache" / "ground";
	}

	std::filesystem::path GroundTileCache::pathFor(const std::string& defName) const {
		constexpr size_t kMaxStem = 48;
		std::string		 stem;
		for (const char c : defName.substr(0, kMaxStem)) {
			const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
			stem += safe ? c : '_';
		}
		const uint64_t h = foundation::hashBytes(defName.data(), defName.size());
		char		   suffix[16];
		std::snprintf(suffix, sizeof(suffix), "-%08x", static_cast<uint32_t>(h ^ (h >> 32)));
		return directory / (stem + suffix + ".gtile");
	}

	std::optional<uint64_t> GroundTileCache::keyFor(const assets::AssetDefinition& def, int maxRenderSize) {
		uint64_t   h = foundation::kFnvOffset;
		const auto mix = [&h](const auto& value) { h = foundation::hashBytes(&value, sizeof(value), h); };
		const auto mixBytes = [&h, &mix](std::span<const uint8_t> data) {
			mix(static_cast<uint64_t>(data.size()));
			h = foundation::hashBytes(data.data(), data.size(), h);
		};
		const auto mixText = [&mixBytes](std::string_view text) {
			mixBytes({reinterpret_cast<const uint8_t*>(text.data()), text.size()});
		};

		mix(kFormatVersion);
		mix(GroundTileBaker::kVersion);
		mixText(def.defName);
		mixText(def.ground.seedGroup);
		mix(def.variantCount);
		mix(def.ground.periodMeters);
		mix(def.ground.pixelsPerMeter);
		mix(maxRenderSize);
		mixText(def.generatorName);
		// The params the registry parsed at load, which the generator runs, not the XML as it is on disk now.
		const std::vector<std::pair<std::string, std::string>> params = def.params.sorted();
		mix(static_cast<uint64_t>(params.size()));
		for (const auto& [name, value] : params) {
			mixText(name);
			mixText(value);
		}
		if (def.isLuaGenerator()) {
			// The generator reads its script from disk on every run, so the script is keyed as it is now.
			const fs::path script = assets::AssetRegistry::Get().resolveScriptPath(def);
			const std::optional<std::vector<uint8_t>> bytes = script.empty() ? std::nullopt : readBytes(script);
			if (!bytes.has_value()) {
				LOG_WARNING(
					Engine, "GroundTileCache: can't read %s's script (%s); not caching it", def.defName.c_str(), script.string().c_str()
				);
				return std::nullopt;
			}
			mixBytes(*bytes);
		}
		return h;
	}

	std::optional<GroundTileBake> GroundTileCache::load(const assets::AssetDefinition& def, uint64_t key) const {
		const auto	   start = std::chrono::steady_clock::now();
		const fs::path path = pathFor(def.defName);
		std::error_code ec;
		if (!fs::exists(path, ec)) {
			return std::nullopt;
		}
		const std::optional<std::vector<uint8_t>> bytes = readBytes(path);
		const auto damaged = [&def, &path](const char* why) {
			LOG_WARNING(Engine, "GroundTileCache: %s's cached bake %s is %s; baking again", def.defName.c_str(), path.string().c_str(), why);
			return std::nullopt;
		};
		if (!bytes.has_value() || bytes->size() < sizeof(uint64_t)) {
			return damaged("unreadable");
		}
		const std::span<const uint8_t> body(bytes->data(), bytes->size() - sizeof(uint64_t));
		uint64_t					   storedChecksum = 0;
		std::memcpy(&storedChecksum, bytes->data() + body.size(), sizeof(uint64_t));

		Reader r(body);
		if (r.get<uint32_t>() != kMagic) {
			return damaged("not a ground bake");
		}
		if (r.get<uint64_t>() != key) {
			LOG_INFO(Engine, "GroundTileCache: %s changed since its cached bake; baking again", def.defName.c_str());
			return std::nullopt;
		}
		if (checksum(body) != storedChecksum) {
			return damaged("damaged (checksum)");
		}

		const int	   layer = GroundTileBaker::layerSize(def);
		const int	   levels = GroundTileBaker::mipCount(layer);
		const uint32_t variants = def.variantCount;
		if (r.getText() != def.defName || r.get<int32_t>() != layer || r.get<uint32_t>() != variants || r.get<int32_t>() != levels) {
			return damaged("for another def or shape");
		}

		GroundTileBake bake;
		bake.defName = def.defName;
		bake.periodMeters = def.ground.periodMeters;
		bake.pixelsPerMeter = def.ground.pixelsPerMeter;
		bake.layerSize = layer;
		bake.variantCount = variants;
		bake.fromCache = true;
		for (uint32_t v = 0; v < variants && r.ok; ++v) {
			GroundVariantBake& variant = bake.variants.emplace_back();
			variant.variant = r.get<uint32_t>();
			variant.seed = r.get<uint32_t>();
			for (int level = 0; level < levels && r.ok; ++level) {
				variant.samples.push_back(getSample(r));
				const int					   size = std::max(1, layer >> level);
				const std::span<const uint8_t> rgba = r.take(static_cast<size_t>(size) * static_cast<size_t>(size) * 4);
				variant.mips.push_back({.size = size, .rgba = {rgba.begin(), rgba.end()}});
			}
		}
		if (!r.atEnd()) {
			return damaged("the wrong size");
		}
		bake.metrics = GroundTileBaker::measure(bake.variants);
		bake.totalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		return bake;
	}

	bool GroundTileCache::store(const GroundTileBake& bake, uint64_t key) const {
		Writer w;
		w.put(kMagic);
		w.put(key);
		w.putText(bake.defName);
		w.put(static_cast<int32_t>(bake.layerSize));
		w.put(static_cast<uint32_t>(bake.variants.size()));
		w.put(static_cast<int32_t>(bake.variants.empty() ? 0 : bake.variants.front().mips.size()));
		for (const GroundVariantBake& variant : bake.variants) {
			w.put(variant.variant);
			w.put(variant.seed);
			for (size_t level = 0; level < variant.mips.size(); ++level) {
				putSample(w, variant.samples[level]);
				w.putBytes(variant.mips[level].rgba);
			}
		}
		w.put(checksum(w.bytes));

		const fs::path	path = pathFor(bake.defName);
		const fs::path	temporary = fs::path(path).concat(".tmp");
		std::error_code ec;
		fs::create_directories(directory, ec);
		{
			std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
			file.write(reinterpret_cast<const char*>(w.bytes.data()), static_cast<std::streamsize>(w.bytes.size()));
			if (!file) {
				LOG_WARNING(Engine, "GroundTileCache: can't write %s; %s will bake again next run", temporary.string().c_str(), bake.defName.c_str());
				fs::remove(temporary, ec);
				return false;
			}
		}
		fs::rename(temporary, path, ec);
		if (ec) {
			LOG_WARNING(Engine, "GroundTileCache: can't replace %s (%s)", path.string().c_str(), ec.message().c_str());
			fs::remove(temporary, ec);
			return false;
		}
		return true;
	}

} // namespace worldview

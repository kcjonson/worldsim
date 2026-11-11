#pragma once

// Baked ground tile chains kept on disk (docs/technical/ground-textures.md, "Baker"), so a run whose
// ground defs haven't changed skips the bake. One file a def and key (pathFor), holding the key it was
// baked under, every variant's mips, and the samples the report measures. A def whose key has moved
// misses, since its file has another name, and a file whose key, shape or checksum doesn't match is
// ignored. Bakes under other keys stay beside it, so two builds that key a def differently don't
// overwrite each other's; a store evicts the least recently used files (a load counts as a use) once
// the directory is past its cap.
//
// The key hashes everything the bake reads: the def as the registry parsed it (its generator params, name,
// variant count, period and density, seed group and generator name), its generator script as it is on
// disk (an @shared/ one included) and the prelude files that run before it, with LuaEngine::kApiVersion,
// or for an SVG def each variant's SVG as it is on disk, the render tile size, salal's version (git
// describe of the checkout built against, with a hash of its diff when dirty), and GroundTileBaker::kVersion
// for worldsim's own bake code. Editing the def's XML or script, then reloading, re-bakes, and so does
// editing an SVG; a part of the XML the bake never reads, like <macro> or <transition>, doesn't.
//
// Any thread. Within a process, load and store of one def must not overlap. Across processes (world-sim
// and the asset-manager share a directory) a store writes a temporary file of its own and renames it
// into place, so a reader never sees half of one.

#include "world/rendering/GroundTileBaker.h"

#include <cstdint>
#include <filesystem>
#include <optional>

namespace assets {
	struct AssetDefinition;
}

namespace worldview {

	class GroundTileCache {
	  public:
		/// A full set of nine defs is about 60 MB.
		static constexpr uintmax_t kDefaultMaxBytes = uintmax_t{1} << 30;

		/// The directory is made on the first store.
		explicit GroundTileCache(std::filesystem::path directory, uintmax_t maxBytes = kDefaultMaxBytes);

		/// The cache in <userDataDir>/cache/ground, or none when userDataDir is empty (salal logged why),
		/// so every bake runs its generator.
		[[nodiscard]] static std::optional<GroundTileCache> inUserData(const std::filesystem::path& userDataDir);

		/// The def's key, or nullopt (logged) when its script or one of its SVGs can't be read.
		[[nodiscard]] static std::optional<uint64_t> keyFor(const assets::AssetDefinition& def, int maxRenderSize);

		/// The bake stored under the key, its timings zero and its metrics measured again, with its file
		/// marked as just used; nullopt when there's none (logged) or the file is damaged (logged).
		[[nodiscard]] std::optional<GroundTileBake> load(const assets::AssetDefinition& def, uint64_t key) const;

		/// Writes the bake under the key, through a temporary file so a reader never sees half of it, then
		/// evicts. False (logged) when the directory or file can't be written.
		bool store(const GroundTileBake& bake, uint64_t key) const;

		/// <directory>/<stem>-<hash>-<key>.gtile: the name's letters, digits, _ and - with anything else as
		/// _, then a hash of the exact name, so names that differ only in case or in other characters never
		/// share a file and none reaches outside the directory, then the key in 16 hex digits.
		[[nodiscard]] std::filesystem::path pathFor(const std::string& defName, uint64_t key) const;

	  private:
		// Deletes the least recently written bakes until the rest fit in maxBytes, never `kept`, and
		// temporary files an hour old (a crashed writer's). A file another process holds is skipped.
		void evict(const std::filesystem::path& kept) const;

		std::filesystem::path directory;
		uintmax_t			  maxBytes;
	};

} // namespace worldview

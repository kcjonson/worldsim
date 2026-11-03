#pragma once

// Baked ground tile chains kept on disk (docs/technical/ground-textures.md, "Baker"), so a run whose
// ground defs haven't changed skips the bake. One file a def (pathFor), holding the key it was baked
// under, every variant's mips, and the samples the report measures. A file whose key, shape or checksum
// doesn't match is ignored, and the next store overwrites it.
//
// The key hashes everything the bake reads: the def as the registry parsed it (its generator params, name,
// variant count, period and density, seed group and generator name), its generator script as it is on
// disk (an @shared/ one included; scripts are sandboxed and can't load others), the render tile size,
// and GroundTileBaker::kVersion. Editing the def's XML or script, then reloading, re-bakes; a part of the
// XML the bake never reads, like <macro> or <transition>, doesn't. Code changes that alter the bake's
// output need kVersion bumped.
//
// Any thread; load and store of one def must not overlap.

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
		explicit GroundTileCache(std::filesystem::path directory);

		/// cache/ground beside the executable. Each build tree (worktree, config) keeps its own, so two
		/// builds whose baker code differs never read each other's bakes, and a clean build starts empty.
		[[nodiscard]] static std::filesystem::path defaultDirectory();

		/// The def's key, or nullopt (logged) when its script can't be read.
		[[nodiscard]] static std::optional<uint64_t> keyFor(const assets::AssetDefinition& def, int maxRenderSize);

		/// The bake stored under the key, its timings zero and its metrics measured again; nullopt when
		/// there's none, it was stored under another key, or the file is damaged (logged).
		[[nodiscard]] std::optional<GroundTileBake> load(const assets::AssetDefinition& def, uint64_t key) const;

		/// Writes the bake under the key, through a temporary file so a reader never sees half of it.
		/// False (logged) when the directory or file can't be written.
		bool store(const GroundTileBake& bake, uint64_t key) const;

		/// <directory>/<stem>-<hash>.gtile: the name's letters, digits, _ and - with anything else as _, then a
		/// hash of the exact name, so names that differ only in case or in other characters never share a
		/// file and none reaches outside the directory.
		[[nodiscard]] std::filesystem::path pathFor(const std::string& defName) const;

	  private:
		std::filesystem::path directory;
	};

} // namespace worldview

#pragma once

// Colonist memory tuning. Loaded from assets/config/colonists/memory.xml.
// See /docs/design/game-systems/colonists/memory.md.

#include <filesystem>
#include <string>
#include <vector>

namespace assets {

	class MemoryConfig {
	  public:
		/// The game's memory config, filled by loadWorkConfig.
		static MemoryConfig& Get();

		/// Replaces everything with `configRoot / file`: false, with problems() saying why, when the file can't be
		/// read or a value in it is missing or malformed. Errors name the file as `file` is written.
		bool loadFromFile(const std::filesystem::path& configRoot, const std::filesystem::path& file);

		/// What the file got wrong: each names the file, the element and the attribute.
		[[nodiscard]] const std::vector<std::string>& problems() const { return problemList; }

		void clear();

		[[nodiscard]] bool loaded() const { return isLoaded; }

		/// How long ago a mover may have been seen and still count as company, in game minutes. Older
		/// sightings are where someone was.
		[[nodiscard]] float recentSightingGameMinutes() const { return recentSightingMinutes; }

	  private:
		bool					 isLoaded = false;
		float					 recentSightingMinutes = 0.0F;
		std::vector<std::string> problemList;
	};

} // namespace assets

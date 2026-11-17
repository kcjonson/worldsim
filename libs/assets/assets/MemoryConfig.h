#pragma once

// Colonist memory tuning. Loaded from assets/config/colonists/memory.xml.
// See /docs/design/game-systems/colonists/memory.md.

#include <string>

namespace assets {

	class MemoryConfig {
	  public:
		/// The game's memory config, filled by loadWorkConfig.
		static MemoryConfig& Get();

		/// Replaces everything with the file's contents. False, with the reason logged, when the file
		/// can't be read or has no Memory root.
		bool loadFromFile(const std::string& xmlPath);

		void clear();

		[[nodiscard]] bool loaded() const { return isLoaded; }

		/// How long ago a mover may have been seen and still count as company, in game minutes. Older
		/// sightings are where someone was.
		[[nodiscard]] float recentSightingGameMinutes() const { return recentSightingMinutes; }

	  private:
		bool  isLoaded = false;
		float recentSightingMinutes = 0.0F;
	};

} // namespace assets

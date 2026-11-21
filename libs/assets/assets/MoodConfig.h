#pragma once

// Mood tuning: how fast mood follows its target, how each need's level turns into a mood penalty, and
// the band names. Loaded from the file with a <Mood> root in the human's folder
// (assets/world/creatures/Human/mood.xml). Each need's cost is on its own entry in needs.xml beside it
// (NeedsConfig). See /docs/design/game-systems/colonists/mood.md.

#include <filesystem>
#include <string>
#include <vector>

namespace assets {

	/// A need's penalty, 0 to 1: none at or above comfortable, rising linearly to penaltyAtWarning at
	/// warning, then linearly to 1 at an empty need.
	struct NeedPenaltyCurve {
		float comfortable = 0.0F;
		float warning = 0.0F;
		float penaltyAtWarning = 0.0F;
	};

	/// A band covers mood from `from` up to the next band's `from`.
	struct MoodBand {
		std::string name;
		float		from = 0.0F;
	};

	class MoodConfig {
	  public:
		/// The game's mood config, filled by loadWorkConfig.
		static MoodConfig& Get();

		/// Replaces everything with `configRoot / file`: false, with problems() saying why, when the file can't be
		/// read, <Mood> has an element other than Easing, NeedPenalty, and Bands, or a value in it is missing
		/// or malformed. Errors name the file as `file` is written.
		bool loadFromFile(const std::filesystem::path& configRoot, const std::filesystem::path& file);

		void clear();

		[[nodiscard]] bool loaded() const { return isLoaded; }

		[[nodiscard]] float					  pointsPerGameHour() const { return easingPointsPerGameHour; }
		[[nodiscard]] const NeedPenaltyCurve& needPenaltyCurve() const { return penaltyCurve; }
		/// Highest `from` first.
		[[nodiscard]] const std::vector<MoodBand>& bands() const { return bandList; }

		/// What the file got wrong: each names the file, the element and the attribute.
		[[nodiscard]] const std::vector<std::string>& problems() const { return problemList; }

		/// The band holding this mood: the one with the highest `from` at or below it. Valid only for a
		/// config that passed ConfigValidator::validateMood.
		[[nodiscard]] const MoodBand& bandFor(float mood) const;

	  private:
		bool					 isLoaded = false;
		float					 easingPointsPerGameHour = 0.0F;
		NeedPenaltyCurve		 penaltyCurve{};
		std::vector<MoodBand>	 bandList{};
		std::vector<std::string> problemList{};
	};

} // namespace assets

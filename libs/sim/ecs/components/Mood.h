#pragma once

// A colonist's mood, 0 to 100, and what it is made of. MoodSystem is the only writer.
// See /docs/design/game-systems/colonists/mood.md.

#include "Needs.h"
#include "Thoughts.h"

#include <assets/MoodConfig.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ecs {

	/// The top of the mood scale. A target before clamping is this plus every contribution.
	inline constexpr float kFullMood = 100.0F;

	inline constexpr size_t kNeedCount = static_cast<size_t>(NeedType::Count);

	enum class MoodCauseKind : uint8_t {
		None,
		Need,
		Thought
	};

	/// The single largest negative contributor to a colonist's target mood.
	struct MoodCause {
		MoodCauseKind kind = MoodCauseKind::None;
		/// Set when kind is Need.
		NeedType	  need = NeedType::Count;
		/// Set when kind is Thought; the id in the colonist's Thoughts record.
		ThoughtId	  thought = 0;

		bool operator==(const MoodCause& other) const = default;
	};

	struct MoodThoughtContribution {
		ThoughtId thought = 0;
		float	  amount = 0.0F;
	};

	struct Mood {
		float	  value = 0.0F;
		float	  target = 0.0F;
		MoodCause cause{};
		/// Each need's signed mood points, -cost x penalty: zero for a met need, negative below.
		std::array<float, kNeedCount>		 needContributions{};
		/// Each held thought's signed mood effect, in arrival order.
		std::vector<MoodThoughtContribution> thoughtContributions{};
		/// The game time (TimeSystem::simulationSeconds) mood last eased to.
		double								 easedAt = 0.0;
	};

	/// The most mood each need takes off, per NeedType, resolved from the config's need names.
	using NeedMoodCosts = std::array<float, kNeedCount>;

	struct ResolvedNeedMoodCosts {
		NeedMoodCosts costs{};
		/// Costed names that are no need's label, in file order.
		std::vector<std::string> unknownNeeds;
		/// Needs the config gives no cost, in NeedType order.
		std::vector<NeedType> uncostedNeeds;
	};

	/// The config's need costs by NeedType, and the names it could not place.
	[[nodiscard]] ResolvedNeedMoodCosts resolveNeedMoodCosts(const assets::MoodConfig& config);

	/// Config errors the assets library can't see: costed names that are no need, and needs with no
	/// cost. Formatted like loadWorkConfig's errors; empty when every need is costed by name.
	[[nodiscard]] std::vector<std::string> moodConfigErrors(const assets::MoodConfig& config);

	/// A need's penalty, 0 to 1, from its value on the config's curve.
	[[nodiscard]] float needPenalty(float value, const assets::NeedPenaltyCurve& curve);

	struct NeedContributionsArgs {
		const NeedsComponent&			needs;
		const NeedMoodCosts&			costs;
		const assets::NeedPenaltyCurve& curve;
	};

	/// Each need's signed mood contribution: -cost x penalty.
	[[nodiscard]] std::array<float, kNeedCount> needMoodContributions(const NeedContributionsArgs& args);

	/// kFullMood plus every need's contribution, never below 0.
	[[nodiscard]] float needsScore(const std::array<float, kNeedCount>& needContributions);

	/// The largest negative contribution among the needs and the held thoughts; needs before thoughts,
	/// then in order, on a tie. None when nothing is negative.
	[[nodiscard]] MoodCause largestNegativeContributor(const Mood& mood);

} // namespace ecs

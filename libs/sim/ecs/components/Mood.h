#pragma once

// A colonist's mood, 0 to 100, and what it is made of. MoodSystem is the only writer.
// See /docs/design/game-systems/colonists/mood.md.

#include "Needs.h"
#include "Thoughts.h"

#include <assets/MoodConfig.h>

#include <array>
#include <cstddef>
#include <cstdint>
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
		/// Each need's signed share of the target, in mood points: zero for a met need, negative below.
		std::array<float, kNeedCount>		 needContributions{};
		/// Each held thought's signed mood effect, in arrival order.
		std::vector<MoodThoughtContribution> thoughtContributions{};
		/// The game time (TimeSystem::simulationSeconds) mood last eased to.
		double								 easedAt = 0.0;
	};

	/// A need's mood weight per NeedType, resolved from the config's need names.
	using NeedMoodWeights = std::array<float, kNeedCount>;

	/// The config's need weights by NeedType. A need the config leaves out weighs nothing; a name that
	/// is no need's label is logged and ignored.
	[[nodiscard]] NeedMoodWeights resolveNeedMoodWeights(const assets::MoodConfig& config);

	/// A need's penalty, 0 to 1, from its value on the config's curve.
	[[nodiscard]] float needPenalty(float value, const assets::NeedPenaltyCurve& curve);

	struct NeedContributionsArgs {
		const NeedsComponent&			needs;
		const NeedMoodWeights&			weights;
		const assets::NeedPenaltyCurve& curve;
	};

	/// Each need's signed mood contribution: -kFullMood x weight x penalty / total weight. Their sum
	/// plus kFullMood is the needs score.
	[[nodiscard]] std::array<float, kNeedCount> needMoodContributions(const NeedContributionsArgs& args);

	/// The largest negative contribution among the needs and the held thoughts; needs before thoughts,
	/// then in order, on a tie. None when nothing is negative.
	[[nodiscard]] MoodCause largestNegativeContributor(const Mood& mood);

} // namespace ecs

#include "Mood.h"

#include <algorithm>

namespace ecs {

	float needPenalty(float value, const assets::NeedPenaltyCurve& curve) {
		if (value >= curve.comfortable) {
			return 0.0F;
		}
		if (value >= curve.warning) {
			const float intoMild = (curve.comfortable - value) / (curve.comfortable - curve.warning);
			return intoMild * curve.penaltyAtWarning;
		}
		const float intoSteep = (curve.warning - std::max(value, 0.0F)) / curve.warning;
		return curve.penaltyAtWarning + intoSteep * (1.0F - curve.penaltyAtWarning);
	}

	std::array<float, kNeedCount> needMoodContributions(const NeedsComponent& needs, const assets::NeedPenaltyCurve& curve) {
		std::array<float, kNeedCount> contributions{};
		for (size_t i = 0; i < kNeedCount; ++i) {
			contributions[i] = -needs.needs[i].moodCost * needPenalty(needs.needs[i].value, curve);
		}
		return contributions;
	}

	float needsScore(const std::array<float, kNeedCount>& needContributions) {
		float score = kFullMood;
		for (const float contribution : needContributions) {
			score += contribution;
		}
		return std::max(score, 0.0F);
	}

	MoodCause largestNegativeContributor(const Mood& mood) {
		MoodCause cause{};
		float	  largest = 0.0F;
		for (size_t i = 0; i < kNeedCount; ++i) {
			if (mood.needContributions[i] < largest) {
				largest = mood.needContributions[i];
				cause = {.kind = MoodCauseKind::Need, .need = static_cast<NeedType>(i)};
			}
		}
		for (const MoodThoughtContribution& contribution : mood.thoughtContributions) {
			if (contribution.amount < largest) {
				largest = contribution.amount;
				cause = {.kind = MoodCauseKind::Thought, .thought = contribution.thought};
			}
		}
		return cause;
	}

} // namespace ecs

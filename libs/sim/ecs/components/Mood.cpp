#include "Mood.h"

#include <utils/Log.h>

#include <algorithm>
#include <string_view>

namespace ecs {

	NeedMoodWeights resolveNeedMoodWeights(const assets::MoodConfig& config) {
		NeedMoodWeights weights{};
		for (const assets::NeedMoodWeight& needWeight : config.needWeights()) {
			const auto found = std::find_if(kNeedLabels.begin(), kNeedLabels.end(), [&needWeight](const char* label) {
				return std::string_view(label) == needWeight.need;
			});
			if (found == kNeedLabels.end()) {
				LOG_ERROR(Engine, "Mood config weights '%s', which is no need; it is ignored", needWeight.need.c_str());
				continue;
			}
			weights[static_cast<size_t>(found - kNeedLabels.begin())] = needWeight.weight;
		}
		return weights;
	}

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

	std::array<float, kNeedCount> needMoodContributions(const NeedContributionsArgs& args) {
		std::array<float, kNeedCount> contributions{};
		float						  totalWeight = 0.0F;
		for (const float weight : args.weights) {
			totalWeight += weight;
		}
		if (totalWeight <= 0.0F) {
			return contributions;
		}
		for (size_t i = 0; i < kNeedCount; ++i) {
			const float penalty = needPenalty(args.needs.needs[i].value, args.curve);
			contributions[i] = -kFullMood * args.weights[i] * penalty / totalWeight;
		}
		return contributions;
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

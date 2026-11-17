#include "Mood.h"

#include <algorithm>
#include <string_view>

namespace ecs {

	ResolvedNeedMoodWeights resolveNeedMoodWeights(const assets::MoodConfig& config) {
		ResolvedNeedMoodWeights resolved;
		std::array<bool, kNeedCount> weighted{};
		for (const assets::NeedMoodWeight& needWeight : config.needWeights()) {
			const auto found = std::find_if(kNeedLabels.begin(), kNeedLabels.end(), [&needWeight](const char* label) {
				return std::string_view(label) == needWeight.need;
			});
			if (found == kNeedLabels.end()) {
				resolved.unknownNeeds.push_back(needWeight.need);
				continue;
			}
			const auto index = static_cast<size_t>(found - kNeedLabels.begin());
			resolved.weights[index] = needWeight.weight;
			weighted[index] = true;
		}
		for (size_t i = 0; i < kNeedCount; ++i) {
			if (!weighted[i]) {
				resolved.unweightedNeeds.push_back(static_cast<NeedType>(i));
			}
		}
		return resolved;
	}

	std::vector<std::string> moodConfigErrors(const assets::MoodConfig& config) {
		const ResolvedNeedMoodWeights resolved = resolveNeedMoodWeights(config);
		std::vector<std::string>	  errors;
		for (const std::string& name : resolved.unknownNeeds) {
			errors.push_back("[Mood] Need '" + name + "' is weighted but no need has that name");
		}
		for (const NeedType need : resolved.unweightedNeeds) {
			const std::string label = needLabel(need);
			errors.push_back(
				"[Mood] Need '" + label + "' has no weight (add <Need name=\"" + label + "\" weight=\"...\"/>; 0 leaves it out of mood)"
			);
		}
		return errors;
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

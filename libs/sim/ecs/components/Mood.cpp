#include "Mood.h"

#include <algorithm>
#include <string_view>

namespace ecs {

	ResolvedNeedMoodCosts resolveNeedMoodCosts(const assets::MoodConfig& config) {
		ResolvedNeedMoodCosts		 resolved;
		std::array<bool, kNeedCount> costed{};
		for (const assets::NeedMoodCost& needCost : config.needCosts()) {
			const auto found = std::find_if(kNeedLabels.begin(), kNeedLabels.end(), [&needCost](const char* label) {
				return std::string_view(label) == needCost.need;
			});
			if (found == kNeedLabels.end()) {
				resolved.unknownNeeds.push_back(needCost.need);
				continue;
			}
			const auto index = static_cast<size_t>(found - kNeedLabels.begin());
			resolved.costs[index] = needCost.cost;
			costed[index] = true;
		}
		for (size_t i = 0; i < kNeedCount; ++i) {
			if (!costed[i]) {
				resolved.uncostedNeeds.push_back(static_cast<NeedType>(i));
			}
		}
		return resolved;
	}

	std::vector<std::string> moodConfigErrors(const assets::MoodConfig& config) {
		const ResolvedNeedMoodCosts resolved = resolveNeedMoodCosts(config);
		std::vector<std::string>	errors;
		for (const std::string& name : resolved.unknownNeeds) {
			errors.push_back("[Mood] Need '" + name + "' has a cost but no need has that name");
		}
		for (const NeedType need : resolved.uncostedNeeds) {
			const std::string label = needLabel(need);
			errors.push_back(
				"[Mood] Need '" + label + "' has no cost (add <Need name=\"" + label + "\" cost=\"...\"/>; 0 leaves it out of mood)"
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
		for (size_t i = 0; i < kNeedCount; ++i) {
			contributions[i] = -args.costs[i] * needPenalty(args.needs.needs[i].value, args.curve);
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

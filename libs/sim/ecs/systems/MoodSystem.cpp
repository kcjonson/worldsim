#include "MoodSystem.h"

#include "../World.h"
#include "../components/Thoughts.h"
#include "TimeSystem.h"

#include <algorithm>

namespace ecs {

	MoodSystem::MoodSystem(const assets::MoodConfig& moodConfig)
		: config(moodConfig) {}

	void MoodSystem::update(float /*deltaTime*/) {
		if (config.generation() != resolvedGeneration) {
			needWeights = resolveNeedMoodWeights(config);
			resolvedGeneration = config.generation();
		}

		const double now = world->getSystem<TimeSystem>().simulationSeconds();
		for (auto [colonist, needs] : world->view<NeedsComponent>()) {
			Mood*	   mood = world->getComponent<Mood>(colonist);
			const bool isNew = mood == nullptr;
			if (isNew) {
				mood = &world->addComponent<Mood>(colonist);
			}

			mood->needContributions =
				needMoodContributions({.needs = needs, .weights = needWeights, .curve = config.needPenaltyCurve()});
			float unclampedTarget = kFullMood;
			for (const float contribution : mood->needContributions) {
				unclampedTarget += contribution;
			}

			mood->thoughtContributions.clear();
			if (const Thoughts* thoughts = world->getComponent<Thoughts>(colonist); thoughts != nullptr) {
				forEachHeldThought(*thoughts, [mood, &unclampedTarget](const Thought& thought) {
					const float amount = heldMoodAmount(thought);
					mood->thoughtContributions.push_back({.thought = thought.id, .amount = amount});
					unclampedTarget += amount;
				});
			}

			mood->target = std::clamp(unclampedTarget, 0.0F, kFullMood);
			mood->cause = largestNegativeContributor(*mood);

			if (isNew) {
				mood->value = mood->target;
			} else {
				const auto	elapsedGameHours = static_cast<float>((now - mood->easedAt) / kSimulationSecondsPerGameHour);
				const float largestStep = config.pointsPerGameHour() * elapsedGameHours;
				mood->value += std::clamp(mood->target - mood->value, -largestStep, largestStep);
			}
			mood->easedAt = now;
		}
	}

} // namespace ecs

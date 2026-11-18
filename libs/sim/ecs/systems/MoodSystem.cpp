#include "MoodSystem.h"

#include "../World.h"
#include "../components/Thoughts.h"
#include "TimeSystem.h"

#include <algorithm>

namespace ecs {

	MoodSystem::MoodSystem(const assets::MoodConfig& moodConfig)
		: config(moodConfig) {}

	void MoodSystem::update(float /*deltaTime*/) {
		refreshNeedCosts();
		const double currentTime = now();
		for (auto [colonist, needs] : world->view<NeedsComponent>()) {
			Mood* mood = world->getComponent<Mood>(colonist);
			if (mood == nullptr) {
				startMood(colonist);
				continue;
			}
			refreshTarget(colonist, needs, *mood);
			const auto	elapsedGameHours = static_cast<float>((currentTime - mood->easedAt) / kSimulationSecondsPerGameHour);
			const float largestStep = config.pointsPerGameHour() * elapsedGameHours;
			mood->value += std::clamp(mood->target - mood->value, -largestStep, largestStep);
			mood->easedAt = currentTime;
		}
	}

	Mood& MoodSystem::startMood(EntityID colonist) {
		refreshNeedCosts();
		Mood& mood = world->addComponent<Mood>(colonist);
		refreshTarget(colonist, *world->getComponent<NeedsComponent>(colonist), mood);
		mood.value = mood.target;
		mood.easedAt = now();
		return mood;
	}

	void MoodSystem::refreshNeedCosts() {
		if (config.generation() != resolvedGeneration) {
			needCosts = resolveNeedMoodCosts(config).costs;
			resolvedGeneration = config.generation();
		}
	}

	void MoodSystem::refreshTarget(EntityID colonist, const NeedsComponent& needs, Mood& mood) const {
		mood.needContributions = needMoodContributions({.needs = needs, .costs = needCosts, .curve = config.needPenaltyCurve()});
		float unclampedTarget = needsScore(mood.needContributions);

		mood.thoughtContributions.clear();
		if (const Thoughts* thoughts = world->getComponent<Thoughts>(colonist); thoughts != nullptr) {
			forEachHeldThought(*thoughts, [&mood, &unclampedTarget](const Thought& thought) {
				const float amount = heldMoodAmount(thought);
				mood.thoughtContributions.push_back({.thought = thought.id, .amount = amount});
				unclampedTarget += amount;
			});
		}

		mood.target = std::clamp(unclampedTarget, 0.0F, kFullMood);
		mood.cause = largestNegativeContributor(mood);
	}

	double MoodSystem::now() const {
		return world->getSystem<TimeSystem>().simulationSeconds();
	}

} // namespace ecs

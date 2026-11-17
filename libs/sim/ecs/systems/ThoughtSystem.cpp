#include "ThoughtSystem.h"

#include "../World.h"
#include "TimeSystem.h"

#include <algorithm>
#include <cassert>
#include <utility>

namespace ecs {

	namespace {
		Thought* findHeld(Thoughts& thoughts, ThoughtEventTypeId eventType, const ThoughtSubject& subject) {
			for (const ThoughtId id : thoughts.heldIds) {
				Thought* thought = findThought(thoughts, id);
				if (thought->eventType == eventType && thought->subject == subject) {
					return thought;
				}
			}
			return nullptr;
		}

		double expiryFor(const MoodEffect& effect, double from) {
			if (effect.lasts != MoodEffectLasts::ForDuration) {
				return 0.0;
			}
			return from + static_cast<double>(effect.durationGameHours) * kSimulationSecondsPerGameHour;
		}
	} // namespace

	void ThoughtSystem::update(float /*deltaTime*/) {
		const double now = world->getSystem<TimeSystem>().simulationSeconds();
		expiredScratch.clear();
		for (auto [colonist, thoughts] : world->view<Thoughts>()) {
			for (const ThoughtId id : thoughts.heldIds) {
				const Thought* thought = findThought(thoughts, id);
				if (thought->moodEffect->lasts == MoodEffectLasts::ForDuration && thought->expiresAt <= now) {
					expiredScratch.push_back({.colonist = colonist, .thought = id});
				}
			}
		}
		// Ended after the walk: an ended observer may create thoughts, adding a Thoughts under the view.
		for (const ThoughtHandle expired : expiredScratch) {
			Thoughts& thoughts = *world->getComponent<Thoughts>(expired.colonist);
			Thought&  thought = *findThought(thoughts, expired.thought);
			endHeld(expired.colonist, thoughts, thought, thought.expiresAt);
		}
	}

	ThoughtHandle ThoughtSystem::create(const CreateThoughtArgs& args) {
		assert(world->isAlive(args.colonist) && "A thought needs a living colonist");
		const double now = world->getSystem<TimeSystem>().simulationSeconds();

		Thoughts* thoughts = world->getComponent<Thoughts>(args.colonist);
		if (thoughts == nullptr) {
			thoughts = &world->addComponent<Thoughts>(args.colonist);
		}

		if (args.moodEffect.has_value()) {
			if (Thought* held = findHeld(*thoughts, args.eventType, args.subject); held != nullptr) {
				if (args.stacks) {
					++held->count;
				}
				held->feeling = args.feeling;
				held->moodEffect = args.moodEffect;
				held->importance = args.importance;
				held->lastRepeatedAt = now;
				held->expiresAt = expiryFor(*args.moodEffect, now);
				// Observers get a copy: one that creates a thought of its own may grow the record under them.
				const Thought repeated = *held;
				for (const auto& observer : arrivedObservers) {
					observer(args.colonist, repeated, ThoughtArrival::Repeat);
				}
				return {.colonist = args.colonist, .thought = repeated.id};
			}
		}

		const Thought thought{
			.id = thoughts->nextId++,
			.eventType = args.eventType,
			.subject = args.subject,
			.feeling = args.feeling,
			.moodEffect = args.moodEffect,
			.importance = args.importance,
			.arrivedAt = now,
			.lastRepeatedAt = now,
			.expiresAt = args.moodEffect.has_value() ? expiryFor(*args.moodEffect, now) : 0.0,
		};
		if (thought.moodEffect.has_value()) {
			thoughts->heldIds.push_back(thought.id);
		}
		thoughts->record.push_back(thought);

		for (const auto& observer : arrivedObservers) {
			observer(args.colonist, thought, ThoughtArrival::New);
		}
		return {.colonist = args.colonist, .thought = thought.id};
	}

	bool ThoughtSystem::end(ThoughtHandle handle) {
		Thoughts* thoughts = world->getComponent<Thoughts>(handle.colonist);
		if (thoughts == nullptr) {
			return false;
		}
		Thought* thought = findThought(*thoughts, handle.thought);
		if (thought == nullptr || !thought->isHeld()) {
			return false;
		}
		endHeld(handle.colonist, *thoughts, *thought, world->getSystem<TimeSystem>().simulationSeconds());
		return true;
	}

	bool ThoughtSystem::endByCause(EntityID colonist, ThoughtEventTypeId eventType, const ThoughtSubject& subject) {
		Thoughts* thoughts = world->getComponent<Thoughts>(colonist);
		if (thoughts == nullptr) {
			return false;
		}
		Thought* thought = findHeld(*thoughts, eventType, subject);
		if (thought == nullptr) {
			return false;
		}
		endHeld(colonist, *thoughts, *thought, world->getSystem<TimeSystem>().simulationSeconds());
		return true;
	}

	void ThoughtSystem::addArrivedObserver(ThoughtArrivedObserver observer) {
		arrivedObservers.push_back(std::move(observer));
	}

	void ThoughtSystem::addEndedObserver(ThoughtEndedObserver observer) {
		endedObservers.push_back(std::move(observer));
	}

	void ThoughtSystem::endHeld(EntityID colonist, Thoughts& thoughts, Thought& thought, double endedAt) {
		thought.endedAt = endedAt;
		thoughts.heldIds.erase(std::find(thoughts.heldIds.begin(), thoughts.heldIds.end(), thought.id));
		const Thought ended = thought;
		for (const auto& observer : endedObservers) {
			observer(colonist, ended);
		}
	}

} // namespace ecs

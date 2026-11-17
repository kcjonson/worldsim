#include "ThoughtSystem.h"

#include "../World.h"
#include "TimeSystem.h"

#include <utils/Log.h>

#include <algorithm>
#include <tuple>
#include <utility>

namespace ecs {

	namespace {
		bool hasRunOut(const Thought& thought, double now) {
			return isHeld(thought) && thought.moodEffect->lasts == MoodEffectLasts::ForDuration && thought.expiresAt <= now;
		}

		/// A timed thought that has run out is no fold target; the next pass ends it.
		Thought* findHeld(Thoughts& thoughts, ThoughtEventTypeId eventType, const ThoughtSubject& subject, double now) {
			for (const ThoughtId id : thoughts.heldIds) {
				Thought* thought = findThought(thoughts, id);
				if (thought->eventType == eventType && thought->subject == subject && !hasRunOut(*thought, now)) {
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
				if (hasRunOut(*thought, now)) {
					expiredScratch.push_back({.expiresAt = thought->expiresAt, .colonist = colonist, .thought = id});
				}
			}
		}
		std::sort(expiredScratch.begin(), expiredScratch.end(), [](const ExpiredThought& left, const ExpiredThought& right) {
			return std::tie(left.expiresAt, left.colonist, left.thought) < std::tie(right.expiresAt, right.colonist, right.thought);
		});
		// Ended after the walk, and each one looked up again: an ended observer may create or end thoughts or
		// destroy the colonist.
		for (const ExpiredThought& expired : expiredScratch) {
			if (!world->isAlive(expired.colonist)) {
				continue;
			}
			Thoughts* thoughts = world->getComponent<Thoughts>(expired.colonist);
			if (thoughts == nullptr) {
				continue;
			}
			Thought* thought = findThought(*thoughts, expired.thought);
			if (thought == nullptr || !hasRunOut(*thought, now)) {
				continue;
			}
			endHeld(expired.colonist, *thoughts, *thought, thought->expiresAt);
		}
	}

	ThoughtHandle ThoughtSystem::create(const CreateThoughtArgs& args) {
		if (!world->isAlive(args.colonist)) {
			LOG_ERROR(
				Engine,
				"ThoughtSystem::create: colonist %llu is not alive; thought type %u dropped",
				static_cast<unsigned long long>(args.colonist),
				args.eventType
			);
			return {};
		}
		const double now = world->getSystem<TimeSystem>().simulationSeconds();

		Thoughts* thoughts = world->getComponent<Thoughts>(args.colonist);
		if (thoughts == nullptr) {
			thoughts = &world->addComponent<Thoughts>(args.colonist);
		}

		if (args.moodEffect.has_value()) {
			if (Thought* held = findHeld(*thoughts, args.eventType, args.subject, now); held != nullptr) {
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
				for (size_t index = 0, count = arrivedObservers.size(); index < count; ++index) {
					arrivedObservers[index](args.colonist, repeated, ThoughtArrival::Repeat);
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

		for (size_t index = 0, count = arrivedObservers.size(); index < count; ++index) {
			arrivedObservers[index](args.colonist, thought, ThoughtArrival::New);
		}
		return {.colonist = args.colonist, .thought = thought.id};
	}

	bool ThoughtSystem::end(ThoughtHandle handle) {
		if (!world->isAlive(handle.colonist)) {
			return false;
		}
		Thoughts* thoughts = world->getComponent<Thoughts>(handle.colonist);
		if (thoughts == nullptr) {
			return false;
		}
		Thought* thought = findThought(*thoughts, handle.thought);
		if (thought == nullptr || !isHeld(*thought)) {
			return false;
		}
		endHeld(handle.colonist, *thoughts, *thought, world->getSystem<TimeSystem>().simulationSeconds());
		return true;
	}

	bool ThoughtSystem::endByCause(EntityID colonist, ThoughtEventTypeId eventType, const ThoughtSubject& subject) {
		if (!world->isAlive(colonist)) {
			return false;
		}
		Thoughts* thoughts = world->getComponent<Thoughts>(colonist);
		if (thoughts == nullptr) {
			return false;
		}
		const double now = world->getSystem<TimeSystem>().simulationSeconds();
		Thought*	 thought = findHeld(*thoughts, eventType, subject, now);
		if (thought == nullptr) {
			return false;
		}
		endHeld(colonist, *thoughts, *thought, now);
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
		for (size_t index = 0, count = endedObservers.size(); index < count; ++index) {
			endedObservers[index](colonist, ended);
		}
	}

} // namespace ecs

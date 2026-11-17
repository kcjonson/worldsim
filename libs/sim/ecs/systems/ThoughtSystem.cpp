#include "ThoughtSystem.h"

#include "../World.h"
#include "TimeSystem.h"

#include <conditions/ConditionVocabulary.h>

#include <utils/Log.h>

#include <algorithm>
#include <tuple>
#include <utility>

namespace ecs {

	namespace {
		bool hasRunOut(const Thought& thought, double now) {
			return isHeld(thought) && thought.moodEffect->lasts == MoodEffectLasts::ForDuration && thought.expiresAt <= now;
		}

		bool isCheckDue(const Thought& thought, double now) {
			return isHeld(thought) && thought.moodEffect->lasts == MoodEffectLasts::WhileCauseHolds && thought.nextCheckAt <= now;
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

		EntityID targetOf(const ThoughtSubject& subject) {
			const bool isEntity = subject.kind == ThoughtSubjectKind::Colonist || subject.kind == ThoughtSubjectKind::Entity;
			return isEntity ? subject.entity : kInvalidEntity;
		}

		bool holds(
			const conditions::ConditionVocabulary&		vocabulary,
			const std::optional<conditions::Condition>& condition,
			const conditions::ConditionContext&			context
		) {
			return !condition.has_value() || vocabulary.evaluate(*condition, context) == conditions::Truth::True;
		}
	} // namespace

	ThoughtSystem::ThoughtSystem(const ThoughtTypes& types, uint32_t randomSeed)
		: thoughtTypes(types),
		  random(randomSeed) {}

	void ThoughtSystem::update(float /*deltaTime*/) {
		const double now = world->getSystem<TimeSystem>().simulationSeconds();
		dueScratch.clear();
		for (auto [colonist, thoughts] : world->view<Thoughts>()) {
			for (const ThoughtId id : thoughts.heldIds) {
				const Thought* thought = findThought(thoughts, id);
				if (hasRunOut(*thought, now)) {
					dueScratch.push_back({.dueAt = thought->expiresAt, .colonist = colonist, .thought = id});
				} else if (isCheckDue(*thought, now)) {
					dueScratch.push_back({.dueAt = thought->nextCheckAt, .colonist = colonist, .thought = id});
				}
			}
		}
		std::sort(dueScratch.begin(), dueScratch.end(), [](const DueThought& left, const DueThought& right) {
			return std::tie(left.dueAt, left.colonist, left.thought) < std::tie(right.dueAt, right.colonist, right.thought);
		});
		// Handled after the walk, and each one looked up again: an ended observer, or an <ends> thought, may
		// create or end thoughts or destroy the colonist.
		for (const DueThought& due : dueScratch) {
			if (!world->isAlive(due.colonist)) {
				continue;
			}
			Thoughts* thoughts = world->getComponent<Thoughts>(due.colonist);
			if (thoughts == nullptr) {
				continue;
			}
			Thought* thought = findThought(*thoughts, due.thought);
			if (thought == nullptr) {
				continue;
			}
			if (hasRunOut(*thought, now)) {
				endHeld(due.colonist, *thoughts, *thought, thought->expiresAt);
			} else if (isCheckDue(*thought, now)) {
				if (causeHasPassed(due.colonist, *thought)) {
					endHeld(due.colonist, *thoughts, *thought, now);
				} else {
					thought->nextCheckAt = now + thoughtTypes.whileCheckSimulationSeconds();
				}
			}
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
		if (args.eventType >= thoughtTypes.typeCount()) {
			LOG_ERROR(Engine, "ThoughtSystem::create: no thought type %u", args.eventType);
			return {};
		}
		const ThoughtType&				   type = thoughtTypes.type(args.eventType);
		const conditions::ConditionContext context{.world = *world, .colonist = args.colonist, .target = targetOf(args.subject)};
		const std::optional<uint16_t>	   typeFeeling = chooseFeeling(type, context);
		if (!typeFeeling.has_value()) {
			LOG_DEBUG(
				Engine,
				"Thought '%s' on colonist %llu: none of its feelings applies, nothing recorded",
				type.name.c_str(),
				static_cast<unsigned long long>(args.colonist)
			);
			return {};
		}
		const ThoughtFeeling& feeling = type.feelings[*typeFeeling];
		const double		  now = world->getSystem<TimeSystem>().simulationSeconds();

		Thoughts* thoughts = world->getComponent<Thoughts>(args.colonist);
		if (thoughts == nullptr) {
			thoughts = &world->addComponent<Thoughts>(args.colonist);
		}

		std::optional<uint16_t> lastLine;
		if (const auto found = thoughts->lastLines.find(args.eventType); found != thoughts->lastLines.end()) {
			lastLine = found->second;
		}
		const std::optional<uint16_t> line = chooseLine(type, feeling.feeling, context, args.subject, lastLine);
		if (line.has_value()) {
			thoughts->lastLines[args.eventType] = *line;
		}
		const double nextCheckAt = now + thoughtTypes.whileCheckSimulationSeconds();

		if (feeling.moodEffect.has_value()) {
			if (Thought* held = findHeld(*thoughts, args.eventType, args.subject, now); held != nullptr) {
				if (type.stacks) {
					++held->count;
				}
				held->feeling = feeling.feeling;
				held->typeFeeling = *typeFeeling;
				held->line = line;
				held->moodEffect = feeling.moodEffect;
				held->importance = type.importance;
				held->lastRepeatedAt = now;
				held->expiresAt = expiryFor(*feeling.moodEffect, now);
				held->nextCheckAt = nextCheckAt;
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
			.feeling = feeling.feeling,
			.typeFeeling = *typeFeeling,
			.line = line,
			.moodEffect = feeling.moodEffect,
			.importance = type.importance,
			.arrivedAt = now,
			.lastRepeatedAt = now,
			.expiresAt = feeling.moodEffect.has_value() ? expiryFor(*feeling.moodEffect, now) : 0.0,
			.nextCheckAt = nextCheckAt,
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

	std::optional<uint16_t> ThoughtSystem::chooseFeeling(const ThoughtType& type, const conditions::ConditionContext& context) const {
		for (std::size_t index = 0; index < type.feelings.size(); ++index) {
			if (holds(thoughtTypes.vocabulary(), type.feelings[index].when, context)) {
				return static_cast<uint16_t>(index);
			}
		}
		return std::nullopt;
	}

	std::optional<uint16_t> ThoughtSystem::chooseLine(
		const ThoughtType&					type,
		FeelingId							feeling,
		const conditions::ConditionContext& context,
		const ThoughtSubject&				subject,
		std::optional<uint16_t>				lastLine
	) {
		const bool subjectHasName =
			std::ranges::any_of(type.lines, &ThoughtLine::namesSubject) && thoughtSubjectName(*world, subject).has_value();
		lineScratch.clear();
		// Lines are most specific first: the first specificity with a line that fits is the one picked from.
		for (std::size_t index = 0; index < type.lines.size(); ++index) {
			const ThoughtLine& line = type.lines[index];
			if (!lineScratch.empty() && line.specificity < type.lines[lineScratch.front()].specificity) {
				break;
			}
			const bool fits = (!line.feeling.has_value() || *line.feeling == feeling) && (!line.namesSubject || subjectHasName) &&
							  holds(thoughtTypes.vocabulary(), line.when, context);
			if (fits) {
				lineScratch.push_back(static_cast<uint16_t>(index));
			}
		}
		if (lineScratch.empty()) {
			return std::nullopt;
		}
		if (lineScratch.size() > 1 && lastLine.has_value()) {
			std::erase(lineScratch, *lastLine);
		}
		return lineScratch[random() % lineScratch.size()];
	}

	bool ThoughtSystem::causeHasPassed(EntityID colonist, const Thought& thought) const {
		const std::optional<conditions::Condition>& condition =
			thoughtTypes.type(thought.eventType).feelings[thought.typeFeeling].whileCondition;
		if (!condition.has_value()) {
			return false;
		}
		const conditions::ConditionContext context{.world = *world, .colonist = colonist, .target = targetOf(thought.subject)};
		return thoughtTypes.vocabulary().evaluate(*condition, context) == conditions::Truth::False;
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
		const std::optional<ThoughtEventTypeId> endsWith = thoughtTypes.type(ended.eventType).ends;
		if (endsWith.has_value() && world->isAlive(colonist)) {
			create({.colonist = colonist, .eventType = *endsWith, .subject = ended.subject});
		}
	}

} // namespace ecs

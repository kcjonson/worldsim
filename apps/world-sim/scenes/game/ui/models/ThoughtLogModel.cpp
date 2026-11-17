#include "ThoughtLogModel.h"

#include <ecs/World.h>
#include <ecs/systems/ThoughtSystem.h>
#include <ecs/systems/TimeSystem.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace world_sim {

	namespace {

		constexpr long kMinutesPerHour = 60;
		constexpr long kMinutesPerDay = 24 * kMinutesPerHour;
		constexpr long kHoursShownBeforeDays = 48;

		std::string counted(long count, const char* one, const char* many) {
			return std::format("{} {}", count, count == 1 ? one : many);
		}

		std::string effectLength(const ecs::ThoughtTypes& types, const ecs::Thought& thought) {
			const ecs::MoodEffect& effect = *thought.moodEffect;
			if (effect.lasts == ecs::MoodEffectLasts::WhileCauseHolds) {
				return "while " + types.type(thought.eventType).feelings[thought.typeFeeling].cause;
			}
			return gameDurationText(effect.durationGameHours);
		}

	} // namespace

	GameClock gameClock(const ecs::World& world) {
		const ecs::TimeSystem& time = world.getSystem<ecs::TimeSystem>();
		return {.simulationSeconds = time.simulationSeconds(), .day = time.day(), .timeOfDay = time.timeOfDay()};
	}

	std::string moodAmountText(float amount) {
		const float tenths = std::round(amount * 10.0F) / 10.0F;
		if (tenths == 0.0F) {
			return "0";
		}
		if (tenths == std::round(tenths)) {
			return std::format("{:+}", static_cast<int>(tenths));
		}
		return std::format("{:+.1f}", tenths);
	}

	std::string gameDurationText(double gameHours) {
		const auto minutes = static_cast<long>(std::lround(gameHours * kMinutesPerHour));
		if (minutes < kMinutesPerHour) {
			return std::format("{} min", minutes);
		}
		if (minutes % kMinutesPerDay == 0) {
			return counted(minutes / kMinutesPerDay, "day", "days");
		}
		if (minutes % kMinutesPerHour == 0) {
			return counted(minutes / kMinutesPerHour, "hour", "hours");
		}
		return std::format("{:.1f} hours", static_cast<double>(minutes) / kMinutesPerHour);
	}

	std::string timeLeftText(double gameHours) {
		const long minutes = std::max(1L, static_cast<long>(std::ceil(gameHours * kMinutesPerHour)));
		if (minutes < kMinutesPerHour) {
			return std::format("{} min left", minutes);
		}
		const long hours = (minutes + kMinutesPerHour - 1) / kMinutesPerHour;
		if (hours < kHoursShownBeforeDays) {
			return counted(hours, "hour", "hours") + " left";
		}
		return counted((hours + 23) / 24, "day", "days") + " left";
	}

	std::string gameDateText(const GameClock& clock, double simulationSeconds) {
		const double nowMinute =
			static_cast<double>(clock.day - 1) * kMinutesPerDay + static_cast<double>(clock.timeOfDay) * kMinutesPerHour;
		const double elapsedMinutes = (clock.simulationSeconds - simulationSeconds) * kMinutesPerHour / ecs::kSimulationSecondsPerGameHour;
		const long	 minute = std::max(0L, std::lround(nowMinute - elapsedMinutes));
		const long	 ofDay = minute % kMinutesPerDay;
		return std::format("Day {}, {:02}:{:02}", minute / kMinutesPerDay + 1, ofDay / kMinutesPerHour, ofDay % kMinutesPerHour);
	}

	std::string thoughtEventText(const ecs::ThoughtTypes& types, const ecs::Thought& thought) {
		const std::string& label = types.type(thought.eventType).label;
		return thought.count > 1 ? std::format("{} x{}", label, thought.count) : label;
	}

	std::string thoughtSummary(const ecs::ThoughtTypes& types, const ecs::Thought& thought) {
		std::string text = thoughtEventText(types, thought) + ". " + types.feeling(thought.feeling).label + ".";
		if (thought.moodEffect.has_value()) {
			const float amount = thought.moodEffect->amount * static_cast<float>(thought.count);
			text += " " + moodAmountText(amount) + " mood, " + effectLength(types, thought);
		}
		return text;
	}

	std::vector<ThoughtLogEntry> thoughtLogEntries(const ecs::ThoughtTypes& types, const ecs::Thoughts& thoughts, const GameClock& clock) {
		std::vector<const ecs::Thought*> order;
		order.reserve(thoughts.record.size());
		for (const ecs::Thought& thought : thoughts.record) {
			order.push_back(&thought);
		}
		std::ranges::sort(order, [](const ecs::Thought* left, const ecs::Thought* right) {
			if (left->lastRepeatedAt != right->lastRepeatedAt) {
				return left->lastRepeatedAt > right->lastRepeatedAt;
			}
			return left->id > right->id;
		});

		std::vector<ThoughtLogEntry> entries;
		entries.reserve(order.size());
		for (const ecs::Thought* thought : order) {
			entries.push_back({
				.id = thought->id,
				.when = gameDateText(clock, thought->lastRepeatedAt),
				.summary = thoughtSummary(types, *thought),
				.line = ecs::renderThoughtLine(types, thoughts, *thought),
			});
		}
		return entries;
	}

	MoodBreakdown
	moodBreakdown(const ecs::ThoughtTypes& types, const ecs::Thoughts* thoughts, const ecs::Mood& mood, double simulationSeconds) {
		float needs = 0.0F;
		for (const float contribution : mood.needContributions) {
			needs += contribution;
		}
		MoodBreakdown breakdown{
			.needs = moodAmountText(needs),
			.target = std::format("{}%", static_cast<int>(std::lround(mood.target))),
		};
		if (thoughts == nullptr) {
			return breakdown;
		}
		breakdown.thoughts.reserve(mood.thoughtContributions.size());
		for (const ecs::MoodThoughtContribution& contribution : mood.thoughtContributions) {
			const ecs::Thought& thought = *ecs::findThought(*thoughts, contribution.thought);
			const bool			timed = thought.moodEffect->lasts == ecs::MoodEffectLasts::ForDuration;
			breakdown.thoughts.push_back({
				.id = thought.id,
				.label = thoughtEventText(types, thought),
				.amount = moodAmountText(contribution.amount),
				.left = timed ? timeLeftText((thought.expiresAt - simulationSeconds) / ecs::kSimulationSecondsPerGameHour)
							  : effectLength(types, thought),
			});
		}
		return breakdown;
	}

	bool ThoughtLogModel::refresh(const ecs::World& world, ecs::EntityID colonist) {
		const ecs::Thoughts* thoughts = world.getComponent<ecs::Thoughts>(colonist);
		const ecs::ThoughtId nextId = thoughts != nullptr ? thoughts->nextId : 0;
		// A repeat folded into a held thought moves its last arrival on, and a stacking one its count too.
		double	 latestRepeat = -1.0;
		uint64_t heldRepeats = 0;
		if (thoughts != nullptr) {
			ecs::forEachHeldThought(*thoughts, [&](const ecs::Thought& thought) {
				latestRepeat = std::max(latestRepeat, thought.lastRepeatedAt);
				heldRepeats += thought.count;
			});
		}
		if (built && colonist == shownColonist && nextId == shownNextId && latestRepeat == shownLatestRepeat &&
			heldRepeats == shownHeldRepeats) {
			return false;
		}
		built = true;
		shownColonist = colonist;
		shownNextId = nextId;
		shownLatestRepeat = latestRepeat;
		shownHeldRepeats = heldRepeats;
		logEntries.clear();
		if (thoughts != nullptr) {
			logEntries = thoughtLogEntries(world.getSystem<ecs::ThoughtSystem>().types(), *thoughts, gameClock(world));
		}
		return true;
	}

} // namespace world_sim

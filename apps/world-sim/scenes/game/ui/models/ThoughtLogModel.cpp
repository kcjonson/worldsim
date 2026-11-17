#include "ThoughtLogModel.h"

#include <ecs/World.h>
#include <ecs/systems/ThoughtSystem.h>
#include <ecs/systems/TimeSystem.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <format>

namespace world_sim {

	namespace {

		constexpr long kMinutesPerHour = 60;
		constexpr long kHoursPerDay = 24;
		constexpr long kMinutesPerDay = kHoursPerDay * kMinutesPerHour;
		constexpr long kHoursShownBeforeDays = 2 * kHoursPerDay;
		// A sum this close to its clamped target was not clamped, only rounded.
		constexpr float kClampTolerance = 0.05F;

		std::string counted(long count, const char* one, const char* many) {
			return std::format("{} {}", count, count == 1 ? one : many);
		}

		/// To the tenth, with no ".0" on a whole number, like moodAmountText.
		std::string percentText(float value) {
			const float tenths = std::round(value * 10.0F) / 10.0F;
			if (tenths == std::round(tenths)) {
				return std::format("{}%", static_cast<int>(tenths));
			}
			return std::format("{:.1f}%", tenths);
		}

		std::string effectLength(const ecs::ThoughtTypes& types, const ecs::Thought& thought) {
			const ecs::MoodEffect& effect = *thought.moodEffect;
			if (effect.lasts == ecs::MoodEffectLasts::WhileCauseHolds) {
				return "while " + types.type(thought.eventType).feelings[thought.typeFeeling].cause;
			}
			return gameDurationText(effect.durationGameHours);
		}

		bool arrivedLater(const ecs::Thought* left, const ecs::Thought* right) {
			if (left->lastRepeatedAt != right->lastRepeatedAt) {
				return left->lastRepeatedAt > right->lastRepeatedAt;
			}
			return left->id > right->id;
		}

	} // namespace

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
		std::string text = counted(minutes / kMinutesPerHour, "hour", "hours");
		if (const long rest = minutes % kMinutesPerHour; rest != 0) {
			text += std::format(" {} min", rest);
		}
		return text;
	}

	std::string timeLeftText(double gameHours) {
		const long minutes = std::max(1L, std::lround(gameHours * kMinutesPerHour));
		if (minutes < kMinutesPerHour) {
			return std::format("{} min left", minutes);
		}
		const long hours = std::lround(static_cast<double>(minutes) / kMinutesPerHour);
		if (hours < kHoursShownBeforeDays) {
			return counted(hours, "hour", "hours") + " left";
		}
		return counted(std::lround(static_cast<double>(hours) / kHoursPerDay), "day", "days") + " left";
	}

	std::string gameDateText(const ecs::ClockTime& clock) {
		return std::format("Day {}, {:02}:{:02}", clock.day, clock.minuteOfDay / kMinutesPerHour, clock.minuteOfDay % kMinutesPerHour);
	}

	std::string thoughtWhenText(const ecs::Thought& thought) {
		if (thought.merged) {
			return gameDateText(thought.arrivedClock) + " to " + gameDateText(thought.lastRepeatedClock);
		}
		return gameDateText(thought.lastRepeatedClock);
	}

	std::string thoughtEventText(const ecs::ThoughtTypes& types, const ecs::Thought& thought) {
		const std::string& label = types.type(thought.eventType).label;
		return thought.count > 1 ? std::format("{} x{}", label, thought.count) : label;
	}

	std::string thoughtSummary(const ecs::ThoughtTypes& types, const ecs::Thought& thought) {
		std::string text = thoughtEventText(types, thought) + ". " + types.feeling(thought.feeling).label + ".";
		if (thought.moodEffect.has_value()) {
			if (thought.merged) {
				text += " " + moodAmountText(thought.moodEffect->amount) + " mood each, " + effectLength(types, thought);
			} else {
				const float amount = thought.moodEffect->amount * static_cast<float>(thought.count);
				text += " " + moodAmountText(amount) + " mood, " + effectLength(types, thought);
			}
		}
		return text;
	}

	bool isNotable(const ecs::Thought& thought) {
		return thought.importance == ecs::ThoughtImportance::Major || thought.moodEffect.has_value();
	}

	MoodBreakdown
	moodBreakdown(const ecs::ThoughtTypes& types, const ecs::Thoughts* thoughts, const ecs::Mood& mood, double simulationSeconds) {
		float needs = 0.0F;
		for (const float contribution : mood.needContributions) {
			needs += contribution;
		}
		float sum = ecs::kFullMood + needs;
		for (const ecs::MoodThoughtContribution& contribution : mood.thoughtContributions) {
			sum += contribution.amount;
		}
		MoodBreakdown breakdown{
			.needs = moodAmountText(needs),
			.target = percentText(mood.target),
			.clampedFrom = std::abs(sum - mood.target) > kClampTolerance ? percentText(sum) : std::string(),
		};
		if (thoughts == nullptr) {
			return breakdown;
		}
		struct HeldRow {
			const ecs::Thought* thought;
			float				amount;
		};
		std::vector<HeldRow> held;
		held.reserve(mood.thoughtContributions.size());
		for (const ecs::MoodThoughtContribution& contribution : mood.thoughtContributions) {
			const ecs::Thought* thought = ecs::findThought(*thoughts, contribution.thought);
			assert(thought != nullptr && thought->moodEffect.has_value() && "a mood contribution names a held thought of this record");
			held.push_back({.thought = thought, .amount = contribution.amount});
		}
		std::ranges::sort(held, [](const HeldRow& left, const HeldRow& right) {
			if (std::abs(left.amount) != std::abs(right.amount)) {
				return std::abs(left.amount) > std::abs(right.amount);
			}
			return arrivedLater(left.thought, right.thought);
		});
		breakdown.thoughts.reserve(held.size());
		for (const HeldRow& row : held) {
			const ecs::Thought& thought = *row.thought;
			const bool			timed = thought.moodEffect->lasts == ecs::MoodEffectLasts::ForDuration;
			breakdown.thoughts.push_back({
				.id = thought.id,
				.label = thoughtEventText(types, thought),
				.amount = moodAmountText(row.amount),
				.left = timed ? timeLeftText((thought.expiresAt - simulationSeconds) / ecs::kSimulationSecondsPerGameHour)
							  : effectLength(types, thought),
			});
		}
		return breakdown;
	}

	void ThoughtLogModel::select(ecs::EntityID colonist) {
		if (selected == colonist) {
			return;
		}
		selected = colonist;
		limit = kThoughtLogPage;
		cache.clear();
	}

	bool ThoughtLogModel::refresh(const ecs::World& world, ecs::EntityID colonist) {
		select(colonist);
		const ecs::Thoughts* thoughts = world.getComponent<ecs::Thoughts>(colonist);
		const Signature		 signature{.colonist = colonist, .revision = thoughts != nullptr ? thoughts->revision : 0, .filter = filter, .limit = limit};
		if (shown == signature) {
			return false;
		}
		const bool first = !shown.has_value();
		shown = signature;

		std::vector<const ecs::Thought*> matching;
		if (thoughts != nullptr) {
			matching.reserve(thoughts->record.size());
			for (const ecs::Thought& thought : thoughts->record) {
				if (filter == ThoughtLogFilter::All || isNotable(thought)) {
					matching.push_back(&thought);
				}
			}
		}
		const std::size_t count = std::min(limit, matching.size());
		std::partial_sort(matching.begin(), matching.begin() + static_cast<std::ptrdiff_t>(count), matching.end(), arrivedLater);

		std::vector<ThoughtLogEntry>					next;
		std::unordered_map<ecs::ThoughtId, CachedEntry> kept;
		next.reserve(count);
		kept.reserve(count);
		for (std::size_t index = 0; index < count; ++index) {
			const ecs::Thought& thought = *matching[index];
			const EntryKey		key{
					 .count = thought.count,
					 .merged = thought.merged,
					 .lastRepeatedAt = thought.lastRepeatedAt,
					 .feeling = thought.feeling,
					 .typeFeeling = thought.typeFeeling,
					 .line = thought.line,
					 .subjectName = thought.subjectName,
			 };
			if (const auto found = cache.find(thought.id); found != cache.end() && found->second.key == key) {
				next.push_back(found->second.entry);
				kept.emplace(thought.id, std::move(found->second));
				continue;
			}
			const ecs::ThoughtTypes& types = world.getSystem<ecs::ThoughtSystem>().types();
			CachedEntry				 built{
							 .key = key,
							 .entry =
					 {
						 .id = thought.id,
						 .when = thoughtWhenText(thought),
						 .summary = thoughtSummary(types, thought),
						 .line = ecs::renderThoughtLine(types, *thoughts, thought),
					 },
			 };
			next.push_back(built.entry);
			kept.emplace(thought.id, std::move(built));
		}
		cache = std::move(kept);

		const bool nextOlder = matching.size() > count;
		const bool nextAny = thoughts != nullptr && !thoughts->record.empty();
		if (!first && next == logEntries && nextOlder == older && nextAny == anyThought) {
			return false;
		}
		logEntries = std::move(next);
		older = nextOlder;
		anyThought = nextAny;
		++builds;
		return true;
	}

} // namespace world_sim

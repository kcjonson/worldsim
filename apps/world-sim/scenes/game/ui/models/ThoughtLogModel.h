#pragma once

// ThoughtLogModel - what a colonist's Log tab and the mood breakdown show of his thoughts. A log entry is
// one block: the event, the feeling and the mood adjustment ("Slept on the ground. Miserable. -10 mood,
// 1 day"), the quip when the thought has one, and when it arrived, newest first. The breakdown lists the
// held thoughts MoodSystem summed into the mood target, from the same contributions.
// See /docs/design/game-systems/colonists/thoughts.md (Where it shows in the UI).

#include <ecs/EntityID.h>
#include <ecs/ThoughtTypes.h>
#include <ecs/components/Mood.h>
#include <ecs/components/Thoughts.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ecs {
	class World;
}

namespace world_sim {

	struct ThoughtLogEntry {
		ecs::ThoughtId id = 0;
		std::string	   when;	// "Day 2, 07:55"
		std::string	   summary; // "Slept on the ground. Miserable. -10 mood, 1 day"
		std::string	   line;	// the quip, empty when it has none

		bool operator==(const ThoughtLogEntry& other) const = default;
	};

	struct MoodThoughtRow {
		ecs::ThoughtId id = 0;
		std::string	   label;  // "Bottled up x3"
		std::string	   amount; // "-6"
		std::string	   left;   // "17 hours left", "while soaked"

		bool operator==(const MoodThoughtRow& other) const = default;
	};

	/// The mood target's parts: the needs' share, each held thought's, and the target they clamp to.
	struct MoodBreakdown {
		std::string					needs; // "-12"
		std::vector<MoodThoughtRow> thoughts;
		std::string					target; // "72%"

		bool operator==(const MoodBreakdown& other) const = default;
	};

	/// What a thought's arrival is dated against: now on TimeSystem's simulation clock and on its day clock.
	struct GameClock {
		double simulationSeconds = 0.0;
		int	   day = 1;
		float  timeOfDay = 0.0F;
	};

	[[nodiscard]] GameClock gameClock(const ecs::World& world);

	/// "-10", "+4.5".
	[[nodiscard]] std::string moodAmountText(float amount);

	/// A mood effect's length: "45 min", "6 hours", "1 day", "3 days".
	[[nodiscard]] std::string gameDurationText(double gameHours);

	/// What is left of a timed effect, rounded up: "45 min left", "17 hours left", "3 days left".
	[[nodiscard]] std::string timeLeftText(double gameHours);

	/// The game date and time a moment on the simulation clock fell at: "Day 2, 07:55".
	[[nodiscard]] std::string gameDateText(const GameClock& clock, double simulationSeconds);

	/// The event's label, with " x<count>" for a stack of more than one.
	[[nodiscard]] std::string thoughtEventText(const ecs::ThoughtTypes& types, const ecs::Thought& thought);

	/// "Slept on the ground. Miserable. -10 mood, 1 day", "... -6 mood, while soaked", or with no mood
	/// effect just "Very hungry. Annoyed.". A stack's amount is its whole effect.
	[[nodiscard]] std::string thoughtSummary(const ecs::ThoughtTypes& types, const ecs::Thought& thought);

	/// Every recorded thought, latest arrival first (a repeat folded into a held thought counts as one),
	/// then the higher id.
	[[nodiscard]] std::vector<ThoughtLogEntry>
	thoughtLogEntries(const ecs::ThoughtTypes& types, const ecs::Thoughts& thoughts, const GameClock& clock);

	/// The mood's parts as MoodSystem last summed them; `thoughts` is null for a colonist who has none yet.
	[[nodiscard]] MoodBreakdown
	moodBreakdown(const ecs::ThoughtTypes& types, const ecs::Thoughts* thoughts, const ecs::Mood& mood, double simulationSeconds);

	/// One colonist's log, built again only when it changes: another colonist, a new thought, or a repeat
	/// folded into a held one.
	class ThoughtLogModel {
	  public:
		/// True when the entries changed.
		bool refresh(const ecs::World& world, ecs::EntityID colonist);

		[[nodiscard]] const std::vector<ThoughtLogEntry>& entries() const { return logEntries; }

	  private:
		ecs::EntityID				 shownColonist{0};
		ecs::ThoughtId				 shownNextId = 0;
		double						 shownLatestRepeat = -1.0;
		uint64_t					 shownHeldRepeats = 0;
		bool						 built = false;
		std::vector<ThoughtLogEntry> logEntries;
	};

} // namespace world_sim

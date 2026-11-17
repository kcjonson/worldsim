#pragma once

// ThoughtLogModel - what a colonist's Log tab and the mood breakdown show of his thoughts. A log entry is
// one block: the event, the feeling and the mood adjustment ("Slept on the ground. Miserable. -10 mood,
// 1 day"), the quip when the thought has one, and when it arrived, newest first. The log shows the notable
// thoughts (major ones and those with a mood effect) or all of them, a page at a time. The breakdown lists
// the held thoughts MoodSystem summed into the mood target, from the same contributions.
// See /docs/design/game-systems/colonists/thoughts.md (Where it shows in the UI).

#include <ecs/ClockTime.h>
#include <ecs/EntityID.h>
#include <ecs/ThoughtTypes.h>
#include <ecs/components/Mood.h>
#include <ecs/components/Thoughts.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
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
		std::string					target; // "72%", "65.5%"
		/// What the parts add to when the target is clamped to 0 or 100 ("112%"); empty when it isn't.
		std::string clampedFrom;

		bool operator==(const MoodBreakdown& other) const = default;
	};

	enum class ThoughtLogFilter : uint8_t { Notable, All };

	/// The blocks the log shows at first, and how many more each "Show older" adds.
	inline constexpr std::size_t kThoughtLogPage = 50;

	/// "-10", "+4.5".
	[[nodiscard]] std::string moodAmountText(float amount);

	/// A mood effect's length: "45 min", "1 hour 15 min", "6 hours", "1 day", "3 days".
	[[nodiscard]] std::string gameDurationText(double gameHours);

	/// What is left of a timed effect in its largest unit, rounded to the nearest and never 0: minutes under
	/// an hour, hours under two days, then days ("12 min left", "17 hours left", "2 days left").
	[[nodiscard]] std::string timeLeftText(double gameHours);

	/// "Day 2, 07:55".
	[[nodiscard]] std::string gameDateText(const ecs::ClockTime& clock);

	/// When a thought arrived: its latest arrival ("Day 9, 17:42"), or a merged entry's range from its first
	/// ("Day 3, 08:10 to Day 9, 17:42").
	[[nodiscard]] std::string thoughtWhenText(const ecs::Thought& thought);

	/// The event's label, with " x<count>" for a stack or a merged entry of more than one.
	[[nodiscard]] std::string thoughtEventText(const ecs::ThoughtTypes& types, const ecs::Thought& thought);

	/// "Slept on the ground. Miserable. -10 mood, 1 day", "... -6 mood, while soaked", or with no mood
	/// effect just "Very hungry. Annoyed.". A stack's amount is its whole effect; a merged entry's is one
	/// thought's ("Slept on the ground x12. Miserable. -10 mood each, 1 day").
	[[nodiscard]] std::string thoughtSummary(const ecs::ThoughtTypes& types, const ecs::Thought& thought);

	/// Major, or with a mood effect: what the log shows by default.
	[[nodiscard]] bool isNotable(const ecs::Thought& thought);

	/// The mood's parts as MoodSystem last summed them; `thoughts` is null for a colonist who has none yet.
	[[nodiscard]] MoodBreakdown
	moodBreakdown(const ecs::ThoughtTypes& types, const ecs::Thoughts* thoughts, const ecs::Mood& mood, double simulationSeconds);

	/// One colonist's log under a filter, the newest `shown` matching thoughts, latest arrival first (a repeat
	/// folded into a held thought counts as one), then the higher id. Built again only when the colonist, his
	/// record's revision, the filter or the page count changes, and then only the entries whose text moved.
	class ThoughtLogModel {
	  public:
		/// Another colonist starts again at one page. refresh does this too; a view calls it on its own when the
		/// selection changes while the log is not being built.
		void select(ecs::EntityID colonist);

		/// True on the first build and when the entries changed since.
		bool refresh(const ecs::World& world, ecs::EntityID colonist);

		void setFilter(ThoughtLogFilter newFilter) { filter = newFilter; }
		void showOlder() { limit += kThoughtLogPage; }

		[[nodiscard]] ThoughtLogFilter					  currentFilter() const { return filter; }
		[[nodiscard]] const std::vector<ThoughtLogEntry>& entries() const { return logEntries; }
		/// More thoughts match the filter than are shown.
		[[nodiscard]] bool hasOlder() const { return older; }
		/// The colonist has any thought at all, shown or not.
		[[nodiscard]] bool hasThoughts() const { return anyThought; }
		/// Counts the builds, so a view can tell it has shown the latest.
		[[nodiscard]] uint64_t version() const { return builds; }

	  private:
		struct Signature {
			ecs::EntityID	 colonist{0};
			uint64_t		 revision = 0;
			ThoughtLogFilter filter = ThoughtLogFilter::Notable;
			std::size_t		 limit = 0;

			bool operator==(const Signature& other) const = default;
		};

		/// What an entry's text is made from; the same key, the same text.
		struct EntryKey {
			uint32_t				count = 0;
			bool					merged = false;
			double					lastRepeatedAt = 0.0;
			ecs::FeelingId			feeling = 0;
			uint16_t				typeFeeling = 0;
			std::optional<uint16_t> line;
			std::optional<uint32_t> subjectName;

			bool operator==(const EntryKey& other) const = default;
		};

		struct CachedEntry {
			EntryKey		key;
			ThoughtLogEntry entry;
		};

		ThoughtLogFilter							   filter = ThoughtLogFilter::Notable;
		std::size_t									   limit = kThoughtLogPage;
		ecs::EntityID								   selected{ecs::kInvalidEntity};
		std::optional<Signature>					   shown;
		std::vector<ThoughtLogEntry>				   logEntries;
		std::unordered_map<ecs::ThoughtId, CachedEntry> cache;
		bool										   older = false;
		bool										   anyThought = false;
		uint64_t									   builds = 0;
	};

} // namespace world_sim

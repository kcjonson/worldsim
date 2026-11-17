#pragma once

// A colonist's thoughts: every one recorded in arrival order, and the ones whose mood effect is
// still running marked as held. ThoughtSystem is the only writer.
// See /docs/design/game-systems/colonists/thoughts.md.

#include "../ClockTime.h"
#include "../EntityID.h"

#include <assets/ThoughtConfig.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ecs {

	/// An event type ("soaked", "saw a new kind of tree"): its index in ThoughtTypes.
	using ThoughtEventTypeId = uint32_t;
	/// An emotion word from the shared feeling list ("annoyed", "delighted"): its index in ThoughtTypes.
	using FeelingId = uint32_t;
	/// A thought's id within one colonist's record, counted up from 1 in arrival order.
	using ThoughtId = uint32_t;

	using ThoughtImportance = assets::ThoughtImportance;

	enum class ThoughtSubjectKind : uint8_t { Nothing, Colonist, Entity, Kind, Place };

	/// Who or what a thought is about. A kind is a kind of thing, by its asset definition: the first oak a
	/// colonist saw is about oaks, not one tree.
	struct ThoughtSubject {
		ThoughtSubjectKind kind = ThoughtSubjectKind::Nothing;
		EntityID		   entity = kInvalidEntity;
		/// The definition's AssetRegistry id, for a kind.
		uint32_t  definition = 0;
		glm::vec2 place{0.0F, 0.0F};

		[[nodiscard]] static ThoughtSubject aboutNothing() { return {}; }
		[[nodiscard]] static ThoughtSubject aboutColonist(EntityID colonistId) {
			return {.kind = ThoughtSubjectKind::Colonist, .entity = colonistId};
		}
		[[nodiscard]] static ThoughtSubject aboutEntity(EntityID entityId) {
			return {.kind = ThoughtSubjectKind::Entity, .entity = entityId};
		}
		[[nodiscard]] static ThoughtSubject aboutKind(uint32_t definitionId) {
			return {.kind = ThoughtSubjectKind::Kind, .definition = definitionId};
		}
		[[nodiscard]] static ThoughtSubject aboutPlace(glm::vec2 position) {
			return {.kind = ThoughtSubjectKind::Place, .place = position};
		}

		bool operator==(const ThoughtSubject& other) const = default;
	};

	enum class MoodEffectLasts : uint8_t { ForDuration, WhileCauseHolds };

	/// A signed mood amount, lasting either a stretch of game time or while its type's `while` condition
	/// holds (or until its creator ends it).
	struct MoodEffect {
		float			amount = 0.0F;
		MoodEffectLasts lasts = MoodEffectLasts::ForDuration;
		float			durationGameHours = 0.0F;

		[[nodiscard]] static MoodEffect timed(float signedAmount, float gameHours) {
			return {.amount = signedAmount, .lasts = MoodEffectLasts::ForDuration, .durationGameHours = gameHours};
		}
		[[nodiscard]] static MoodEffect whileCauseHolds(float signedAmount) {
			return {.amount = signedAmount, .lasts = MoodEffectLasts::WhileCauseHolds};
		}
	};

	/// Times are on TimeSystem::simulationSeconds().
	struct Thought {
		ThoughtId		   id = 0;
		ThoughtEventTypeId eventType = 0;
		ThoughtSubject	   subject{};
		FeelingId		   feeling = 0;
		/// Which of its type's feelings applied: an index into ThoughtType::feelings.
		uint16_t typeFeeling = 0;
		/// Its line, an index into ThoughtType::lines; none when no line fit.
		std::optional<uint16_t> line{};
		/// The subject's name when the thought arrived, an index into Thoughts::subjectNames; none when the
		/// subject had no name. Kept so a line still names a subject that is gone.
		std::optional<uint32_t>	  subjectName{};
		std::optional<MoodEffect> moodEffect{};
		ThoughtImportance		  importance = ThoughtImportance::Normal;
		/// The first arrival: a merged entry's earliest thought.
		double arrivedAt = 0.0;
		/// arrivedAt on the day clock, stamped then: a dev time set later moves the clock, not this.
		ClockTime arrivedClock{};
		/// The latest arrival: a repeat folded into this thought, or a merged entry's latest thought; equals
		/// arrivedAt until there is one.
		double lastRepeatedAt = 0.0;
		/// lastRepeatedAt on the day clock, stamped then.
		ClockTime lastRepeatedClock{};
		/// A timed effect's end. Unused for a cause-bound or momentary thought.
		double expiresAt = 0.0;
		/// When a cause-bound effect's `while` condition is next checked. Unused otherwise.
		double				  nextCheckAt = 0.0;
		/// The latest end among a merged entry's thoughts.
		std::optional<double> endedAt{};
		/// Repeats of a stacking held thought, the first included; its mood effect counts this many times. A
		/// merged entry's is the sum of its thoughts' counts.
		uint32_t count = 1;
		/// Pruning collapsed repeats of this event, subject, and typeFeeling into this entry, which keeps the
		/// earliest one's id, line, and mood effect. Never held.
		bool merged = false;
	};

	struct Thoughts {
		/// Every thought in first-arrival order (ids ascending), held or not. Pruning removes and merges
		/// entries, never held ones, and a surviving entry keeps its id.
		std::vector<Thought>   record;
		std::vector<ThoughtId> heldIds;
		ThoughtId			   nextId = 1;
		/// Bumped each time a thought arrives, folds a repeat, or ends, and each time pruning changes the
		/// record, so a reader can tell the record changed.
		uint64_t revision = 0;
		/// The line this colonist was last given for each event type, so the next pick can avoid it.
		std::unordered_map<ThoughtEventTypeId, uint16_t> lastLines;
		/// Each subject name this colonist's thoughts have given, once: a thought holds an index, not a copy.
		std::vector<std::string> subjectNames;
	};

	[[nodiscard]] const Thought* findThought(const Thoughts& thoughts, ThoughtId id);
	[[nodiscard]] Thought*		 findThought(Thoughts& thoughts, ThoughtId id);

	[[nodiscard]] bool isHeld(const Thought& thought);

	/// A held thought's mood effect, a stacking one counted once per repeat; zero when not held.
	[[nodiscard]] float heldMoodAmount(const Thought& thought);

	/// Calls visitor(const Thought&) for each held thought in arrival order. The visitor must not create
	/// or end thoughts.
	template <typename Visitor>
	void forEachHeldThought(const Thoughts& thoughts, Visitor&& visitor) {
		for (const ThoughtId id : thoughts.heldIds) {
			visitor(*findThought(thoughts, id));
		}
	}

	/// The sum of every held thought's mood effect, a stacking thought counted once per repeat.
	[[nodiscard]] float heldMoodTotal(const Thoughts& thoughts);

} // namespace ecs

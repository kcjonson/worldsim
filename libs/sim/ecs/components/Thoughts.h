#pragma once

// A colonist's thoughts: every one recorded in arrival order, and the ones whose mood effect is
// still running marked as held. ThoughtSystem is the only writer.
// See /docs/design/game-systems/colonists/thoughts.md.

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

	enum class ThoughtSubjectKind : uint8_t { Nothing, Colonist, Entity, Place };

	/// Who or what a thought is about.
	struct ThoughtSubject {
		ThoughtSubjectKind kind = ThoughtSubjectKind::Nothing;
		EntityID		   entity = kInvalidEntity;
		glm::vec2		   place{0.0F, 0.0F};

		[[nodiscard]] static ThoughtSubject aboutNothing() { return {}; }
		[[nodiscard]] static ThoughtSubject aboutColonist(EntityID colonistId) {
			return {.kind = ThoughtSubjectKind::Colonist, .entity = colonistId};
		}
		[[nodiscard]] static ThoughtSubject aboutEntity(EntityID entityId) {
			return {.kind = ThoughtSubjectKind::Entity, .entity = entityId};
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
		double					  arrivedAt = 0.0;
		/// The latest repeat folded into this thought; equals arrivedAt until one arrives.
		double lastRepeatedAt = 0.0;
		/// A timed effect's end. Unused for a cause-bound or momentary thought.
		double expiresAt = 0.0;
		/// When a cause-bound effect's `while` condition is next checked. Unused otherwise.
		double				  nextCheckAt = 0.0;
		std::optional<double> endedAt{};
		/// Repeats of a stacking held thought, the first included. Its mood effect counts this many times.
		uint32_t count = 1;
	};

	struct Thoughts {
		/// Every thought in arrival order (ids ascending), held or not.
		std::vector<Thought>   record;
		std::vector<ThoughtId> heldIds;
		ThoughtId			   nextId = 1;
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

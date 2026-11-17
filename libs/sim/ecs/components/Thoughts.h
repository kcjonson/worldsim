#pragma once

// A colonist's thoughts: every one recorded in arrival order, and the ones whose mood effect is
// still running marked as held. ThoughtSystem is the only writer.
// See /docs/design/game-systems/colonists/thoughts.md.

#include "../EntityID.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace ecs {

	/// An event type ("soaked", "saw a new kind of tree"), interned by whoever defines the types.
	using ThoughtEventTypeId = uint32_t;
	/// An emotion word from the shared feeling list ("annoyed", "delighted").
	using FeelingId = uint32_t;
	/// A thought's id within one colonist's record, counted up from 1 in arrival order.
	using ThoughtId = uint32_t;

	enum class ThoughtImportance : uint8_t {
		Trivial,
		Normal,
		Major
	};

	enum class ThoughtSubjectKind : uint8_t {
		Nothing,
		Colonist,
		Entity,
		Place
	};

	/// Who or what a thought is about.
	struct ThoughtSubject {
		ThoughtSubjectKind kind = ThoughtSubjectKind::Nothing;
		EntityID		   entity = kInvalidEntity;
		glm::vec2		   place{0.0F, 0.0F};

		[[nodiscard]] static ThoughtSubject aboutNothing() { return {}; }
		[[nodiscard]] static ThoughtSubject aboutColonist(EntityID colonistId) {
			return {.kind = ThoughtSubjectKind::Colonist, .entity = colonistId};
		}
		[[nodiscard]] static ThoughtSubject aboutEntity(EntityID entityId) { return {.kind = ThoughtSubjectKind::Entity, .entity = entityId}; }
		[[nodiscard]] static ThoughtSubject aboutPlace(glm::vec2 position) { return {.kind = ThoughtSubjectKind::Place, .place = position}; }

		bool operator==(const ThoughtSubject& other) const = default;
	};

	enum class MoodEffectLasts : uint8_t {
		ForDuration,
		WhileCauseHolds
	};

	/// A signed mood amount, lasting either a stretch of game time or until its creator ends it.
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
		ThoughtId				  id = 0;
		ThoughtEventTypeId		  eventType = 0;
		ThoughtSubject			  subject{};
		FeelingId				  feeling = 0;
		std::optional<MoodEffect> moodEffect{};
		ThoughtImportance		  importance = ThoughtImportance::Normal;
		double					  arrivedAt = 0.0;
		/// The latest repeat folded into this thought; equals arrivedAt until one arrives.
		double					  lastRepeatedAt = 0.0;
		/// A timed effect's end. Unused for a cause-bound or momentary thought.
		double					  expiresAt = 0.0;
		std::optional<double>	  endedAt{};
		/// Repeats of a stacking held thought, the first included. Its mood effect counts this many times.
		uint32_t				  count = 1;

		[[nodiscard]] bool	isHeld() const { return moodEffect.has_value() && !endedAt.has_value(); }
		[[nodiscard]] float heldMoodAmount() const { return isHeld() ? moodEffect->amount * static_cast<float>(count) : 0.0F; }
	};

	struct Thoughts {
		/// Every thought in arrival order (ids ascending), held or not.
		std::vector<Thought>   record;
		std::vector<ThoughtId> heldIds;
		ThoughtId			   nextId = 1;
	};

	[[nodiscard]] const Thought* findThought(const Thoughts& thoughts, ThoughtId id);
	[[nodiscard]] Thought*		 findThought(Thoughts& thoughts, ThoughtId id);

	/// The held thoughts in arrival order.
	[[nodiscard]] std::vector<const Thought*> heldThoughts(const Thoughts& thoughts);

	/// The sum of every held thought's mood effect, a stacking thought counted once per repeat.
	[[nodiscard]] float heldMoodTotal(const Thoughts& thoughts);

} // namespace ecs

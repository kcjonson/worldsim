#pragma once

// The thought types and feelings, built from data with their conditions parsed, which ThoughtSystem
// creates thoughts from. See /docs/design/game-systems/colonists/thoughts.md.

#include "components/Thoughts.h"

#include <conditions/Condition.h>

#include <assets/ThoughtConfig.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace conditions {
	class ConditionVocabulary;
}

namespace ecs {

	class World;

	struct FeelingDefinition {
		std::string name;
	};

	/// One feeling a thought type can produce. `whileCondition` is set when the mood effect lasts while it
	/// holds.
	struct ThoughtFeeling {
		FeelingId							 feeling = 0;
		std::optional<conditions::Condition> when;
		uint32_t							 specificity = 0;
		std::optional<MoodEffect>			 moodEffect;
		std::optional<conditions::Condition> whileCondition;
	};

	struct ThoughtLine {
		std::string							 text;
		std::optional<FeelingId>			 feeling;
		std::optional<conditions::Condition> when;
		uint32_t							 specificity = 0;
		/// The text names the subject with {subject}.
		bool namesSubject = false;
	};

	/// Feelings and lines are kept most specific first, file order within the same specificity.
	struct ThoughtType {
		std::string						  name;
		ThoughtImportance				  importance = ThoughtImportance::Normal;
		bool							  stacks = false;
		std::vector<ThoughtFeeling>		  feelings;
		std::vector<ThoughtLine>		  lines;
		std::optional<ThoughtEventTypeId> ends;
	};

	/// The number of terms a condition names; a feeling or line with more is more specific. None is 0.
	[[nodiscard]] uint32_t specificityOf(const std::optional<conditions::Condition>& condition);

	class ThoughtTypes {
	  public:
		/// Conditions are parsed with `vocabulary`, which must be frozen and outlive these types.
		explicit ThoughtTypes(const conditions::ConditionVocabulary& vocabulary);

		/// Replaces everything with `config`. Returns every error: an unknown feeling, a type with no
		/// feeling, a line limited to a feeling its type cannot produce, a condition that does not parse
		/// (with its position), an <ends> naming no type or making a cycle, a line longer than the tuning
		/// allows. Nothing is kept when there are errors.
		std::vector<std::string> load(const assets::ThoughtConfig& config);

		[[nodiscard]] std::optional<ThoughtEventTypeId>		 findType(std::string_view name) const;
		[[nodiscard]] std::optional<FeelingId>				 findFeeling(std::string_view name) const;
		[[nodiscard]] const ThoughtType&					 type(ThoughtEventTypeId id) const { return types[id]; }
		[[nodiscard]] const FeelingDefinition&				 feeling(FeelingId id) const { return feelings[id]; }
		[[nodiscard]] std::size_t							 typeCount() const { return types.size(); }
		[[nodiscard]] std::size_t							 maxSubjectNameLength() const { return subjectNameLength; }
		[[nodiscard]] double								 whileCheckSimulationSeconds() const { return whileCheckSeconds; }
		[[nodiscard]] const conditions::ConditionVocabulary& vocabulary() const { return conditionVocabulary; }

	  private:
		const conditions::ConditionVocabulary&				conditionVocabulary;
		std::vector<FeelingDefinition>						feelings;
		std::vector<ThoughtType>							types;
		std::unordered_map<std::string, FeelingId>			feelingsByName;
		std::unordered_map<std::string, ThoughtEventTypeId> typesByName;
		double												whileCheckSeconds = 0.0;
		std::size_t											subjectNameLength = 0;
	};

	/// The name a line gives the subject: a colonist's own name, or another entity's asset label (its
	/// definition name when it has no label). None for a place, nothing, an entity that is gone or has
	/// neither, or a colonist with an empty name, who is never named by his definition.
	[[nodiscard]] std::optional<std::string> thoughtSubjectName(const World& world, const ThoughtSubject& subject);

	/// The first `maxCharacters` UTF-8 characters of `name` with trailing spaces dropped; the whole name when
	/// it fits.
	[[nodiscard]] std::string shortenSubjectName(std::string_view name, std::size_t maxCharacters);

	/// The thought's line with {subject} filled in by the name the thought was given, shortened to the
	/// tuning's maxSubjectNameLength; empty when it has no line.
	[[nodiscard]] std::string renderThoughtLine(const ThoughtTypes& types, const Thoughts& thoughts, const Thought& thought);

} // namespace ecs

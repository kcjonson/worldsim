#pragma once

// Thoughts as data: the shared list of feelings, the thought types, and the numbers the thought system
// runs on. Every XML file under the folder adds to it, so a mod adds or replaces a feeling or a thought
// type by adding a file. Conditions are kept as text here; the sim parses them against its vocabulary.
// See /docs/design/game-systems/colonists/thoughts.md (Data).

#include "assets/NeedsConfig.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace assets {

	enum class ThoughtImportance : uint8_t { Trivial, Normal, Major };

	/// An emotion word from the shared list, the closed set a thought type's feelings are checked against.
	struct FeelingDef {
		std::string id;
		std::string label; // what the log shows: "Miserable"
		std::string source; // the file it was declared in
	};

	/// A feeling's mood effect: a signed amount for `hours` game hours, or for as long as `whileCondition`
	/// holds. Exactly one of the two is set; a while effect's `cause` names what holds it ("soaked").
	struct ThoughtMoodDef {
		float				 amount = 0.0F;
		std::optional<float> hours;
		std::string			 whileCondition;
		std::string			 cause;
	};

	/// One way a colonist can take a thought type's event: the feeling, the condition it needs (empty for
	/// none), and its mood effect, if any.
	struct ThoughtFeelingDef {
		std::string					  feeling;
		std::string					  when;
		std::optional<ThoughtMoodDef> mood;
	};

	/// A quip. `feeling` and `when` are empty when the line is not limited by them.
	struct ThoughtLineDef {
		std::string text;
		std::string feeling;
		std::string when;
	};

	/// What happens in the sim that creates a thought of a type. Each names a change, never a state.
	enum class ThoughtTrigger : uint8_t {
		NeedFalls,		// a need falls below one of its thresholds, the lowest one it crossed
		NeedRises,		// a need rises back to one of its thresholds, the highest one it crossed
		ActionStarts,	// a colonist starts an action
		ActionFinishes, // a colonist finishes an action; one cut short does not finish
		FirstSight,		// a colonist sees a kind of thing for the first time
		RecipeLearned,	// a colonist learns a recipe
		Idle,			// a slow random roll while no other thought has arrived
	};

	/// An action's `on` for one done on the bare ground, about no thing.
	inline constexpr const char* kGroundName = "ground";

	/// One <source>: the trigger, and the attributes that trigger reads. Names are checked against the
	/// sim's needs and actions when it builds the types.
	struct ThoughtSourceDef {
		ThoughtTrigger trigger = ThoughtTrigger::Idle;
		std::string	   need;			 // NeedFalls, NeedRises: the need's label
		std::size_t	   threshold = 0;	 // NeedFalls, NeedRises: an index into kNeedThresholdNames
		std::string	   action;			 // ActionStarts, ActionFinishes: the sim's action name
		/// ActionStarts, ActionFinishes (`on`): kGroundName when the action is about no thing, or a capability
		/// the kind of thing it is about has. FirstSight (`of`): a capability the kind seen has. Empty for any.
		std::string thing;
		bool		first = false;		// ActionStarts, ActionFinishes: only the first time for this kind of thing
		float		everyMinutes = 0.0F; // Idle: game minutes between rolls
		float		chance = 0.0F;		 // Idle: the chance a roll gives the thought, above 0 and at most 1
	};

	struct ThoughtTypeDef {
		std::string					   id;
		std::string					   label; // what the log shows for the event: "Slept on the ground"
		ThoughtImportance			   importance = ThoughtImportance::Normal;
		bool						   stacks = false;
		std::vector<ThoughtFeelingDef> feelings;
		std::vector<ThoughtLineDef>	   lines;
		std::vector<ThoughtSourceDef>  sources;
		std::string					   ends; // the thought type created when a held one of this type ends
		std::string					   source;
	};

	/// How many thoughts a colonist keeps, and how pruning picks what goes. Major thoughts are never dropped,
	/// so they carry no weight.
	struct ThoughtKeptTuning {
		/// The most thoughts a colonist keeps.
		std::size_t limit = 0;
		/// The fraction of the limit at which pruning starts, above pruneStopsAt and at most 1.
		float pruneStartsAt = 0.0F;
		/// The fraction of the limit pruning brings the record back to, above 0.
		float pruneStopsAt = 0.0F;
		/// Thoughts from this many recent game days are never pruned.
		float recentDays = 0.0F;
		/// The importance a trivial or a normal thought scores with.
		float trivialWeight = 0.0F;
		float normalWeight = 0.0F;
	};

	struct ThoughtTuning {
		/// The longest a line may be, in characters, {subject} counted as maxSubjectNameLength.
		std::size_t maxLineLength = 0;
		/// The most characters of a subject's name a line shows; a longer name is cut to fit.
		std::size_t maxSubjectNameLength = 0;
		/// How often a held thought's `while` condition is checked, in game minutes.
		float			  whileCheckMinutes = 0.0F;
		ThoughtKeptTuning kept;
	};

	struct ThoughtConfig {
		std::vector<FeelingDef>		 feelings;
		std::vector<ThoughtTypeDef>	 types;
		std::optional<ThoughtTuning> tuning;
	};

	struct ThoughtConfigLoad {
		ThoughtConfig			 config;
		std::vector<std::string> errors;
	};

	/// Read every .xml file under `folder`. The folder's own files are the base set and load first, in path
	/// order; then the files in its subfolders, in path order.
	///
	/// Each file's root is <thoughts>, holding <feeling>, <thought>, and <tuning> elements; a <tuning> holds
	/// one <kept>. A feeling or a
	/// thought type declared again in a later file replaces the earlier one, and a later <tuning> replaces
	/// the earlier numbers, each logged at info level naming both files; one declared twice in the same file
	/// is an error. Unknown elements and attributes are errors. The checks that need the condition vocabulary
	/// or the whole set (an unknown feeling, an <ends> cycle, a line too long) are the sim's, when it builds
	/// the types. An empty error list means it loaded clean.
	[[nodiscard]] ThoughtConfigLoad loadThoughtConfig(const std::filesystem::path& folder);

} // namespace assets

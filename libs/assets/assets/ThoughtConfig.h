#pragma once

// Thoughts as data: the shared list of feelings, the thought types, and the numbers the thought system
// runs on. Every XML file under the folder adds to it, so a mod adds or replaces a feeling or a thought
// type by adding a file. Conditions are kept as text here; the sim parses them against its vocabulary.
// See /docs/design/game-systems/colonists/thoughts.md (Data).

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
		std::string source; // the file it was declared in
	};

	/// A feeling's mood effect: a signed amount for `hours` game hours, or for as long as `whileCondition`
	/// holds. Exactly one of the two is set.
	struct ThoughtMoodDef {
		float				 amount = 0.0F;
		std::optional<float> hours;
		std::string			 whileCondition;
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

	struct ThoughtTypeDef {
		std::string					   id;
		ThoughtImportance			   importance = ThoughtImportance::Normal;
		bool						   stacks = false;
		std::vector<ThoughtFeelingDef> feelings;
		std::vector<ThoughtLineDef>	   lines;
		std::string					   ends; // the thought type created when a held one of this type ends
		std::string					   source;
	};

	struct ThoughtTuning {
		/// The longest a line may be, in characters, {subject} counted as maxSubjectNameLength.
		std::size_t maxLineLength = 0;
		/// The most characters of a subject's name a line shows; a longer name is cut to fit.
		std::size_t maxSubjectNameLength = 0;
		/// How often a held thought's `while` condition is checked, in game minutes.
		float whileCheckMinutes = 0.0F;
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
	/// Each file's root is <thoughts>, holding <feeling>, <thought>, and <tuning> elements. A feeling or a
	/// thought type declared again in a later file replaces the earlier one, and a later <tuning> replaces
	/// the earlier numbers, each logged at info level naming both files; one declared twice in the same file
	/// is an error. Unknown elements and attributes are errors. The checks that need the condition vocabulary
	/// or the whole set (an unknown feeling, an <ends> cycle, a line too long) are the sim's, when it builds
	/// the types. An empty error list means it loaded clean.
	[[nodiscard]] ThoughtConfigLoad loadThoughtConfig(const std::filesystem::path& folder);

} // namespace assets

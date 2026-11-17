#pragma once

// The vocabulary conditions are written in, as data: the terms and their types, the names a Symbol
// takes, the kinds near() looks for, task groups, and the night hours. Every XML file under the folder
// adds to it, so a mod adds terms, names, and kinds by adding a file.
// See /docs/design/game-systems/colonists/mood.md (Conditions).

#include "assets/AssetDefinition.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace assets {

	/// What a term or an argument is. A Symbol is a name from a declared set (a kind, a need); a Target
	/// is an entity.
	enum class ConditionValueType : uint8_t { Truth, Number, Symbol, Target };

	struct ConditionParameterDef {
		ConditionValueType type = ConditionValueType::Number;
		std::string		   symbols; // the symbol set a Symbol parameter takes its names from
	};

	struct ConditionTermDef {
		std::string						   name;
		ConditionValueType				   result = ConditionValueType::Truth;
		std::string						   resultSymbols; // the symbol set a Symbol result is compared against with `is`
		std::vector<ConditionParameterDef> parameters;
		std::string						   source; // the file it was declared in, for errors
	};

	/// A kind near() and count_near() look for, and the capability memory holds it under. A kind with no
	/// capability is declared but unanswered, and reads Unknown.
	struct ConditionKindDef {
		std::string					  name;
		std::optional<CapabilityType> capability;
	};

	/// A name current_task() takes that stands for several task types.
	struct ConditionTaskGroupDef {
		std::string				 name;
		std::vector<std::string> tasks;
	};

	/// The hours that count as night: from `start` on, past midnight when `end` is earlier, until `end`.
	struct NightHours {
		float start = 0.0F;
		float end = 0.0F;
	};

	/// The names a Symbol takes. The sim adds the names it knows (needs, attributes, traits, task types) when it
	/// binds. An open set takes any name, listed or not; one nothing answers reads Unknown.
	struct ConditionSymbolSetDef {
		std::vector<std::string> names;
		bool					 open = false;
	};

	/// The symbol sets "kind" and "task" are completed by the loader: every <Kind> is a name in "kind", and
	/// every <TaskGroup> a name in "task".
	struct ConditionConfig {
		std::vector<ConditionTermDef>				  terms;
		std::map<std::string, ConditionSymbolSetDef> symbolSets;
		std::vector<ConditionKindDef>				  kinds;
		std::vector<ConditionTaskGroupDef>			  taskGroups;
		std::optional<NightHours>					  night;
	};

	struct ConditionConfigLoad {
		ConditionConfig			 config;
		std::vector<std::string> errors;
	};

	/// Read every .xml file under `folder` into one vocabulary and check it. The folder's own files are
	/// the base set and load first, in path order; then the files in its subfolders, in path order.
	///
	/// A later file may add names to a symbol set (a set any file declares open="true" is open), add a
	/// term, add a kind, replace a kind's capability, and replace <Night>; each replacement is logged at
	/// info level naming both files. Redeclaring a term with the same signature is accepted; with a
	/// different one it is an error naming both files, as is redeclaring a task group. Every symbol set a
	/// term names must exist, and night hours must be given. An empty error list means it loaded clean.
	[[nodiscard]] ConditionConfigLoad loadConditionConfig(const std::filesystem::path& folder);

} // namespace assets

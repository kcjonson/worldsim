#pragma once

// The vocabulary conditions are written in, as data: the terms and their types, the names a Symbol
// takes, the kinds near() looks for, task groups, and the night hours. Every XML file under the folder
// adds to it, so a mod adds terms, names, and kinds by adding a file.
// See /docs/design/game-systems/colonists/mood.md (Conditions).

#include "assets/AssetDefinition.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace assets {

	inline constexpr std::string_view kConditionAnd = "and";
	inline constexpr std::string_view kConditionOr = "or";
	inline constexpr std::string_view kConditionNot = "not";
	inline constexpr std::string_view kConditionIs = "is";
	inline constexpr std::string_view kConditionSelf = "self";
	inline constexpr std::string_view kConditionTarget = "target";

	/// The words of the condition grammar, which no term, symbol, or trait id may be.
	inline constexpr std::array<std::string_view, 6> kConditionReservedWords =
		{kConditionAnd, kConditionOr, kConditionNot, kConditionIs, kConditionSelf, kConditionTarget};

	/// A symbol set whose names belong to something other than the conditions files. It is closed, no
	/// conditions file may declare it, and its names come only from its owner.
	struct OwnedSymbolSet {
		std::string_view name;
		std::string_view owner;
	};

	inline constexpr std::string_view kKindSymbols = "kind";
	inline constexpr std::string_view kTaskSymbols = "task";
	inline constexpr std::string_view kTraitSymbols = "trait";
	inline constexpr std::string_view kNeedSymbols = "need";
	inline constexpr std::string_view kAttributeSymbols = "attribute";

	inline constexpr std::array<OwnedSymbolSet, 5> kOwnedSymbolSets = {{
		{.name = kKindSymbols, .owner = "the <Kind> declarations"},
		{.name = kTraitSymbols, .owner = "the trait definitions (colonists/traits)"},
		{.name = kNeedSymbols, .owner = "the sim's needs"},
		{.name = kAttributeSymbols, .owner = "the sim's attributes"},
		{.name = kTaskSymbols, .owner = "the sim's task types and the <TaskGroup> declarations"},
	}};

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

	/// A kind near() and count_near() look for: a capability memory holds things under, or the asset
	/// definition of a mover (`mover`, "Colonist") whose recent sightings memory keeps. A kind with
	/// neither is declared but unanswered, and reads Unknown.
	struct ConditionKindDef {
		std::string					  name;
		std::optional<CapabilityType> capability;
		std::string					  mover;
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

	/// The names a Symbol takes. An open set takes any name, listed or not; one nothing answers reads Unknown.
	struct ConditionSymbolSetDef {
		std::vector<std::string> names;
		bool					 open = false;
	};

	/// The loader declares every owned set (kOwnedSymbolSets) closed: every <Kind> is a name in "kind" and
	/// every <TaskGroup> a name in "task", and the sim adds the rest (needs, attributes, traits, task types).
	struct ConditionConfig {
		std::vector<ConditionTermDef>				 terms;
		std::map<std::string, ConditionSymbolSetDef> symbolSets;
		std::vector<ConditionKindDef>				 kinds;
		std::vector<ConditionTaskGroupDef>			 taskGroups;
		std::optional<NightHours>					 night;
	};

	struct ConditionConfigLoad {
		ConditionConfig			 config;
		std::vector<std::string> errors;
	};

	/// Read every .xml file under `folder` into one vocabulary and check it. The folder's own files are
	/// the base set and load first, in path order; then the files in its subfolders, in path order.
	///
	/// A later file may add names to a symbol set (a set any file declares open="true" is open), add a
	/// term, add a kind, replace a kind's mapping, and replace <Night>; each replacement is logged at
	/// info level naming both files. Redeclaring a term with the same signature is accepted; with a
	/// different one it is an error naming both files, as is redeclaring a task group. Declaring an owned
	/// set is an error naming the file. Every symbol set a term names must exist, and night hours must be
	/// given. An empty error list means it loaded clean.
	[[nodiscard]] ConditionConfigLoad loadConditionConfig(const std::filesystem::path& folder);

} // namespace assets

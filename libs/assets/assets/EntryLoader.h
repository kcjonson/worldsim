#pragma once

// Loads the behavior library from assets/config (behaviors/, actions/, work/, orders/, facts/) and the
// creatures from their asset folders, resolving every reference between entries. See
// /docs/technical/queries-and-chains.md (Files, Ids, Loading a creature).

#include "assets/CreatureConfigFiles.h"
#include "assets/Entries.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace assets {

	struct EntrySources {
		/// The folder holding behaviors/, actions/, work/, orders/, and facts/.
		std::filesystem::path configRoot;
		/// Every creature folder's files, each with no structural errors (asserted).
		std::vector<CreatureConfigFiles> creatures;
		/// The arbitration ladder's rung names, highest first; a work entry's `rung` names one.
		std::vector<std::string> rungs;
		/// The needs the sim tracks in its fixed list, by need id, which no behavior brings until the need entries
		/// land (behavior-data-migration.md, Seams); an action's effect may name one.
		std::vector<std::string> trackedNeeds;
		/// The id of a loaded asset definition by its name, or none when no definition has that name.
		std::function<std::optional<uint32_t>(std::string_view)> definitionId;
	};

	struct EntryLoad {
		EntryLibrary			 library;
		std::vector<std::string> errors;
		/// What loads but is likely a mistake (a short id hiding a top-level one, a chain nothing starts),
		/// kept for the caller to log once the whole config has loaded; empty when the load had an error.
		std::vector<std::string> warnings;
	};

	/// The names entries borrow from config that loads after them: the traits, the condition vocabulary's kinds, and
	/// the thought types.
	struct EntryNameSets {
		std::function<bool(std::string_view)> isTrait;
		std::function<bool(std::string_view)> isKind;
		std::function<bool(std::string_view)> isThought;
	};

	/// Every `<scale trait>`, `<from kind>`, and `<think thought>` in `library` that names nothing in `names`, one error
	/// each, in the loaders' form. Run once the traits, the vocabulary, and the thought types have loaded.
	[[nodiscard]] std::vector<std::string> checkEntryNames(const EntryLibrary& library, const EntryNameSets& names);

	/// Read every entry and creature, then resolve the references between them. Each error names the file
	/// (prefixed by its folder: [behaviors], [actions], [work], [orders], [facts], or [creatures]), the entry,
	/// the element, and the attribute. A file with any problem of its own loads nothing, and the references
	/// are resolved only when every file read clean; an error of any kind leaves the library empty.
	[[nodiscard]] EntryLoad loadEntries(const EntrySources& sources);

} // namespace assets

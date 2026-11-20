#pragma once

// A creature's config files, found in its asset folder (assets/world/creatures/<Name>/) by their root
// element. See /docs/technical/queries-and-chains.md (Loading a creature).

#include <filesystem>
#include <string>
#include <vector>

namespace assets {

	/// The creature folder's config files by root, each list in file name order: one <Mood> file, one
	/// <Memory> file, and the <Traits>, <Attributes>, and <Needs> files, each read by its own loader.
	struct CreatureConfigFiles {
		std::filesystem::path			   folder;
		std::vector<std::filesystem::path> mood;
		std::vector<std::filesystem::path> memory;
		std::vector<std::filesystem::path> traits;
		std::vector<std::filesystem::path> attributes;
		std::vector<std::filesystem::path> needs;
		/// What is wrong with the folder, each prefixed [creatures]. Any error stops every loader given these
		/// files; takeErrors reports them once.
		std::vector<std::string> errors;
		bool					 errorsReported = false;
	};

	/// Every .xml file at the top level of `folder` but the primary definition (`<Name>/<Name>.xml`, the asset
	/// registry's) and the files the primary names in a <motion> element. Each is an error naming it relative
	/// to `folder` when it is not readable XML or its root is none of the five; so is a subfolder. A primary
	/// that cannot be read is the one error, since the helper files cannot be told from the rest without it,
	/// and so is a missing folder. Only when every file is placed is a root with no file (or a second <Mood>
	/// or <Memory> file) an error naming the folder, so each fault gives one error.
	[[nodiscard]] CreatureConfigFiles creatureConfigFiles(const std::filesystem::path& folder);

	/// True when `found` has errors, which stop the loader asking. The first call adds them to `into`; later
	/// calls add nothing, so they reach the player once whichever loader runs first.
	[[nodiscard]] bool takeErrors(CreatureConfigFiles& found, std::vector<std::string>& into);

	/// True when `file` sits at the top level of a creature folder (a folder under one named `creatures`),
	/// whose files other than the definition and its helpers the creature's config loading owns and reports.
	[[nodiscard]] bool isInCreatureFolder(const std::filesystem::path& file);

} // namespace assets

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
		/// What is wrong with the folder, each prefixed [creatures]. The loaders take only a folder with none.
		std::vector<std::string> errors;
	};

	/// Every .xml file at the top level of `folder` but the primary definition (`<Name>/<Name>.xml`, the asset
	/// registry's) and the files the primary names in a <motion> element. Each is an error naming it relative
	/// to `folder` when it is not readable XML or its root is none of the five (a <Motion> root the primary does
	/// not name says so); so is a subfolder, and so is a filesystem fault while reading the folder. A primary
	/// that is missing or cannot be read is the one error, since the helper files cannot be told from the rest
	/// without it, and so is a missing folder. Only when every file is placed is a root with no file (or a
	/// second <Mood> or <Memory> file) an error naming the folder, so each fault gives one error.
	[[nodiscard]] CreatureConfigFiles creatureConfigFiles(const std::filesystem::path& folder);

} // namespace assets

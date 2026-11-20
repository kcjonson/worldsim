#pragma once

// A creature's config files, found in its asset folder (assets/world/creatures/<Name>/) by their root
// element. See /docs/technical/queries-and-chains.md (Loading a creature).

#include "assets/ConfigFiles.h"

#include <filesystem>
#include <string>
#include <vector>

namespace assets {

	/// The creature folder's config files by root (<Mood>, <Memory>, <Traits>, <Attributes>, <Needs>), each
	/// read by its own loader, each list in file name order. No list carries an error; what went wrong finding
	/// them is in `errors`.
	struct CreatureConfigFiles {
		std::filesystem::path	 folder;
		ConfigFiles				 mood;
		ConfigFiles				 memory;
		ConfigFiles				 traits;
		ConfigFiles				 attributes;
		ConfigFiles				 needs;
		std::vector<std::string> errors;
	};

	/// Every .xml file at the top level of `folder` but the primary definition (`<Name>/<Name>.xml`, the asset
	/// registry's) and the files the primary definition names in a <motion> element, grouped by root in file
	/// name order. A file that is not readable XML, or whose root is none of those, is an error naming it
	/// relative to `folder`; so is a missing folder.
	[[nodiscard]] CreatureConfigFiles creatureConfigFiles(const std::filesystem::path& folder);

	/// True when `file` sits at the top level of a creature folder (a folder under one named `creatures`)
	/// and has one of those roots: a config file, not an asset definition.
	[[nodiscard]] bool isCreatureConfigFile(const std::filesystem::path& file);

} // namespace assets

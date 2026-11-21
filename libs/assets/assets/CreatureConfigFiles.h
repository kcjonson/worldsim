#pragma once

// A creature's config files, found in its asset folder (assets/world/creatures/<Name>/) by their root
// element. See /docs/technical/queries-and-chains.md (Loading a creature).

#include <filesystem>
#include <string>
#include <vector>

namespace assets {

	/// The creature folder's config files by root, each list in file name order and any list possibly empty:
	/// the <Mood> and <Memory> files (at most one each), the <Traits>, <Attributes>, and <Needs> files, each read
	/// by its own loader, and the <creature> files and <behavior> files, which loadEntries reads. Which kinds a
	/// creature must have is its loader's business; missingKinds is the human's check.
	struct CreatureConfigFiles {
		std::filesystem::path			   folder;
		std::vector<std::filesystem::path> mood;
		std::vector<std::filesystem::path> memory;
		std::vector<std::filesystem::path> traits;
		std::vector<std::filesystem::path> attributes;
		std::vector<std::filesystem::path> needs;
		std::vector<std::filesystem::path> creature;
		std::vector<std::filesystem::path> behaviors;
		/// The folder's structural faults, each prefixed [creatures]. The loaders take only a folder with none.
		std::vector<std::string> errors;
	};

	/// Every file ending in `.xml` at the top level of `folder` but the primary definition (`<Name>/<Name>.xml`,
	/// matched to the folder's name exactly, as the asset registry matches it) and the files the primary names in
	/// a <motion> element; any other file is ignored. Structural faults are errors naming the file relative to
	/// `folder`: a file that is not readable XML or whose root is none of the seven (a <Motion> or
	/// <AssetDefinitions> root says why it is out of place), a subfolder, a second <Mood> or <Memory> file, and a
	/// filesystem fault. A missing or unreadable primary is the one error, since the helper files cannot be told
	/// from the rest without it; so is a missing folder. A root with no file is not a fault here.
	[[nodiscard]] CreatureConfigFiles creatureConfigFiles(const std::filesystem::path& folder);

	/// One error per kind the human reads (<Mood>, <Memory>, <Traits>, <Attributes>, <Needs>) with no file, naming
	/// the folder (`[creatures] creatures/Human: no file has a <Needs> root`).
	[[nodiscard]] std::vector<std::string> missingKinds(const CreatureConfigFiles& found);

} // namespace assets

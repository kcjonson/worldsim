#include "assets/CreatureConfigFiles.h"

#include <pugixml.hpp>

#include <algorithm>
#include <array>
#include <set>
#include <string_view>
#include <system_error>

namespace assets {

	namespace {

		constexpr const char* kPrefix = "[creatures] ";

		/// Without a trailing separator, so filename() is the folder's own name.
		std::filesystem::path withoutTrailingSeparator(const std::filesystem::path& folder) {
			std::filesystem::path normal = folder.lexically_normal();
			if (!normal.has_filename() && normal.has_relative_path()) {
				normal = normal.parent_path();
			}
			return normal;
		}

		struct RootList {
			std::string_view				   root;
			std::vector<std::filesystem::path> CreatureConfigFiles::* files;
			/// <Mood> and <Memory> are one file each; the others merge across files.
			bool oneFile;
		};

		constexpr std::array<RootList, 5> kRootLists = {{
			{.root = "Mood", .files = &CreatureConfigFiles::mood, .oneFile = true},
			{.root = "Memory", .files = &CreatureConfigFiles::memory, .oneFile = true},
			{.root = "Traits", .files = &CreatureConfigFiles::traits, .oneFile = false},
			{.root = "Attributes", .files = &CreatureConfigFiles::attributes, .oneFile = false},
			{.root = "Needs", .files = &CreatureConfigFiles::needs, .oneFile = false},
		}};

		const RootList* listFor(std::string_view root) {
			const auto found = std::ranges::find(kRootLists, root, &RootList::root);
			return found == kRootLists.end() ? nullptr : &*found;
		}

		/// How an error names `file`: relative to the creature folder `base`, with forward slashes. Lexical, so
		/// naming a file never touches the disk.
		std::string nameIn(const std::filesystem::path& file, const std::filesystem::path& base) {
			return file.lexically_relative(base).generic_string();
		}

	} // namespace

	CreatureConfigFiles creatureConfigFiles(const std::filesystem::path& folder) {
		CreatureConfigFiles			found{.folder = folder};
		const std::string			folderName = folder.generic_string();
		const std::filesystem::path base = withoutTrailingSeparator(folder);
		// Errors about the folder as a whole name it by its last two parts (creatures/Human), the same wherever
		// it was found from: the game's working directory or the asset validator's absolute root.
		const std::string shortName = (base.parent_path().filename() / base.filename()).generic_string();
		const auto		  fail = [&found](const std::string& message) {
			 found.errors.push_back(kPrefix + message);
		};

		std::error_code error;
		if (!std::filesystem::is_directory(base, error)) {
			fail(folderName + ": not a folder");
			return found;
		}

		// The primary names its helper files; when it cannot be read, nothing about the rest can be told.
		const std::filesystem::path primary = (base / (base.filename().string() + ".xml")).lexically_normal();
		if (!std::filesystem::is_regular_file(primary, error)) {
			fail(nameIn(primary, base) + ": missing; it is the creature's definition");
			return found;
		}
		pugi::xml_document			 definition;
		const pugi::xml_parse_result primaryParsed = definition.load_file(primary.string().c_str());
		if (!primaryParsed) {
			fail(nameIn(primary, base) + ": not readable XML: " + primaryParsed.description());
			return found;
		}
		std::set<std::filesystem::path> helpers;
		for (const pugi::xml_node assetDef : definition.child("AssetDefinitions").children("AssetDef")) {
			const std::string motion = assetDef.child_value("motion");
			if (!motion.empty()) {
				helpers.insert((base / motion).lexically_normal());
			}
		}

		std::vector<std::filesystem::path>	files;
		std::vector<std::filesystem::path>	subfolders;
		std::filesystem::directory_iterator entries(base, error);
		for (const std::filesystem::directory_iterator end; !error && entries != end; entries.increment(error)) {
			const std::filesystem::directory_entry& entry = *entries;
			const std::filesystem::path				file = entry.path().lexically_normal();
			std::error_code							entryError;
			if (entry.is_directory(entryError)) {
				subfolders.push_back(file);
			} else if (!entryError && entry.is_regular_file(entryError) && file.extension() == ".xml" && file != primary &&
					   !helpers.contains(file)) {
				files.push_back(file);
			}
			if (entryError) {
				fail(nameIn(file, base) + ": " + entryError.message());
			}
		}
		if (error) {
			fail(folderName + ": " + error.message());
			return found;
		}
		std::ranges::sort(subfolders);
		std::ranges::sort(files);

		for (const std::filesystem::path& subfolder : subfolders) {
			fail(nameIn(subfolder, base) + ": a subfolder, which a creature folder may not have");
		}
		std::string roots;
		for (const RootList& list : kRootLists) {
			roots += (roots.empty() ? "<" : ", <") + std::string(list.root) + ">";
		}
		for (const std::filesystem::path& file : files) {
			const std::string			 source = nameIn(file, base);
			pugi::xml_document			 document;
			const pugi::xml_parse_result parsed = document.load_file(file.string().c_str());
			if (!parsed) {
				fail(source + ": not readable XML: " + parsed.description());
				continue;
			}
			const std::string_view root = document.document_element().name();
			const RootList*		   list = listFor(root);
			if (list == nullptr && root == "Motion") {
				fail(source + ": a motion file the definition does not name in a <motion> element");
				continue;
			}
			if (list == nullptr) {
				fail(source + ": the root element <" + std::string(root) + "> is not one a creature folder holds (" + roots + ")");
				continue;
			}
			(found.*(list->files)).push_back(file);
		}

		// A file that could not be read or placed may be the one a kind is missing; it is the error to fix.
		if (!found.errors.empty()) {
			return found;
		}
		for (const RootList& list : kRootLists) {
			const std::vector<std::filesystem::path>& listed = found.*(list.files);
			const std::string						  root = "<" + std::string(list.root) + ">";
			if (listed.empty()) {
				fail(shortName + ": no file has a " + root + " root");
			} else if (list.oneFile && listed.size() > 1) {
				std::string names;
				for (const std::filesystem::path& file : listed) {
					names += (names.empty() ? "" : ", ") + nameIn(file, base);
				}
				fail(shortName + ": more than one file has a " + root + " root: " + names);
			}
		}
		return found;
	}

} // namespace assets

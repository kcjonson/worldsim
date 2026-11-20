#include "assets/CreatureConfigFiles.h"

#include "assets/ConfigFiles.h"

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

	} // namespace

	CreatureConfigFiles creatureConfigFiles(const std::filesystem::path& folder) {
		CreatureConfigFiles			found{.folder = folder};
		const std::string			folderName = folder.generic_string();
		const std::filesystem::path base = withoutTrailingSeparator(folder);
		const auto					fail = [&found](const std::string& message) {
			 found.errors.push_back(kPrefix + message);
		};

		std::error_code error;
		if (!std::filesystem::is_directory(base, error)) {
			fail(folderName + ": not a folder");
			return found;
		}

		// The primary names its helper files; when it cannot be read, nothing about the rest can be told.
		const std::filesystem::path	 primary = (base / (base.filename().string() + ".xml")).lexically_normal();
		pugi::xml_document			 definition;
		const pugi::xml_parse_result primaryParsed = definition.load_file(primary.string().c_str());
		if (!primaryParsed) {
			fail(configFileName(primary, base) + ": not readable XML: " + primaryParsed.description());
			return found;
		}
		std::set<std::filesystem::path> helpers;
		for (const pugi::xml_node assetDef : definition.child("AssetDefinitions").children("AssetDef")) {
			const std::string motion = assetDef.child_value("motion");
			if (!motion.empty()) {
				helpers.insert((base / motion).lexically_normal());
			}
		}

		std::vector<std::filesystem::path> files;
		std::vector<std::filesystem::path> subfolders;
		for (const auto& entry : std::filesystem::directory_iterator(base, error)) {
			const std::filesystem::path file = entry.path().lexically_normal();
			if (entry.is_directory()) {
				subfolders.push_back(entry.path());
			} else if (entry.is_regular_file() && file.extension() == ".xml" && file != primary && !helpers.contains(file)) {
				files.push_back(entry.path());
			}
		}
		if (error) {
			fail(folderName + ": " + error.message());
			return found;
		}
		std::ranges::sort(subfolders);
		std::ranges::sort(files);

		for (const std::filesystem::path& subfolder : subfolders) {
			fail(configFileName(subfolder, base) + ": a subfolder, which a creature folder may not have");
		}
		std::string roots;
		for (const RootList& list : kRootLists) {
			roots += (roots.empty() ? "<" : ", <") + std::string(list.root) + ">";
		}
		for (const std::filesystem::path& file : files) {
			const std::string			 source = configFileName(file, base);
			pugi::xml_document			 document;
			const pugi::xml_parse_result parsed = document.load_file(file.string().c_str());
			if (!parsed) {
				fail(source + ": not readable XML: " + parsed.description());
				continue;
			}
			const std::string_view root = document.document_element().name();
			const RootList*		   list = listFor(root);
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
				fail(folderName + ": no file has a " + root + " root");
			} else if (list.oneFile && listed.size() > 1) {
				std::string names;
				for (const std::filesystem::path& file : listed) {
					names += (names.empty() ? "" : ", ") + configFileName(file, base);
				}
				fail(folderName + ": more than one file has a " + root + " root: " + names);
			}
		}
		return found;
	}

	bool takeErrors(CreatureConfigFiles& found, std::vector<std::string>& into) {
		if (found.errors.empty()) {
			return false;
		}
		if (!found.errorsReported) {
			into.insert(into.end(), found.errors.begin(), found.errors.end());
			found.errorsReported = true;
		}
		return true;
	}

	bool isInCreatureFolder(const std::filesystem::path& file) {
		return file.parent_path().parent_path().filename() == "creatures";
	}

} // namespace assets

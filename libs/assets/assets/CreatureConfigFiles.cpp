#include "assets/CreatureConfigFiles.h"

#include <pugixml.hpp>

#include <algorithm>
#include <array>
#include <set>
#include <string_view>
#include <system_error>

namespace assets {

	namespace {

		/// Without a trailing separator, so filename() is the folder's own name.
		std::filesystem::path withoutTrailingSeparator(const std::filesystem::path& folder) {
			std::filesystem::path normal = folder.lexically_normal();
			if (!normal.has_filename() && normal.has_relative_path()) {
				normal = normal.parent_path();
			}
			return normal;
		}

		std::filesystem::path primaryFile(const std::filesystem::path& folder) {
			return folder / (folder.filename().string() + ".xml");
		}

		/// The files the primary definition names in a <motion> element, normalized.
		std::set<std::filesystem::path> motionFiles(const std::filesystem::path& folder) {
			std::set<std::filesystem::path> named;
			pugi::xml_document				document;
			if (!document.load_file(primaryFile(folder).string().c_str())) {
				return named;
			}
			for (const pugi::xml_node definition : document.child("AssetDefinitions").children("AssetDef")) {
				const std::string motion = definition.child_value("motion");
				if (!motion.empty()) {
					named.insert((folder / motion).lexically_normal());
				}
			}
			return named;
		}

		struct RootList {
			std::string_view root;
			ConfigFiles CreatureConfigFiles::* files;
		};

		constexpr std::array<RootList, 5> kRootLists = {{
			{.root = "Mood", .files = &CreatureConfigFiles::mood},
			{.root = "Memory", .files = &CreatureConfigFiles::memory},
			{.root = "Traits", .files = &CreatureConfigFiles::traits},
			{.root = "Attributes", .files = &CreatureConfigFiles::attributes},
			{.root = "Needs", .files = &CreatureConfigFiles::needs},
		}};

		const RootList* listFor(std::string_view root) {
			const auto found = std::ranges::find(kRootLists, root, &RootList::root);
			return found == kRootLists.end() ? nullptr : &*found;
		}

	} // namespace

	CreatureConfigFiles creatureConfigFiles(const std::filesystem::path& folder) {
		CreatureConfigFiles			found{.folder = folder};
		const std::filesystem::path base = withoutTrailingSeparator(folder);
		std::error_code				error;
		if (!std::filesystem::is_directory(base, error)) {
			found.errors.push_back(folder.generic_string() + ": not a folder");
			return found;
		}

		const std::filesystem::path			  primary = primaryFile(base).lexically_normal();
		const std::set<std::filesystem::path> helpers = motionFiles(base);
		std::vector<std::filesystem::path>	  files;
		for (const auto& entry : std::filesystem::directory_iterator(base, error)) {
			const std::filesystem::path file = entry.path().lexically_normal();
			if (entry.is_regular_file() && file.extension() == ".xml" && file != primary && !helpers.contains(file)) {
				files.push_back(entry.path());
			}
		}
		if (error) {
			found.errors.push_back(folder.generic_string() + ": " + error.message());
			return found;
		}
		std::ranges::sort(files);

		std::string roots;
		for (const RootList& list : kRootLists) {
			roots += (roots.empty() ? "<" : ", <") + std::string(list.root) + ">";
		}
		for (const std::filesystem::path& file : files) {
			const std::string			 source = configFileName(file, base);
			pugi::xml_document			 document;
			const pugi::xml_parse_result parsed = document.load_file(file.string().c_str());
			if (!parsed) {
				found.errors.push_back(source + ": not readable XML: " + parsed.description());
				continue;
			}
			const std::string_view root = document.document_element().name();
			const RootList*		   list = listFor(root);
			if (list == nullptr) {
				found.errors.push_back(
					source + ": the root element <" + std::string(root) + "> is not one a creature folder holds (" + roots + ")"
				);
				continue;
			}
			(found.*(list->files)).files.push_back(file);
		}
		return found;
	}

	bool isCreatureConfigFile(const std::filesystem::path& file) {
		if (file.parent_path().parent_path().filename() != "creatures") {
			return false;
		}
		pugi::xml_document document;
		if (!document.load_file(file.string().c_str())) {
			return false;
		}
		return listFor(document.document_element().name()) != nullptr;
	}

} // namespace assets

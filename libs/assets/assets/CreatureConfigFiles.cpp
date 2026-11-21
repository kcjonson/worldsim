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

		/// A folder as errors name it: by its last two parts (creatures/Human), the same wherever it was found
		/// from, the game's working directory or the asset validator's absolute root.
		std::string shortNameOf(const std::filesystem::path& folder) {
			const std::filesystem::path base = withoutTrailingSeparator(folder);
			return (base.parent_path().filename() / base.filename()).generic_string();
		}

		enum class PrimaryState { Absent, Unreadable, File };

	} // namespace

	CreatureConfigFiles creatureConfigFiles(const std::filesystem::path& folder) {
		CreatureConfigFiles			found{.folder = folder};
		const std::filesystem::path base = withoutTrailingSeparator(folder);
		const std::string			shortName = shortNameOf(folder);
		const auto					fail = [&found](const std::string& message) {
			 found.errors.push_back(kPrefix + message);
		};

		std::error_code error;
		if (!std::filesystem::is_directory(base, error)) {
			fail(shortName + (error && error != std::errc::no_such_file_or_directory ? ": " + error.message() : ": not a folder"));
			return found;
		}

		// The primary is matched to the folder's name exactly, as the asset registry matches it, on every platform:
		// the walk below finds it by name rather than asking the filesystem, which ignores case on Windows.
		const std::filesystem::path			primary = (base / (base.filename().string() + ".xml")).lexically_normal();
		PrimaryState						primaryState = PrimaryState::Absent;
		std::vector<std::filesystem::path>	files;
		std::vector<std::filesystem::path>	subfolders;
		std::filesystem::directory_iterator entries(base, error);
		for (const std::filesystem::directory_iterator end; !error && entries != end; entries.increment(error)) {
			const std::filesystem::directory_entry& entry = *entries;
			const std::filesystem::path				file = entry.path().lexically_normal();
			std::error_code							entryError;
			const bool								isFolder = entry.is_directory(entryError);
			const bool								isFile = !entryError && !isFolder && entry.is_regular_file(entryError);
			if (entryError) {
				fail(nameIn(file, base) + ": " + entryError.message());
				if (file == primary) {
					primaryState = PrimaryState::Unreadable;
				}
			} else if (isFolder) {
				subfolders.push_back(file);
			} else if (file == primary) {
				primaryState = isFile ? PrimaryState::File : PrimaryState::Unreadable;
			} else if (isFile && file.extension() == ".xml") {
				files.push_back(file);
			}
		}
		if (error) {
			fail(shortName + ": " + error.message());
			return found;
		}

		// The primary names its helper files; when it cannot be read, nothing about the rest can be told.
		if (primaryState == PrimaryState::Absent) {
			fail(nameIn(primary, base) + ": missing; it is the creature's definition");
			return found;
		}
		if (primaryState == PrimaryState::Unreadable) {
			if (found.errors.empty()) {
				fail(nameIn(primary, base) + ": not a file; it is the creature's definition");
			}
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
		std::erase_if(files, [&helpers](const std::filesystem::path& file) { return helpers.contains(file); });
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
			if (list == nullptr && root == "AssetDefinitions") {
				fail(
					source + ": asset definitions outside the definition file, which is named for its folder exactly (" +
					nameIn(primary, base) + ")"
				);
				continue;
			}
			if (list == nullptr) {
				fail(source + ": the root element <" + std::string(root) + "> is not one a creature folder holds (" + roots + ")");
				continue;
			}
			(found.*(list->files)).push_back(file);
		}

		for (const RootList& list : kRootLists) {
			const std::vector<std::filesystem::path>& listed = found.*(list.files);
			if (list.oneFile && listed.size() > 1) {
				std::string names;
				for (const std::filesystem::path& file : listed) {
					names += (names.empty() ? "" : ", ") + nameIn(file, base);
				}
				fail(shortName + ": more than one file has a <" + std::string(list.root) + "> root: " + names);
			}
		}
		return found;
	}

	std::vector<std::string> missingKinds(const CreatureConfigFiles& found) {
		std::vector<std::string> errors;
		for (const RootList& list : kRootLists) {
			if ((found.*(list.files)).empty()) {
				errors.push_back(
					std::string(kPrefix) + shortNameOf(found.folder) + ": no file has a <" + std::string(list.root) + "> root"
				);
			}
		}
		return errors;
	}

} // namespace assets

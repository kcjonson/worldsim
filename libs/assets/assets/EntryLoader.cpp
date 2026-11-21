// loadEntries: reading the library's folders, then the creatures, then resolving the whole. The per-entry parsers
// are in the *Reading.cpp files, ids and references in EntryResolution.cpp, the creatures in EntryCreatureLoading.cpp.

#include "assets/EntryLoader.h"

#include "assets/ConfigFiles.h"
#include "assets/EntryLoading.h"

#include <algorithm>
#include <iterator>

namespace assets {

	const CreatureEntry* EntryLibrary::findCreature(std::string_view name) const {
		const auto found = std::ranges::find(creatures, name, &CreatureEntry::name);
		return found == creatures.end() ? nullptr : &*found;
	}

	std::optional<NeedId> EntryLibrary::findNeed(std::string_view id) const {
		const auto found = needIndex.find(id);
		if (found == needIndex.end()) {
			return std::nullopt;
		}
		return NeedId{static_cast<uint16_t>(found->second)};
	}

	namespace entry_loading {

		void Loader::append(FileReader& file) {
			std::vector<std::string> problems = file.takeProblems();
			if (!problems.empty()) {
				result.errors.insert(result.errors.end(), problems.begin(), problems.end());
				return;
			}
			EntryLibrary& library = result.library;
			FileEntries&  entries = file.entries;
			const auto	  needOffset = static_cast<uint32_t>(library.needs.size());
			const auto	  behaviorOffset = static_cast<uint32_t>(library.behaviors.size());
			for (NeedEntry& need : entries.needs) {
				need.behavior.index += behaviorOffset;
				definedNeeds.insert(need.id.index);
				library.needs.push_back(std::move(need));
			}
			for (BehaviorEntry& behavior : entries.behaviors) {
				for (EntryHandle<NeedEntry>& need : behavior.needs) {
					need.index += needOffset;
				}
				library.behaviors.push_back(std::move(behavior));
			}
			const auto move = [](auto& from, auto& to) {
				std::move(from.begin(), from.end(), std::back_inserter(to));
			};
			move(entries.chains, library.chains);
			move(entries.queries, library.queries);
			move(entries.actions, library.actions);
			move(entries.categories, library.categories);
			move(entries.work, library.work);
			move(entries.orders, library.orders);
		}

		void Loader::readSharedFolder(std::string_view folderName, const char* rootName, FileKind kind) {
			const std::string			prefix = "[" + std::string(folderName) + "] ";
			const std::filesystem::path folder = sources.configRoot / folderName;
			const ConfigFiles			found = configFilesIn(folder);
			if (found.error) {
				fail(prefix + *found.error);
				return;
			}
			// The shared folders' entries take top-level ids, so a subfolder would add nothing to an id and is refused.
			std::set<std::string> subfolders;
			std::error_code		  walkError;
			for (std::filesystem::directory_iterator entry(folder, walkError), end; !walkError && entry != end;
				 entry.increment(walkError)) {
				std::error_code typeError;
				if (entry->is_directory(typeError)) {
					subfolders.insert(entry->path().filename().generic_string());
				}
			}
			for (const std::string& subfolder : subfolders) {
				fail(
					prefix + subfolder + ": a subfolder, which " + std::string(folderName) +
					"/ may not have; its entries take top-level ids"
				);
			}
			if (!subfolders.empty()) {
				return;
			}
			for (const std::filesystem::path& file : found.files) {
				const std::string		 source = prefix + configFileName(file, folder);
				pugi::xml_document		 document;
				FileReader				 reader(*this, source, "", kind);
				std::vector<std::string> rootErrors;
				const pugi::xml_node	 root = readRootElement(document, file, rootName, source, rootErrors);
				if (!root) {
					result.errors.insert(result.errors.end(), rootErrors.begin(), rootErrors.end());
					continue;
				}
				if (kind == FileKind::Facts) {
					reader.readFacts(root);
					std::vector<std::pair<uint16_t, float>> values = reader.entries.factValues;
					const std::size_t						errorsBefore = result.errors.size();
					append(reader);
					if (result.errors.size() == errorsBefore) {
						for (const auto& [fact, value] : values) {
							if (!factSources[fact].empty()) {
								fail(source + ": <fact id=" + inQuotes(kFacts[fact].name) + "> is also given in " + factSources[fact]);
								continue;
							}
							factSources[fact] = source;
							result.library.factTuning[fact] = value;
						}
					}
					continue;
				}
				if (kind == FileKind::Actions) {
					reader.readActions(root);
				} else if (kind == FileKind::Work) {
					reader.readWork(root);
				} else {
					reader.readOrders(root);
				}
				append(reader);
			}
		}

		void Loader::readBehaviorFile(const std::filesystem::path& file, const std::string& source, const std::string& fullBehaviorId) {
			pugi::xml_document		 document;
			std::vector<std::string> rootErrors;
			const pugi::xml_node	 root = readRootElement(document, file, "behavior", source, rootErrors);
			if (!root) {
				result.errors.insert(result.errors.end(), rootErrors.begin(), rootErrors.end());
				return;
			}
			FileReader reader(*this, source, std::string(folderOf(fullBehaviorId)), FileKind::Behavior);
			reader.readBehavior(root, fullBehaviorId);
			append(reader);
		}

		void Loader::readBehaviorsFolder() {
			const std::filesystem::path folder = sources.configRoot / "behaviors";
			const ConfigFiles			found = configFilesIn(folder);
			if (found.error) {
				fail("[behaviors] " + *found.error);
				return;
			}
			for (const std::filesystem::path& file : found.files) {
				const std::string name = configFileName(file, folder);
				const std::string source = "[behaviors] " + name;
				const std::string fullBehaviorId = name.substr(0, name.size() - file.extension().string().size());
				if (folderOf(fullBehaviorId).empty()) {
					fail(source + ": a behavior file sits in a folder under behaviors/, the folder its id starts with");
					continue;
				}
				if (!idPathIsClean(source, fullBehaviorId)) {
					continue;
				}
				readBehaviorFile(file, source, fullBehaviorId);
			}
		}

		bool Loader::idPathIsClean(const std::string& source, std::string_view path) {
			bool		clean = true;
			std::size_t start = 0;
			while (start <= path.size()) {
				const std::size_t	   slash = path.find('/', start);
				const std::size_t	   end = slash == std::string_view::npos ? path.size() : slash;
				const std::string_view part = path.substr(start, end - start);
				if (!isIdPart(part)) {
					fail(
						source + ": the " + (slash == std::string_view::npos ? "file" : "folder") + " name " + inQuotes(part) +
						" must be " + std::string(kIdPartRule)
					);
					clean = false;
				}
				if (slash == std::string_view::npos) {
					break;
				}
				start = slash + 1;
			}
			return clean;
		}

		void Loader::checkFactTuning() {
			for (std::size_t index = 0; index < kFacts.size(); ++index) {
				if (!kFacts[index].tuning.empty() && factSources[index].empty()) {
					fail(
						"[facts] <fact id=" + inQuotes(kFacts[index].name) + "> " + std::string(kFacts[index].tuning) +
						" is given by no file in facts/"
					);
				}
			}
		}

		EntryLoad Loader::run() {
			readBehaviorsFolder();
			readSharedFolder("actions", "actions", FileKind::Actions);
			readSharedFolder("work", "colonyWork", FileKind::Work);
			readSharedFolder("orders", "orders", FileKind::Orders);
			readSharedFolder("facts", "facts", FileKind::Facts);
			readCreatures();
			if (result.errors.empty()) {
				checkFactTuning();
			}
			if (result.errors.empty()) {
				registerIds();
				resolve();
				resolveCreatures();
				checkNeedReferences();
				checkChains();
			}
			if (!result.errors.empty()) {
				result.warnings.clear();
				result.library = EntryLibrary{};
				return std::move(result);
			}
			warnUnstarted();
			const EntryLibrary& library = result.library;
			LOG_INFO(
				Engine,
				"Loaded %zu behaviors, %zu needs, %zu chains, %zu queries, %zu actions, %zu work entries, %zu orders, %zu creatures",
				library.behaviors.size(),
				library.needs.size(),
				library.chains.size(),
				library.queries.size(),
				library.actions.size(),
				library.work.size(),
				library.orders.size(),
				library.creatures.size()
			);
			return std::move(result);
		}

	} // namespace entry_loading

	EntryLoad loadEntries(const EntrySources& sources) {
		return entry_loading::Loader(sources).run();
	}

} // namespace assets

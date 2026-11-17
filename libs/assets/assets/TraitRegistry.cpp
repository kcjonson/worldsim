#include "assets/TraitRegistry.h"

#include "assets/ConfigNumber.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <algorithm>
#include <initializer_list>
#include <map>
#include <optional>
#include <sstream>
#include <utility>

namespace assets {

	namespace {

		bool isSnakeCase(std::string_view id) {
			if (id.empty() || id.front() < 'a' || id.front() > 'z') {
				return false;
			}
			return std::ranges::all_of(id, [](char character) {
				return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_';
			});
		}

		std::vector<std::string> splitNames(const char* text) {
			std::vector<std::string> names;
			std::istringstream		 stream(text);
			std::string				 name;
			while (stream >> name) {
				names.push_back(std::move(name));
			}
			return names;
		}

		bool excludes(const std::vector<std::vector<std::string>>& groups, const std::string& first, const std::string& second) {
			return std::ranges::any_of(groups, [&](const std::vector<std::string>& group) {
				return std::ranges::find(group, first) != group.end() && std::ranges::find(group, second) != group.end();
			});
		}

		/// Whether `wanted` of `ids`, from `from` on, can be chosen on top of `chosen` with no pair excluded.
		bool canChoose(
			const std::vector<std::string>& ids, const std::vector<std::vector<std::string>>& groups, std::vector<std::string>& chosen, std::size_t from,
			std::size_t wanted
		) {
			if (chosen.size() >= wanted) {
				return true;
			}
			for (std::size_t next = from; next < ids.size() && chosen.size() + (ids.size() - next) >= wanted; ++next) {
				if (std::ranges::any_of(chosen, [&](const std::string& held) { return excludes(groups, held, ids[next]); })) {
					continue;
				}
				chosen.push_back(ids[next]);
				if (canChoose(ids, groups, chosen, next + 1, wanted)) {
					return true;
				}
				chosen.pop_back();
			}
			return false;
		}

		struct ExclusiveGroup {
			std::vector<std::string> ids;
			std::string				 source;
		};

		class Loader {
		  public:
			Loader(std::vector<TraitDef>& newTraits, TraitCount& newCount, std::vector<std::vector<std::string>>& newGroups)
				: traits(newTraits)
				, count(newCount)
				, groups(newGroups) {}

			std::vector<std::string> errors;

			void loadFile(const std::filesystem::path& path, const std::string& source) {
				pugi::xml_document			 document;
				const pugi::xml_parse_result parsed = document.load_file(path.string().c_str());
				if (!parsed) {
					fail(source, std::string("not readable XML: ") + parsed.description());
					return;
				}
				const pugi::xml_node root = document.child("Traits");
				if (!root) {
					fail(source, "the root element is not <Traits>");
					return;
				}
				std::vector<std::string> declaredHere;
				for (const pugi::xml_node node : root.children()) {
					if (node.type() != pugi::node_element) {
						continue;
					}
					const std::string_view element = node.name();
					if (element == "Trait") {
						loadTrait(node, source, declaredHere);
					} else if (element == "Count") {
						loadCount(node, source);
					} else if (element == "Exclusive") {
						loadExclusive(node, source);
					} else {
						fail(source, "unknown element <" + std::string(element) + ">");
					}
				}
			}

			void check() {
				for (const ExclusiveGroup& group : pending) {
					for (const std::string& id : group.ids) {
						if (!isDeclared(id)) {
							fail(group.source, "<Exclusive> names '" + id + "', which no file declares as a trait");
						}
					}
					groups.push_back(group.ids);
				}
				for (const TraitDef& trait : traits) {
					allIds.push_back(trait.id);
				}
				if (traits.empty()) {
					fail("traits", "no file declares a <Trait>");
				}
				if (!countSource.has_value()) {
					fail("traits", "no file gives <Count> min and max");
				} else if (static_cast<std::size_t>(count.min) > traits.size()) {
					fail(*countSource, "<Count> min is " + std::to_string(count.min) + ", more than the " + std::to_string(traits.size()) + " declared traits");
				} else if (std::vector<std::string> chosen; !canChoose(allIds, groups, chosen, 0, static_cast<std::size_t>(count.min))) {
					fail(
						*countSource,
						"<Count> min is " + std::to_string(count.min) + ", but the <Exclusive> groups leave no way to hold that many of the " +
							std::to_string(traits.size()) + " declared traits at once"
					);
				}
			}

		  private:
			void fail(const std::string& source, const std::string& message) { errors.push_back(source + ": " + message); }

			/// False, with each one reported, when the element has an attribute it does not take.
			bool onlyAttributes(pugi::xml_node node, std::initializer_list<std::string_view> allowed, const std::string& what, const std::string& source) {
				bool clean = true;
				for (const pugi::xml_attribute attribute : node.attributes()) {
					if (std::ranges::find(allowed, std::string_view(attribute.name())) == allowed.end()) {
						fail(source, what + ": unknown attribute '" + attribute.name() + "'");
						clean = false;
					}
				}
				return clean;
			}

			bool isDeclared(const std::string& id) const {
				return std::ranges::any_of(traits, [&](const TraitDef& trait) { return trait.id == id; });
			}

			void loadTrait(pugi::xml_node node, const std::string& source, std::vector<std::string>& declaredHere) {
				TraitDef trait{
					.id = node.attribute("id").as_string(),
					.name = node.attribute("name").as_string(),
					.description = node.attribute("description").as_string(),
				};
				if (!isSnakeCase(trait.id)) {
					fail(source, "a <Trait> needs an id in snake_case, not '" + trait.id + "'");
					return;
				}
				const std::string what = "trait '" + trait.id + "'";
				if (!onlyAttributes(node, {"id", "name", "description"}, what, source)) {
					return;
				}
				if (trait.name.empty()) {
					fail(source, what + " has no name");
					return;
				}
				if (trait.description.empty()) {
					fail(source, what + " has no description");
					return;
				}
				if (std::ranges::find(declaredHere, trait.id) != declaredHere.end()) {
					fail(source, what + " is declared twice in this file");
					return;
				}
				declaredHere.push_back(trait.id);
				std::string& declaredIn = traitSources[trait.id];
				const auto	 existing = std::ranges::find(traits, trait.id, &TraitDef::id);
				if (existing == traits.end()) {
					traits.push_back(std::move(trait));
				} else {
					LOG_INFO(Engine, "Trait %s: %s replaces the declaration in %s", trait.id.c_str(), source.c_str(), declaredIn.c_str());
					*existing = std::move(trait);
				}
				declaredIn = source;
			}

			void loadCount(pugi::xml_node node, const std::string& source) {
				if (!onlyAttributes(node, {"min", "max"}, "<Count>", source)) {
					return;
				}
				ConfigReader reader(source);
				const int	 min = reader.attributeInteger(node, "Count", "min", 0);
				const int	 max = reader.attributeInteger(node, "Count", "max", 0);
				if (std::vector<std::string> problems = reader.takeProblems(); !problems.empty()) {
					errors.insert(errors.end(), problems.begin(), problems.end());
					return;
				}
				if (min > max) {
					fail(source, "<Count> min is " + std::to_string(min) + ", more than max " + std::to_string(max));
					return;
				}
				if (countSource.has_value()) {
					LOG_INFO(Engine, "Trait <Count>: %s replaces the range from %s", source.c_str(), countSource->c_str());
				}
				count = {.min = min, .max = max};
				countSource = source;
			}

			void loadExclusive(pugi::xml_node node, const std::string& source) {
				std::vector<std::string> ids = splitNames(node.text().as_string());
				const std::string		 what = "<Exclusive>" + std::string(node.text().as_string()) + "</Exclusive>";
				if (!onlyAttributes(node, {}, what, source)) {
					return;
				}
				if (ids.size() < 2) {
					fail(source, what + ": lists at least two trait ids");
					return;
				}
				std::vector<std::string> sorted = ids;
				std::ranges::sort(sorted);
				if (std::ranges::adjacent_find(sorted) != sorted.end()) {
					fail(source, what + ": lists a trait id twice");
					return;
				}
				pending.push_back({.ids = std::move(ids), .source = source});
			}

			std::vector<TraitDef>&				   traits;
			TraitCount&							   count;
			std::vector<std::vector<std::string>>& groups;
			std::vector<ExclusiveGroup>			   pending;
			std::vector<std::string>			   allIds;
			std::map<std::string, std::string>	   traitSources;
			std::optional<std::string>			   countSource;
		};

	} // namespace

	TraitRegistry& TraitRegistry::Get() {
		static TraitRegistry instance;
		return instance;
	}

	std::vector<std::string> TraitRegistry::loadFromFolder(const std::filesystem::path& folder) {
		clear();
		std::error_code error;
		if (!std::filesystem::is_directory(folder, error)) {
			return {folder.generic_string() + ": not a folder"};
		}

		std::vector<std::filesystem::path> files;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(folder, error)) {
			if (entry.is_regular_file() && entry.path().extension() == ".xml") {
				files.push_back(entry.path());
			}
		}
		if (error) {
			return {folder.generic_string() + ": " + error.message()};
		}
		std::ranges::sort(files, {}, [&folder](const std::filesystem::path& file) { return std::pair(file.parent_path() != folder, file); });

		Loader loader(traitList, traitCount, exclusiveGroups);
		for (const std::filesystem::path& file : files) {
			loader.loadFile(file, std::filesystem::relative(file, folder).generic_string());
		}
		loader.check();
		if (!loader.errors.empty()) {
			clear();
		}
		return std::move(loader.errors);
	}

	void TraitRegistry::clear() {
		traitList.clear();
		traitCount = {};
		exclusiveGroups.clear();
	}

	const TraitDef* TraitRegistry::find(std::string_view id) const {
		const auto found = std::ranges::find(traitList, id, &TraitDef::id);
		return found == traitList.end() ? nullptr : &*found;
	}

	bool TraitRegistry::exclusive(std::string_view first, std::string_view second) const {
		return std::ranges::any_of(exclusiveGroups, [&](const std::vector<std::string>& group) {
			return std::ranges::find(group, first) != group.end() && std::ranges::find(group, second) != group.end();
		});
	}

} // namespace assets

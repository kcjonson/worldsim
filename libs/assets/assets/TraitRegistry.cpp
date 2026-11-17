#include "assets/TraitRegistry.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <algorithm>
#include <charconv>
#include <map>
#include <optional>
#include <sstream>
#include <utility>

namespace assets {

	namespace {

		/// The attribute's whole text as an integer; nothing for a missing attribute, empty text, or text
		/// that is not entirely an integer.
		std::optional<int> integerOf(pugi::xml_attribute attribute) {
			const std::string_view text = attribute.as_string();
			int					   number = 0;
			const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
			if (text.empty() || error != std::errc() || end != text.data() + text.size()) {
				return std::nullopt;
			}
			return number;
		}

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
				if (traits.empty()) {
					fail("traits", "no file declares a <Trait>");
				}
				if (!countSource.has_value()) {
					fail("traits", "no file gives <Count> min and max");
				} else if (static_cast<std::size_t>(count.min) > traits.size()) {
					fail(*countSource, "<Count> min is " + std::to_string(count.min) + ", more than the " + std::to_string(traits.size()) + " declared traits");
				}
			}

		  private:
			void fail(const std::string& source, const std::string& message) { errors.push_back(source + ": " + message); }

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
				const std::optional<int> min = integerOf(node.attribute("min"));
				const std::optional<int> max = integerOf(node.attribute("max"));
				if (!min.has_value() || !max.has_value()) {
					fail(source, "<Count> needs min and max, whole numbers");
					return;
				}
				if (*min < 0 || *min > *max) {
					fail(source, "<Count> min must be at least 0 and no more than max");
					return;
				}
				if (countSource.has_value()) {
					LOG_INFO(Engine, "Trait <Count>: %s replaces the range from %s", source.c_str(), countSource->c_str());
				}
				count = {.min = *min, .max = *max};
				countSource = source;
			}

			void loadExclusive(pugi::xml_node node, const std::string& source) {
				std::vector<std::string> ids = splitNames(node.text().as_string());
				if (ids.size() < 2) {
					fail(source, "an <Exclusive> lists at least two trait ids");
					return;
				}
				std::vector<std::string> sorted = ids;
				std::ranges::sort(sorted);
				if (std::ranges::adjacent_find(sorted) != sorted.end()) {
					fail(source, "an <Exclusive> lists a trait id twice");
					return;
				}
				pending.push_back({.ids = std::move(ids), .source = source});
			}

			std::vector<TraitDef>&				   traits;
			TraitCount&							   count;
			std::vector<std::vector<std::string>>& groups;
			std::vector<ExclusiveGroup>			   pending;
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

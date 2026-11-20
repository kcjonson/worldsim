#include "assets/TraitRegistry.h"

#include "assets/ConditionConfig.h"
#include "assets/ConfigFiles.h"
#include "assets/ConfigNumber.h"
#include "assets/ConfigXml.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <algorithm>
#include <map>
#include <optional>
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

		std::string reservedWordList() {
			std::string list;
			for (const std::string_view word : kConditionReservedWords) {
				list += (list.empty() ? "" : ", ") + std::string(word);
			}
			return list;
		}

		struct ExclusiveGroup {
			std::vector<std::string> ids;
			std::string				 source;
		};

		class Loader {
		  public:
			std::vector<TraitDef>		traits;
			TraitCount					count;
			std::optional<std::string>	countSource;
			std::vector<ExclusiveGroup> groups;
			std::vector<std::string>	errors;

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
				for (const ExclusiveGroup& group : groups) {
					for (const std::string& id : group.ids) {
						if (std::ranges::find(traits, id, &TraitDef::id) == traits.end()) {
							fail(group.source, "<Exclusive> names '" + id + "', which no file declares as a trait");
						}
					}
				}
				if (traits.empty()) {
					fail("traits", "no file declares a <Trait>");
				}
				if (!countGiven) {
					fail("traits", "no file gives <Count> min and max");
				}
			}

		  private:
			void fail(const std::string& source, const std::string& message) { errors.push_back(source + ": " + message); }

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
				if (std::ranges::find(kConditionReservedWords, trait.id) != kConditionReservedWords.end()) {
					fail(source, what + ": the id is a word of the condition language (" + reservedWordList() + ")");
					return;
				}
				if (!onlyAttributes(node, {"id", "name", "description"}, what, source, errors)) {
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
				countGiven = true;
				if (!onlyAttributes(node, {"min", "max"}, "<Count>", source, errors)) {
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
				if (!onlyAttributes(node, {}, what, source, errors)) {
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
				groups.push_back({.ids = std::move(ids), .source = source});
			}

			std::map<std::string, std::string> traitSources;
			bool							   countGiven = false;
		};

	} // namespace

	std::vector<std::string>
	TraitRegistry::loadFromFiles(const std::vector<std::filesystem::path>& files, const std::filesystem::path& folder) {
		clear();
		Loader loader;
		for (const std::filesystem::path& file : files) {
			loader.loadFile(file, configFileName(file, folder));
		}
		loader.check();
		if (!loader.errors.empty()) {
			return std::move(loader.errors);
		}

		traitList = std::move(loader.traits);
		traitCount = loader.count;
		excludes.assign(traitList.size(), std::vector<bool>(traitList.size(), false));
		for (const ExclusiveGroup& group : loader.groups) {
			std::vector<std::size_t>& members = exclusiveGroups.emplace_back();
			for (const std::string& id : group.ids) {
				members.push_back(static_cast<std::size_t>(std::ranges::find(traitList, id, &TraitDef::id) - traitList.begin()));
			}
			for (const std::size_t first : members) {
				for (const std::size_t second : members) {
					excludes[first][second] = first != second;
				}
			}
		}

		const auto	min = static_cast<std::size_t>(traitCount.min);
		std::string problem;
		if (min > traitList.size()) {
			problem = "<Count> min is " + std::to_string(min) + ", more than the " + std::to_string(traitList.size()) + " declared traits";
		} else if (!reachable({}, min)) {
			problem = "<Count> min is " + std::to_string(min) + ", but the <Exclusive> groups leave no way to hold that many of the " +
					  std::to_string(traitList.size()) + " declared traits at once";
		}
		if (!problem.empty()) {
			clear();
			return {*loader.countSource + ": " + problem};
		}
		return {};
	}

	void TraitRegistry::clear() {
		traitList.clear();
		traitCount = {};
		exclusiveGroups.clear();
		excludes.clear();
	}

	const TraitDef* TraitRegistry::find(std::string_view id) const {
		const auto found = std::ranges::find(traitList, id, &TraitDef::id);
		return found == traitList.end() ? nullptr : &*found;
	}

	bool TraitRegistry::exclusive(std::string_view first, std::string_view second) const {
		const TraitDef* firstTrait = find(first);
		const TraitDef* secondTrait = find(second);
		return firstTrait != nullptr && secondTrait != nullptr &&
			   excludes[static_cast<std::size_t>(firstTrait - traitList.data())][static_cast<std::size_t>(secondTrait - traitList.data())];
	}

	bool TraitRegistry::reachable(std::span<const std::size_t> held, std::size_t wanted) const {
		for (std::size_t at = 0; at < held.size(); ++at) {
			for (std::size_t next = at + 1; next < held.size(); ++next) {
				if (excludes[held[at]][held[next]]) {
					return false;
				}
			}
		}
		if (held.size() >= wanted) {
			return true;
		}
		std::vector<std::size_t> candidates;
		for (std::size_t trait = 0; trait < traitList.size(); ++trait) {
			if (std::ranges::none_of(held, [&](std::size_t holding) { return holding == trait || excludes[holding][trait]; })) {
				candidates.push_back(trait);
			}
		}
		return fits(candidates, wanted - held.size());
	}

	/// Each step takes the first candidate or leaves it, and gives up on a branch whose candidates cannot
	/// hold `wanted` even by the group bound.
	bool TraitRegistry::fits(const std::vector<std::size_t>& candidates, std::size_t wanted) const {
		if (wanted == 0) {
			return true;
		}
		if (mostHeldOf(candidates) < wanted) {
			return false;
		}
		const std::vector<std::size_t> rest(candidates.begin() + 1, candidates.end());
		std::vector<std::size_t>	   compatible;
		for (const std::size_t trait : rest) {
			if (!excludes[candidates.front()][trait]) {
				compatible.push_back(trait);
			}
		}
		return fits(compatible, wanted - 1) || fits(rest, wanted);
	}

	/// An upper bound on how many of `candidates` one colonist can hold: at most one of each exclusive
	/// group's candidates that no earlier group already counted.
	std::size_t TraitRegistry::mostHeldOf(const std::vector<std::size_t>& candidates) const {
		std::vector<bool> isCandidate(traitList.size(), false);
		for (const std::size_t trait : candidates) {
			isCandidate[trait] = true;
		}
		std::size_t most = candidates.size();
		for (const std::vector<std::size_t>& group : exclusiveGroups) {
			std::size_t members = 0;
			for (const std::size_t trait : group) {
				if (isCandidate[trait]) {
					isCandidate[trait] = false;
					++members;
				}
			}
			if (members > 1) {
				most -= members - 1;
			}
		}
		return most;
	}

} // namespace assets

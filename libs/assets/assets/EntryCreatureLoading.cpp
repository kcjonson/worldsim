// The creature loader: each creature folder's <creature> block (on the base definition and in <creature> files) and
// its own behavior files, settled into a creature with its behaviors in order and every need's values. See
// /docs/technical/queries-and-chains.md (Loading a creature).

#include "assets/EntryLoading.h"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <map>

namespace assets::entry_loading {

	// --- Reading the rows ------------------------------------------------------------------------------------

	std::optional<CreatureNeedRow> FileReader::creatureNeed(pugi::xml_node node, const std::string& label) {
		attributesOnly(node, {"id", "from", "label", "seek", "urgent", "critical"}, label);
		childrenOnly(node, {"drains", "setBy", "satisfiedBy"}, label);
		CreatureNeedRow row{.source = source};
		if (const std::optional<NeedId> id = needName(node, "id", label)) {
			row.id = node.attribute("id").value();
		}
		row.from = node.attribute("from").value();
		if (!node.attribute("from").empty() && row.from.empty()) {
			refuse(label, node, "from", "a behavior id");
		}
		if (!node.attribute("label").empty()) {
			row.label = required(node, "label", label);
		}
		for (const auto& [attribute, value] :
			 {std::pair{"seek", &row.seek}, std::pair{"urgent", &row.urgent}, std::pair{"critical", &row.critical}}) {
			if (!node.attribute(attribute).empty()) {
				*value = boundedNumber(node, attribute, label, 0.0F, 100.0F);
			}
		}
		row.move = needMove(node, label, false);
		if (node.child("satisfiedBy")) {
			row.satisfiedBy = satisfiedByRows(node, label);
		}
		if (row.id.empty()) {
			return std::nullopt;
		}
		return row;
	}

	void FileReader::readCreatureBlock(pugi::xml_node block, bool primary, CreatureRows& rows) {
		const std::string label = "<creature>";
		reserveScript(block, label);
		if (primary) {
			attributesOnly(block, {"mood", "script"}, label);
			if (block.attribute("mood").empty()) {
				refuse(label, block, "mood", "");
			} else {
				rows.mood = boolean(block, "mood", label).value_or(false);
			}
		} else {
			attributesOnly(block, {"script"}, label);
		}
		childrenOnly(block, {"behavior", "need", "moodCost"}, label);
		std::size_t index = 0;
		for (const pugi::xml_node child : block.children()) {
			if (child.type() != pugi::node_element) {
				continue;
			}
			const std::string_view element = child.name();
			const std::string	   where = label + " " + rowLabel(index, element);
			if (element == "behavior") {
				attributesOnly(child, {"id"}, where);
				childrenOnly(child, {}, where);
				const std::string id = required(child, "id", where);
				if (!id.empty()) {
					rows.includes.push_back(CreatureInclude{.source = source, .row = index, .id = id});
				}
			} else if (element == "need") {
				if (std::optional<CreatureNeedRow> row = creatureNeed(child, "<need id=" + inQuotes(child.attribute("id").value()) + ">")) {
					rows.needs.push_back(std::move(*row));
				}
			} else if (element == "moodCost") {
				attributesOnly(child, {"need", "cost"}, where);
				childrenOnly(child, {}, where);
				CreatureCostRow cost{
					.source = source, .cost = boundedNumber(child, "cost", where, 0.0F, std::numeric_limits<float>::max())
				};
				if (needName(child, "need", where)) {
					cost.need = child.attribute("need").value();
					rows.costs.push_back(std::move(cost));
				}
			}
			++index;
		}
	}

	// --- Reading the folders ---------------------------------------------------------------------------------

	void Loader::readCreatures() {
		for (const CreatureConfigFiles& creature : sources.creatures) {
			assert(creature.errors.empty() && "loadEntries takes creature folders with no structural errors");
			std::filesystem::path folder = creature.folder.lexically_normal();
			if (!folder.has_filename() && folder.has_relative_path()) {
				folder = folder.parent_path();
			}
			const std::string name = folder.filename().string();
			CreatureRows	  rows{.name = name, .primarySource = "[creatures] " + name + "/" + name + ".xml"};
			// The folder's name starts the ids of the creature's own behaviors.
			if (!isIdPart(name)) {
				fail(rows.primarySource + ": the folder name " + inQuotes(name) + " must be " + std::string(kIdPartRule));
				continue;
			}

			pugi::xml_document			 definitions;
			const pugi::xml_parse_result parsed = definitions.load_file((folder / (name + ".xml")).string().c_str());
			if (!parsed) {
				fail(rows.primarySource + ": not readable XML: " + parsed.description());
				continue;
			}
			FileReader				 primary(*this, rows.primarySource, name, FileKind::CreatureBlock);
			pugi::xml_node			 base;
			std::vector<std::string> placement;
			for (const pugi::xml_node definition : definitions.child("AssetDefinitions").children("AssetDef")) {
				const std::string defName = ConfigReader::describe(definition, "AssetDef");
				const bool		  isBase = std::string_view(definition.child_value("defName")) == name;
				const auto		  blocks = definition.children("creature");
				const auto		  count = std::distance(blocks.begin(), blocks.end());
				if (isBase) {
					base = definition;
					if (count != 1) {
						placement.push_back(
							rows.primarySource + ": " + defName +
							(count == 0 ? " has no <creature> block; the base definition of a creature folder carries one"
										: " has more than one <creature> block")
						);
					}
				} else if (count > 0) {
					placement.push_back(
						rows.primarySource + ": " + defName + " has a <creature> block, which only the base definition (" + name +
						") carries"
					);
				}
			}
			if (!base) {
				placement.push_back(
					rows.primarySource + ": no <AssetDef> has the folder's name, " + inQuotes(name) +
					", so the creature has no base definition"
				);
			}
			if (!placement.empty()) {
				result.errors.insert(result.errors.end(), placement.begin(), placement.end());
				continue;
			}
			primary.readCreatureBlock(base.child("creature"), true, rows);
			std::vector<std::string> problems = primary.takeProblems();
			result.errors.insert(result.errors.end(), problems.begin(), problems.end());

			for (const std::filesystem::path& file : creature.creature) {
				const std::string		 source = "[creatures] " + name + "/" + file.filename().string();
				pugi::xml_document		 document;
				std::vector<std::string> rootErrors;
				const pugi::xml_node	 root = readRootElement(document, file, "creature", source, rootErrors);
				if (!root) {
					result.errors.insert(result.errors.end(), rootErrors.begin(), rootErrors.end());
					continue;
				}
				FileReader	 reader(*this, source, name, FileKind::CreatureBlock);
				CreatureRows fileRows;
				reader.readCreatureBlock(root, false, fileRows);
				std::vector<std::string> fileProblems = reader.takeProblems();
				if (!fileProblems.empty()) {
					result.errors.insert(result.errors.end(), fileProblems.begin(), fileProblems.end());
					continue;
				}
				std::ranges::move(fileRows.includes, std::back_inserter(rows.includes));
				std::ranges::move(fileRows.needs, std::back_inserter(rows.needs));
				std::ranges::move(fileRows.costs, std::back_inserter(rows.costs));
			}
			for (const std::filesystem::path& file : creature.behaviors) {
				const std::string stem = file.stem().string();
				const std::string fullBehaviorId = name + "/" + stem;
				const std::string source = "[creatures] " + name + "/" + file.filename().string();
				if (!idPathIsClean(source, fullBehaviorId)) {
					continue;
				}
				readBehaviorFile(file, source, fullBehaviorId);
				rows.ownBehaviors.push_back(fullBehaviorId);
			}
			creatureRows.push_back(std::move(rows));
		}
	}

	// --- Settling each creature ------------------------------------------------------------------------------

	void Loader::resolveCreatures() {
		EntryLibrary& library = result.library;
		for (CreatureRows& rows : creatureRows) {
			CreatureEntry	  creature{.name = rows.name, .mood = rows.mood};
			const std::size_t errorsBefore = result.errors.size();

			// The creature's order: the primary block's includes, then its <creature> files', then its own
			// behavior files not already placed by a row.
			// One fault gives one error: when an include names no behavior, nothing can tell which needs it would
			// have brought, so a row or a mood cost for a need no included behavior brings says nothing more; nor
			// does a mood cost for a need whose behaviors are left unsettled.
			std::map<uint32_t, std::string> placedBy;
			bool							includeFailed = false;
			std::set<std::string>			unsettled;
			for (CreatureInclude& include : rows.includes) {
				const std::string where = include.source + ": <creature> " + rowLabel(include.row, "behavior");
				const auto		  found = library.behaviorIds.find(include.id);
				if (found == library.behaviorIds.end()) {
					fail(where + " id " + inQuotes(include.id) + " names no behavior");
					includeFailed = true;
					continue;
				}
				const auto [placed, added] = placedBy.try_emplace(found->second, where);
				if (!added) {
					fail(where + " id " + inQuotes(include.id) + " includes a behavior already included at " + placed->second);
					continue;
				}
				creature.behaviors.push_back(EntryHandle<BehaviorEntry>{found->second});
			}
			for (const std::string& own : rows.ownBehaviors) {
				const auto found = library.behaviorIds.find(own);
				if (found != library.behaviorIds.end() && !placedBy.contains(found->second)) {
					placedBy.emplace(found->second, library.behaviors[found->second].file);
					creature.behaviors.push_back(EntryHandle<BehaviorEntry>{found->second});
				}
			}

			// Each need id the included behaviors bring, with every behavior that brings it, in order.
			std::vector<std::pair<std::string, std::vector<uint32_t>>> brought;
			for (const EntryHandle<BehaviorEntry> behavior : creature.behaviors) {
				for (const EntryHandle<NeedEntry> need : library.behaviors[behavior.index].needs) {
					const std::string& id = library.needIds[library.needs[need.index].id.index];
					auto			   found = std::ranges::find(brought, id, &std::pair<std::string, std::vector<uint32_t>>::first);
					if (found == brought.end()) {
						brought.emplace_back(id, std::vector<uint32_t>{});
						found = std::prev(brought.end());
					}
					found->second.push_back(need.index);
				}
			}
			std::vector<std::string> needOrder;
			for (const auto& [id, needs] : brought) {
				needOrder.push_back(id);
			}
			for (const CreatureNeedRow& row : rows.needs) {
				if (std::ranges::find(needOrder, row.id) == needOrder.end()) {
					needOrder.push_back(row.id);
				}
			}

			for (const std::string& id : needOrder) {
				const auto broughtBy = std::ranges::find(brought, id, &std::pair<std::string, std::vector<uint32_t>>::first);
				std::vector<const CreatureNeedRow*> needRows;
				for (const CreatureNeedRow& row : rows.needs) {
					if (row.id == id) {
						needRows.push_back(&row);
					}
				}
				const std::string needLabel = "<need id=" + inQuotes(id) + ">";

				// Settling: which behavior's entry the need takes when several bring it.
				const CreatureNeedRow* settledBy = nullptr;
				for (const CreatureNeedRow* row : needRows) {
					if (row->from.empty()) {
						continue;
					}
					if (settledBy != nullptr) {
						fail(row->source + ": " + needLabel + " from is also given in " + settledBy->source);
						continue;
					}
					settledBy = row;
				}
				CreatureNeed	 need{.id = *library.findNeed(id)};
				const NeedEntry* base = nullptr;
				if (broughtBy != brought.end()) {
					if (settledBy != nullptr) {
						const auto from = library.behaviorIds.find(settledBy->from);
						const auto match = std::ranges::find_if(broughtBy->second, [&](uint32_t candidate) {
							return from != library.behaviorIds.end() && library.needs[candidate].behavior.index == from->second;
						});
						if (match == broughtBy->second.end()) {
							fail(
								settledBy->source + ": " + needLabel + " from " + inQuotes(settledBy->from) +
								" names no included behavior that brings the need"
							);
							unsettled.insert(id);
						} else {
							base = &library.needs[*match];
						}
					} else if (broughtBy->second.size() > 1) {
						std::vector<std::string> names;
						for (const uint32_t candidate : broughtBy->second) {
							names.push_back(inQuotes(library.behaviors[library.needs[candidate].behavior.index].id));
						}
						fail(
							rows.primarySource + ": <creature> need " + inQuotes(id) + " comes from more than one included behavior (" +
							joined(names) + "); settle it with <need id=" + inQuotes(id) + " from=\"...\"/>"
						);
						unsettled.insert(id);
					} else {
						base = &library.needs[broughtBy->second.front()];
					}
				} else if (settledBy != nullptr) {
					fail(
						settledBy->source + ": " + needLabel + " from " + inQuotes(settledBy->from) +
						" settles a need no included behavior brings"
					);
				}

				std::optional<std::string> label;
				std::optional<float>	   seek;
				std::optional<float>	   urgent;
				std::optional<float>	   critical;
				std::optional<NeedMove>	   move;
				std::vector<SatisfiedBy>   satisfiedBy;
				if (base != nullptr) {
					label = base->label;
					seek = base->seek;
					urgent = base->urgent;
					critical = base->critical;
					move = base->move;
					satisfiedBy = base->satisfiedBy;
					need.behavior = base->behavior;
				}
				// Each value is set by one row at most; two rows setting it are an error naming both files.
				std::map<std::string, std::string, std::less<>> setBy;
				const auto										set = [&](const CreatureNeedRow& row, std::string_view value) {
					 const auto [found, added] = setBy.try_emplace(std::string(value), row.source);
					 if (!added) {
						 fail(row.source + ": " + needLabel + " " + std::string(value) + " is also set in " + found->second);
					 }
				};
				for (const CreatureNeedRow* row : needRows) {
					if (row->label) {
						set(*row, "label");
						label = row->label;
					}
					if (row->seek) {
						set(*row, "seek");
						seek = row->seek;
					}
					if (row->urgent) {
						set(*row, "urgent");
						urgent = row->urgent;
					}
					if (row->critical) {
						set(*row, "critical");
						critical = row->critical;
					}
					if (row->move) {
						set(*row, "how the level moves (<drains> or <setBy>)");
						move = row->move;
					}
					if (row->satisfiedBy) {
						if (broughtBy != brought.end()) {
							fail(
								row->source + ": " + needLabel +
								" has <satisfiedBy> rows; what meets an included need comes from its behavior"
							);
							continue;
						}
						set(*row, "<satisfiedBy>");
						satisfiedBy = *row->satisfiedBy;
					}
				}
				const std::string where = rows.primarySource + ": <creature> need " + inQuotes(id);
				if (!label || !seek || !urgent || !critical || !move) {
					if (base == nullptr && broughtBy == brought.end() && !includeFailed && settledBy == nullptr) {
						std::vector<std::string_view> missing;
						if (!label) {
							missing.emplace_back("label");
						}
						if (!seek) {
							missing.emplace_back("seek");
						}
						if (!urgent) {
							missing.emplace_back("urgent");
						}
						if (!critical) {
							missing.emplace_back("critical");
						}
						if (!move) {
							missing.emplace_back("<drains> or <setBy>");
						}
						fail(
							where + " is no included behavior's need, so the creature's rows give all of it, and they give no " +
							joined(missing)
						);
					}
					continue;
				}
				if (*urgent >= *seek) {
					fail(where + " urgent must be below seek (" + numberText(*seek) + "), got " + inQuotes(numberText(*urgent)));
				}
				if (*critical >= *urgent) {
					fail(where + " critical must be below urgent (" + numberText(*urgent) + "), got " + inQuotes(numberText(*critical)));
				}
				need.label = *label;
				need.seek = *seek;
				need.urgent = *urgent;
				need.critical = *critical;
				need.move = *move;
				need.satisfiedBy = std::move(satisfiedBy);
				if (broughtBy == brought.end()) {
					resolveSatisfiedBy(need.satisfiedBy, rows.name, rows.primarySource + ": <creature> " + needLabel);
					definedNeeds.insert(need.id.index);
				}
				creature.needs.push_back(std::move(need));
			}

			// Mood costs: one for every need of a creature with mood, none for one without.
			std::map<std::string, std::string, std::less<>> costSources;
			for (const CreatureCostRow& cost : rows.costs) {
				const std::string where = cost.source + ": <moodCost need=" + inQuotes(cost.need) + ">";
				if (!rows.mood) {
					fail(where + " gives a mood cost, and the creature has no mood (mood=\"false\")");
					continue;
				}
				const auto [found, added] = costSources.try_emplace(cost.need, cost.source);
				if (!added) {
					fail(where + " is also given in " + found->second);
					continue;
				}
				const auto need = std::ranges::find_if(creature.needs, [&](const CreatureNeed& candidate) {
					return library.needIds[candidate.id.index] == cost.need;
				});
				if (need == creature.needs.end()) {
					if (!includeFailed && !unsettled.contains(cost.need)) {
						fail(where + " names a need the creature does not have");
					}
					continue;
				}
				need->moodCost = cost.cost;
			}
			if (rows.mood) {
				for (const CreatureNeed& need : creature.needs) {
					if (!need.moodCost) {
						fail(
							rows.primarySource + ": <creature> need " + inQuotes(library.needIds[need.id.index]) +
							" has no <moodCost>; a creature with mood gives every need a cost"
						);
					}
				}
			}
			if (result.errors.size() == errorsBefore) {
				library.creatures.push_back(std::move(creature));
			}
		}
	}

} // namespace assets::entry_loading

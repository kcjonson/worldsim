// FileReader's <query> entries: their <from> rows, fact rows, and scale rows.

#include "assets/EntryLoading.h"

#include <algorithm>

namespace assets::entry_loading {

	std::optional<FromRow> FileReader::fromRow(pugi::xml_node node, const std::string& label) {
		static constexpr std::array<std::string_view, 8> kSources = {
			"capability", "kind", "item", "yields", "pile", "box", "place", "goal"
		};
		std::vector<std::string_view> given;
		for (const std::string_view sourceName : kSources) {
			if (!node.attribute(std::string(sourceName).c_str()).empty()) {
				given.push_back(sourceName);
			}
		}
		if (given.size() != 1) {
			failAt(
				label + (given.empty() ? " names no source" : " names more than one source (" + joined(given) + ")") +
				"; give exactly one of " + joined(kSources)
			);
			return std::nullopt;
		}
		const std::string_view sourceName = given.front();
		FromRow				   row;
		if (!node.attribute("fallback").empty()) {
			row.fallback = boolean(node, "fallback", label).value_or(false);
		}
		if (sourceName == "capability") {
			attributesOnly(node, {"capability", "fallback"}, label);
			row.source = CandidateSource::Capability;
			const std::optional<CapabilityType> parsed = capability(node, "capability", label, true);
			if (!parsed) {
				return std::nullopt;
			}
			row.capability = *parsed;
		} else if (sourceName == "kind") {
			attributesOnly(node, {"kind", "fallback"}, label);
			row.source = CandidateSource::Kind;
			row.kind = required(node, "kind", label);
		} else if (sourceName == "item" || sourceName == "yields" || sourceName == "pile" || sourceName == "box") {
			attributesOnly(node, {"item", "yields", "pile", "box", "fallback"}, label);
			row.source = sourceName == "item"	  ? CandidateSource::Item
						 : sourceName == "yields" ? CandidateSource::Yields
						 : sourceName == "pile"	  ? CandidateSource::Pile
												  : CandidateSource::Box;
			const std::optional<ItemArgument> item =
				itemArgument(node, std::string(sourceName).c_str(), label, row.source != CandidateSource::Item);
			if (!item) {
				return std::nullopt;
			}
			row.item = *item;
		} else if (sourceName == "place") {
			static constexpr std::array<std::string_view, 3> kPlaces = {"here", "around", "grid"};
			const std::optional<std::size_t>				 place = choice(node, "place", label, kPlaces, true);
			if (!place) {
				return std::nullopt;
			}
			if (*place == 0) {
				attributesOnly(node, {"place", "fallback"}, label);
				row.source = CandidateSource::PlaceHere;
			} else if (*place == 1) {
				attributesOnly(node, {"place", "radius", "count", "minRadius", "fallback"}, label);
				row.source = CandidateSource::PlaceAround;
				row.radius = boundedNumber(node, "radius", label, 0.0F, std::numeric_limits<float>::max(), true);
				row.count = reader.labeledInteger(label, node, "count", 1, std::numeric_limits<int>::max());
				if (!node.attribute("minRadius").empty()) {
					row.minRadius = boundedNumber(node, "minRadius", label, 0.0F, std::numeric_limits<float>::max());
					if (!std::isnan(row.minRadius) && !std::isnan(row.radius) && row.minRadius >= row.radius) {
						refuse(label, node, "minRadius", "below radius (" + numberText(row.radius) + ")");
					}
				}
			} else {
				attributesOnly(node, {"place", "spacing", "radius", "fallback"}, label);
				row.source = CandidateSource::PlaceGrid;
				row.spacing = boundedNumber(node, "spacing", label, 0.0F, std::numeric_limits<float>::max(), true);
				row.radius = boundedNumber(node, "radius", label, 0.0F, std::numeric_limits<float>::max(), true);
			}
		} else {
			attributesOnly(node, {"goal", "fallback"}, label);
			static constexpr std::array<std::string_view, 2> kGoalSources = {"destination", "spots"};
			const std::optional<std::size_t>				 goal = choice(node, "goal", label, kGoalSources, true);
			if (!goal) {
				return std::nullopt;
			}
			if (kind != FileKind::Work) {
				fail(
					label, "goal " + inQuotes(kGoalSources[*goal]) + " reads the goal a work option serves, which only a query in work/ has"
				);
				return std::nullopt;
			}
			row.source = *goal == 0 ? CandidateSource::GoalDestination : CandidateSource::GoalSpots;
		}
		childrenOnly(node, {}, label);
		return row;
	}

	std::optional<QueryEntry> FileReader::query(pugi::xml_node node) {
		const std::string shortId = declaredId(node, "query");
		const std::string label = entryLabel("query", shortId);
		attributesOnly(node, {"id", "pick", "script"}, label);
		reserveScript(node, label);
		QueryEntry										 entry{.id = fullId(shortId), .file = source};
		static constexpr std::array<std::string_view, 2> kPicks = {"best", "weighted"};
		if (const std::optional<std::size_t> pick = choice(node, "pick", label, kPicks, false)) {
			entry.pick = static_cast<QueryPick>(*pick);
		}
		const std::size_t problemsBefore = problems.size() + reader.problemCount();
		std::size_t		  index = 0;
		for (const pugi::xml_node row : node.children()) {
			if (row.type() != pugi::node_element) {
				continue;
			}
			const std::string_view element = row.name();
			const std::string	   where = label + " " + rowLabel(index++, element);
			if (element == "from") {
				if (std::optional<FromRow> from = fromRow(row, where)) {
					from->row = static_cast<uint16_t>(index - 1);
					entry.from.push_back(std::move(*from));
				}
			} else if (element == "weigh" || element == "exclude" || element == "prefer") {
				FactRow factRow;
				if (element == "weigh") {
					attributesOnly(row, {"fact", "by", "when"}, where);
					factRow.kind = FactRowKind::Weigh;
					factRow.by = number(row, "by", where);
				} else {
					if (element == "exclude") {
						attributesOnly(row, {"fact", "above", "below", "when"}, where);
					} else {
						attributesOnly(row, {"fact", "above", "below"}, where);
					}
					factRow.kind = element == "exclude" ? FactRowKind::Exclude : FactRowKind::Prefer;
					if (!row.attribute("above").empty() && !row.attribute("below").empty()) {
						failAt(where + " gives both above and below; a row draws one line");
					}
					factRow.above = optionalNumber(row, "above", where);
					factRow.below = optionalNumber(row, "below", where);
				}
				if (!row.attribute("when").empty()) {
					factRow.when = required(row, "when", where);
				}
				childrenOnly(row, {}, where);
				if (const std::optional<FactUse> used = fact(row, "fact", where)) {
					factRow.fact = *used;
					entry.rows.push_back(std::move(factRow));
				}
			} else if (element == "scale") {
				attributesOnly(row, {"trait", "fact", "by"}, where);
				childrenOnly(row, {}, where);
				ScaleRow scale{
					.trait = required(row, "trait", where), .by = number(row, "by", where), .row = static_cast<uint16_t>(index - 1)
				};
				if (const std::optional<FactUse> used = fact(row, "fact", where)) {
					scale.fact = *used;
					entry.scales.push_back(std::move(scale));
				}
			} else {
				failAt(label + ": unknown element <" + std::string(element) + ">");
			}
		}
		if (problems.size() + reader.problemCount() == problemsBefore) {
			const uint16_t distance = *factIndex("distance");
			const auto	   bounded = [&](FactRowKind rowKind) {
				return std::ranges::any_of(entry.rows, [&](const FactRow& row) {
					if (row.kind != rowKind || row.fact.fact != distance || !row.when.empty()) {
						return false;
					}
					return rowKind == FactRowKind::Weigh ? row.by < 0.0F : row.above.has_value();
				});
			};
			if (entry.from.empty()) {
				failAt(label + " has no <from> row, so it has no candidates");
			}
			if (entry.pick == QueryPick::Weighted && !bounded(FactRowKind::Exclude)) {
				failAt(label + " picks weighted but excludes no distance above a bound, so its draw has no bound");
			} else if (!bounded(FactRowKind::Weigh) && !bounded(FactRowKind::Exclude)) {
				failAt(label + " neither weighs distance by a negative number nor excludes it above a bound, so nothing ends its search");
			}
		}
		if (shortId.empty()) {
			return std::nullopt;
		}
		return entry;
	}

} // namespace assets::entry_loading

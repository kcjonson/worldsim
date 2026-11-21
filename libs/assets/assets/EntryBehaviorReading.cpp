// FileReader's behavior files: the <behavior> root, its <need>, <idle>, and <lowMood> entries.

#include "assets/EntryLoading.h"

namespace assets::entry_loading {

	std::optional<NeedMove> FileReader::needMove(pugi::xml_node node, const std::string& label, bool isRequired) {
		const pugi::xml_node drains = node.child("drains");
		const pugi::xml_node setBy = node.child("setBy");
		const std::size_t given = static_cast<std::size_t>(std::distance(node.children("drains").begin(), node.children("drains").end())) +
								  static_cast<std::size_t>(std::distance(node.children("setBy").begin(), node.children("setBy").end()));
		if (given > 1) {
			failAt(label + " has more than one of <drains> and <setBy>; a level moves in exactly one way");
			return std::nullopt;
		}
		if (given == 0) {
			if (isRequired) {
				failAt(label + " has neither <drains> nor <setBy>; a level moves in exactly one way");
			}
			return std::nullopt;
		}
		NeedMove move;
		if (drains) {
			const std::string drainsLabel = label + " <drains>";
			attributesOnly(drains, {"perGameMinute"}, drainsLabel);
			childrenOnly(drains, {"rate"}, drainsLabel);
			move.kind = NeedMove::Kind::Drains;
			move.perGameMinute = boundedNumber(drains, "perGameMinute", drainsLabel, 0.0F, std::numeric_limits<float>::max());
			std::size_t row = 0;
			for (const pugi::xml_node rate : drains.children("rate")) {
				const std::string rateLabel = drainsLabel + " " + rowLabel(row++, "rate");
				attributesOnly(rate, {"when", "multiply"}, rateLabel);
				move.rates.push_back(
					RateRow{
						.when = required(rate, "when", rateLabel),
						.multiply = boundedNumber(rate, "multiply", rateLabel, 0.0F, std::numeric_limits<float>::max()),
					}
				);
			}
			return move;
		}
		const std::string setByLabel = label + " <setBy>";
		attributesOnly(setBy, {"provider"}, setByLabel);
		childrenOnly(setBy, {}, setByLabel);
		move.kind = NeedMove::Kind::SetBy;
		if (const std::optional<std::size_t> provider = choice(setBy, "provider", setByLabel, kNeedProviders, true)) {
			move.provider = std::string(kNeedProviders[*provider]);
		}
		return move;
	}

	std::vector<SatisfiedBy> FileReader::satisfiedByRows(pugi::xml_node node, const std::string& label) {
		std::vector<SatisfiedBy> rows;
		std::size_t				 index = 0;
		for (const pugi::xml_node row : node.children("satisfiedBy")) {
			const std::string rowName = label + " " + rowLabel(index++, "satisfiedBy");
			attributesOnly(row, {"chain", "work", "when"}, rowName);
			childrenOnly(row, {}, rowName);
			SatisfiedBy satisfied;
			const bool	hasChain = !row.attribute("chain").empty();
			const bool	hasWork = !row.attribute("work").empty();
			if (hasChain == hasWork) {
				failAt(
					rowName + " names " + (hasChain ? "both a chain and a work entry" : "neither a chain nor a work entry") +
					"; give exactly one"
				);
			} else if (hasChain) {
				satisfied.chain = reference<ChainEntry>(row, "chain", rowName);
			} else {
				satisfied.work = reference<WorkEntry>(row, "work", rowName);
			}
			if (!row.attribute("when").empty()) {
				satisfied.when = required(row, "when", rowName);
			}
			rows.push_back(std::move(satisfied));
		}
		return rows;
	}

	void FileReader::levelOrder(pugi::xml_node node, const std::string& label, float seek, float urgent, float critical) {
		if (!std::isnan(seek) && !std::isnan(urgent) && urgent >= seek) {
			refuse(label, node, "urgent", "below seek (" + numberText(seek) + ")");
		}
		if (!std::isnan(urgent) && !std::isnan(critical) && critical >= urgent) {
			refuse(label, node, "critical", "below urgent (" + numberText(urgent) + ")");
		}
	}

	std::optional<NeedEntry> FileReader::need(pugi::xml_node node) {
		const std::string label = "<need id=" + inQuotes(node.attribute("id").value()) + ">";
		attributesOnly(node, {"id", "label", "seek", "urgent", "critical", "script"}, label);
		childrenOnly(node, {"drains", "setBy", "satisfiedBy"}, label);
		reserveScript(node, label);
		NeedEntry					entry;
		const std::optional<NeedId> id = needName(node, "id", "<need>");
		entry.label = required(node, "label", label);
		entry.seek = boundedNumber(node, "seek", label, 0.0F, 100.0F);
		entry.urgent = boundedNumber(node, "urgent", label, 0.0F, 100.0F);
		entry.critical = boundedNumber(node, "critical", label, 0.0F, 100.0F);
		levelOrder(node, label, entry.seek, entry.urgent, entry.critical);
		const std::optional<NeedMove> move = needMove(node, label, true);
		entry.satisfiedBy = satisfiedByRows(node, label);
		entry.file = source;
		if (!id || !move) {
			return std::nullopt;
		}
		entry.id = *id;
		entry.move = *move;
		return entry;
	}

	std::optional<LowMoodEntry> FileReader::lowMood(pugi::xml_node node, const std::string& behaviorLabel) {
		const std::string label = behaviorLabel + " <lowMood>";
		attributesOnly(node, {"rampStart", "rampFull", "base"}, label);
		childrenOnly(node, {"target", "weight", "requires", "effect", "ends", "relief"}, label);
		LowMoodEntry entry;
		entry.rampStart = boundedNumber(node, "rampStart", label, 0.0F, 100.0F);
		entry.rampFull = boundedNumber(node, "rampFull", label, 0.0F, 100.0F);
		if (!std::isnan(entry.rampStart) && !std::isnan(entry.rampFull) && entry.rampFull >= entry.rampStart) {
			refuse(label, node, "rampFull", "below rampStart (" + numberText(entry.rampStart) + ")");
		}
		entry.base = boundedNumber(node, "base", label, 0.0F, std::numeric_limits<float>::max());
		const auto exactlyOne = [&](const char* element, bool isRequired) -> pugi::xml_node {
			const auto		  children = node.children(element);
			const std::size_t count = static_cast<std::size_t>(std::distance(children.begin(), children.end()));
			if (count > 1 || (count == 0 && isRequired)) {
				failAt(
					label + " has " + std::to_string(count) + " <" + element + "> rows; it takes " +
					(isRequired ? "exactly one" : "at most one")
				);
				return {};
			}
			return node.child(element);
		};
		if (const pugi::xml_node target = exactlyOne("target", false)) {
			attributesOnly(target, {"query"}, label + " <target>");
			entry.target = reference<QueryEntry>(target, "query", label + " <target>");
		}
		std::size_t row = 0;
		for (const pugi::xml_node weight : node.children("weight")) {
			const std::string where = label + " " + rowLabel(row++, "weight");
			attributesOnly(weight, {"when", "per", "multiply"}, where);
			WeightRow  weightRow;
			const bool hasWhen = !weight.attribute("when").empty();
			const bool hasPer = !weight.attribute("per").empty();
			if (hasWhen == hasPer) {
				failAt(where + " gives " + (hasWhen ? "both when and per" : "neither when nor per") + "; give exactly one");
			} else if (hasWhen) {
				weightRow.when = required(weight, "when", where);
			} else {
				weightRow.per = required(weight, "per", where);
			}
			weightRow.multiply = boundedNumber(weight, "multiply", where, 0.0F, std::numeric_limits<float>::max());
			entry.weights.push_back(std::move(weightRow));
		}
		row = 0;
		for (const pugi::xml_node requirement : node.children("requires")) {
			const std::string where = label + " " + rowLabel(row++, "requires");
			attributesOnly(requirement, {"when"}, where);
			entry.requirements.push_back(required(requirement, "when", where));
		}
		if (const pugi::xml_node effect = exactlyOne("effect", true)) {
			attributesOnly(effect, {"chain"}, label + " <effect>");
			entry.effect = reference<ChainEntry>(effect, "chain", label + " <effect>");
		}
		// One or more end conditions; the run stops at the first of them.
		if (!node.child("ends")) {
			failAt(label + " has no <ends> row; it takes an <ends> row");
		}
		row = 0;
		for (const pugi::xml_node ends : node.children("ends")) {
			const std::string								 where = label + " " + rowLabel(row++, "ends");
			static constexpr std::array<std::string_view, 1> kAfter = {"task"};
			attributesOnly(ends, {"after"}, where);
			if (choice(ends, "after", where, kAfter, true)) {
				if (entry.endsAfterTask) {
					fail(where, "after \"task\" is given twice");
				}
				entry.endsAfterTask = true;
			}
		}
		if (const pugi::xml_node relief = exactlyOne("relief", true)) {
			attributesOnly(relief, {"amount"}, label + " <relief>");
			if (const std::optional<std::size_t> amount = choice(relief, "amount", label + " <relief>", kReliefNames, true)) {
				entry.relief = static_cast<Relief>(*amount);
			}
		}
		return entry;
	}

	void FileReader::readBehavior(pugi::xml_node root, std::string_view fullBehaviorId) {
		const std::string_view stem = shortOf(fullBehaviorId);
		const std::string	   label = entryLabel("behavior", root.attribute("id").value());
		attributesOnly(root, {"id", "label", "script"}, label);
		childrenOnly(root, {"need", "chain", "query", "action", "idle", "lowMood"}, label);
		reserveScript(root, label);
		const std::string declared = required(root, "id", "<behavior>");
		if (!declared.empty() && declared != stem) {
			refuse(label, root, "id", "the file's name, " + inQuotes(stem));
		}
		BehaviorEntry behavior{.id = std::string(fullBehaviorId), .file = source, .label = root.attribute("label").value()};
		if (!root.attribute("label").empty() && behavior.label.empty()) {
			refuse(label, root, "label", "a label");
		}
		std::set<std::string> needIds;
		std::size_t			  idleRow = 0;
		for (const pugi::xml_node child : root.children()) {
			if (child.type() != pugi::node_element) {
				continue;
			}
			const std::string_view element = child.name();
			if (element == "need") {
				std::optional<NeedEntry> need = this->need(child);
				if (need) {
					const std::string id = child.attribute("id").value();
					if (!needIds.insert(id).second) {
						failAt(label + " <need id=" + inQuotes(id) + "> is given twice in one behavior");
					}
					need->behavior = EntryHandle<BehaviorEntry>{0};
					behavior.needs.push_back(EntryHandle<NeedEntry>{static_cast<uint32_t>(entries.needs.size())});
					entries.needs.push_back(std::move(*need));
				}
			} else if (element == "chain") {
				if (std::optional<ChainEntry> parsed = chain(child)) {
					entries.chains.push_back(std::move(*parsed));
				}
			} else if (element == "query") {
				if (std::optional<QueryEntry> parsed = query(child)) {
					entries.queries.push_back(std::move(*parsed));
				}
			} else if (element == "action") {
				if (std::optional<ActionEntry> parsed = action(child)) {
					entries.actions.push_back(std::move(*parsed));
				}
			} else if (element == "idle") {
				const std::string where = label + " " + rowLabel(idleRow++, "idle");
				attributesOnly(child, {"chain", "when"}, where);
				childrenOnly(child, {}, where);
				IdleRow row{.chain = reference<ChainEntry>(child, "chain", where)};
				if (!child.attribute("when").empty()) {
					row.when = required(child, "when", where);
				}
				behavior.idle.push_back(std::move(row));
			} else if (element == "lowMood") {
				if (behavior.lowMood) {
					failAt(label + " has more than one <lowMood>; a low-mood behavior holds one");
					continue;
				}
				behavior.lowMood = lowMood(child, label);
			}
		}
		entries.behaviors.push_back(std::move(behavior));
	}

} // namespace assets::entry_loading

// FileReader's shared folders other than actions/: the <colonyWork>, <orders>, and <facts> files.

#include "assets/EntryLoading.h"

namespace assets::entry_loading {

	std::optional<CategoryEntry> FileReader::category(pugi::xml_node node) {
		const std::string shortId = declaredId(node, "category");
		const std::string label = entryLabel("category", shortId);
		attributesOnly(node, {"id", "label", "order", "canDisable", "script"}, label);
		childrenOnly(node, {}, label);
		reserveScript(node, label);
		CategoryEntry entry{.id = shortId, .file = source, .label = required(node, "label", label)};
		entry.order = reader.labeledInteger(label, node, "order", 0, std::numeric_limits<int>::max());
		entry.canDisable = boolean(node, "canDisable", label).value_or(true);
		if (shortId.empty()) {
			return std::nullopt;
		}
		return entry;
	}

	std::optional<WorkEntry> FileReader::workEntry(pugi::xml_node node) {
		const std::string shortId = declaredId(node, "work");
		const std::string label = entryLabel("work", shortId);
		attributesOnly(node, {"id", "goal", "need", "category", "rung", "gathers", "source", "script"}, label);
		childrenOnly(node, {"source", "chain"}, label);
		reserveScript(node, label);
		WorkEntry entry{.id = shortId, .file = source};
		if (const std::optional<std::size_t> goal = choice(node, "goal", label, kWorkGoalNames, true)) {
			entry.goal = static_cast<WorkGoal>(*goal);
		}
		if (entry.goal == WorkGoal::Care) {
			entry.need = needName(node, "need", label);
		} else if (!node.attribute("need").empty()) {
			refuse(label, node, "need", "left out; it names the need of a care goal");
		}
		entry.category = reference<CategoryEntry>(node, "category", label);
		const std::string rung = required(node, "rung", label);
		if (rung == kReservedRung) {
			refuse(label, node, "rung", "a rung other than survival, which is reserved");
		} else if (const std::optional<std::size_t> position = indexIn(loader.rungs(), rung); position) {
			entry.rung = static_cast<uint8_t>(*position);
		} else if (!rung.empty()) {
			refuse(label, node, "rung", "a rung of the ladder (" + joined(loader.rungs()) + ")");
		}
		entry.gathers = boolean(node, "gathers", label).value_or(false);
		const auto sources = node.children("source");
		const auto sourceCount = std::distance(sources.begin(), sources.end());
		if (!node.attribute("source").empty()) {
			static constexpr std::array<std::string_view, 1> kCarried = {"carried"};
			entry.sourceCarried = choice(node, "source", label, kCarried, true).has_value();
			if (sourceCount > 0) {
				failAt(label + " gives both source=\"carried\" and a <source> row; give one");
			}
		} else if (sourceCount > 1) {
			failAt(label + " has more than one <source> row");
		} else if (sourceCount == 1) {
			const pugi::xml_node row = node.child("source");
			attributesOnly(row, {"query"}, label + " <source>");
			entry.source = reference<QueryEntry>(row, "query", label + " <source>");
		}
		const auto chains = node.children("chain");
		if (std::distance(chains.begin(), chains.end()) != 1) {
			failAt(label + " has " + std::to_string(std::distance(chains.begin(), chains.end())) + " <chain> rows; it names exactly one");
		} else {
			attributesOnly(node.child("chain"), {"id"}, label + " <chain>");
			entry.chain = reference<ChainEntry>(node.child("chain"), "id", label + " <chain>");
		}
		if (shortId.empty()) {
			return std::nullopt;
		}
		return entry;
	}

	void FileReader::readWork(pugi::xml_node root) {
		attributesOnly(root, {}, "<colonyWork>");
		childrenOnly(root, {"category", "work", "chain", "query", "action"}, "<colonyWork>");
		for (const pugi::xml_node child : root.children()) {
			const std::string_view element = child.name();
			if (element == "category") {
				if (std::optional<CategoryEntry> parsed = category(child)) {
					entries.categories.push_back(std::move(*parsed));
				}
			} else if (element == "work") {
				if (std::optional<WorkEntry> parsed = workEntry(child)) {
					entries.work.push_back(std::move(*parsed));
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
			}
		}
	}

	std::optional<OrderEntry> FileReader::order(pugi::xml_node node) {
		const std::string shortId = declaredId(node, "order");
		const std::string label = entryLabel("order", shortId);
		attributesOnly(node, {"id", "label", "on", "chain", "script"}, label);
		childrenOnly(node, {}, label);
		reserveScript(node, label);
		OrderEntry		  entry{.id = shortId, .file = source, .label = required(node, "label", label)};
		const std::string on = required(node, "on", label);
		if (on == "ground") {
			entry.on = OrderTarget{.kind = OrderTarget::Kind::Ground};
		} else if (const std::optional<CapabilityType> named = capabilityFromName(on)) {
			entry.on = OrderTarget{.kind = OrderTarget::Kind::Capability, .capability = *named};
		} else if (!on.empty()) {
			refuse(label, node, "on", "a capability (" + capabilityNames() + ") or ground");
		}
		entry.chain = reference<ChainEntry>(node, "chain", label);
		if (shortId.empty()) {
			return std::nullopt;
		}
		return entry;
	}

	void FileReader::readOrders(pugi::xml_node root) {
		attributesOnly(root, {}, "<orders>");
		childrenOnly(root, {"order", "chain"}, "<orders>");
		for (const pugi::xml_node child : root.children()) {
			if (std::string_view(child.name()) == "order") {
				if (std::optional<OrderEntry> parsed = order(child)) {
					entries.orders.push_back(std::move(*parsed));
				}
			} else if (std::string_view(child.name()) == "chain") {
				if (std::optional<ChainEntry> parsed = chain(child)) {
					entries.chains.push_back(std::move(*parsed));
				}
			}
		}
	}

	void FileReader::readFacts(pugi::xml_node root) {
		attributesOnly(root, {}, "<facts>");
		childrenOnly(root, {"fact"}, "<facts>");
		std::set<uint16_t> given;
		for (const pugi::xml_node node : root.children("fact")) {
			const std::string id = node.attribute("id").value();
			const std::string label = "<fact id=" + inQuotes(id) + ">";
			childrenOnly(node, {}, label);
			if (node.attribute("id").empty()) {
				refuse("<fact>", node, "id", "");
				continue;
			}
			const std::optional<uint16_t> index = factIndex(id);
			if (!index) {
				refuse("<fact>", node, "id", "a fact the sim provides");
				continue;
			}
			const FactDeclaration& declaration = kFacts[*index];
			if (declaration.tuning.empty()) {
				failAt(label + " reads no tuning value, so it has no row here");
				continue;
			}
			const std::string tuning(declaration.tuning);
			attributesOnly(node, {"id", declaration.tuning}, label);
			if (!given.insert(*index).second) {
				failAt(label + " is given twice in one file");
				continue;
			}
			const float value = tuning == "ground"
									? boundedNumber(node, tuning.c_str(), label, 0.0F, 1.0F)
									: boundedNumber(node, tuning.c_str(), label, 0.0F, std::numeric_limits<float>::max(), true);
			entries.factValues.emplace_back(*index, value);
		}
	}

} // namespace assets::entry_loading

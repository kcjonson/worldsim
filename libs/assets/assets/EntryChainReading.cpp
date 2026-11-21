// FileReader's <chain> entries and their steps.

#include "assets/EntryLoading.h"

namespace assets::entry_loading {

	std::optional<ChainEntry> FileReader::chain(pugi::xml_node node) {
		const std::string shortId = declaredId(node, "chain");
		const std::string label = entryLabel("chain", shortId);
		attributesOnly(node, {"id", "script"}, label);
		reserveScript(node, label);
		ChainEntry	entry{.id = fullId(shortId), .file = source};
		std::size_t index = 0;
		bool		unknownStep = false;
		for (const pugi::xml_node stepNode : node.children()) {
			if (stepNode.type() != pugi::node_element) {
				continue;
			}
			const std::string_view element = stepNode.name();
			const std::string	   where = label + " " + stepLabel(index++, element);
			ChainStep			   step;
			if (element == "go") {
				attributesOnly(stepNode, {"query", "to"}, where);
				GoStep	   go;
				const bool hasQuery = !stepNode.attribute("query").empty();
				const bool hasTo = !stepNode.attribute("to").empty();
				if (hasQuery == hasTo) {
					failAt(
						where + " names " + (hasQuery ? "both a query and a target" : "neither a query nor a target") +
						"; give exactly one of query and to"
					);
				} else if (hasQuery) {
					go.query = reference<QueryEntry>(stepNode, "query", where);
				} else if (const std::optional<std::size_t> target = choice(stepNode, "to", where, kStepTargetNames, true)) {
					go.to = static_cast<StepTarget>(*target);
				}
				step.step = std::move(go);
			} else if (element == "do") {
				attributesOnly(stepNode, {"action", "on", "claims", "amount"}, where);
				DoStep doStep{.action = reference<ActionEntry>(stepNode, "action", where)};
				if (const std::optional<std::size_t> on = choice(stepNode, "on", where, kStepTargetNames, false)) {
					doStep.on = static_cast<StepTarget>(*on);
				}
				if (const std::optional<std::size_t> amount = choice(stepNode, "amount", where, kStepAmountNames, false)) {
					doStep.amount = static_cast<StepAmount>(*amount);
				}
				if (!stepNode.attribute("claims").empty()) {
					const std::vector<std::string> claims = splitNames(stepNode.attribute("claims").value());
					if (claims.empty()) {
						refuse(where, stepNode, "claims", "claim kinds (" + joined(kClaimKindNames) + ")");
					}
					for (const std::string& claim : claims) {
						const std::optional<std::size_t> kindIndex = indexIn(kClaimKindNames, claim);
						if (!kindIndex) {
							fail(where, "claims " + inQuotes(claim) + " is not a claim kind (" + joined(kClaimKindNames) + ")");
							continue;
						}
						const auto bit = static_cast<uint8_t>(1U << *kindIndex);
						if ((doStep.claims & bit) != 0) {
							fail(where, "claims names " + inQuotes(claim) + " twice");
						}
						doStep.claims |= bit;
					}
				}
				step.step = std::move(doStep);
			} else if (element == "have") {
				attributesOnly(stepNode, {"item", "capability", "fetch"}, where);
				HaveStep   have;
				const bool hasItem = !stepNode.attribute("item").empty();
				const bool hasCapability = !stepNode.attribute("capability").empty();
				if (hasItem == hasCapability) {
					failAt(
						where + " names " + (hasItem ? "both an item and a capability" : "neither an item nor a capability") +
						"; give exactly one"
					);
				} else if (hasItem) {
					have.item = itemArgument(stepNode, "item", where, false);
				} else {
					have.capability = capability(stepNode, "capability", where, true);
				}
				have.fetch = reference<ChainEntry>(stepNode, "fetch", where);
				step.step = std::move(have);
			} else if (element == "think") {
				attributesOnly(stepNode, {"thought", "to"}, where);
				ThinkStep think{.thought = required(stepNode, "thought", where)};
				if (const std::optional<std::size_t> to = choice(stepNode, "to", where, kRecipientNames, true)) {
					think.to = static_cast<ThoughtRecipient>(*to);
				}
				step.step = std::move(think);
			} else if (element == "need") {
				attributesOnly(stepNode, {"id", "change"}, where);
				NeedStep needStep;
				if (const std::optional<NeedId> id = needName(stepNode, "id", where)) {
					needStep.need = *id;
				}
				needStep.change = number(stepNode, "change", where);
				step.step = needStep;
			} else if (element == "drop" || element == "leave_colony") {
				attributesOnly(stepNode, {}, where);
				step.step = element == "drop" ? decltype(step.step)(DropStep{}) : decltype(step.step)(LeaveColonyStep{});
			} else if (element == "run") {
				attributesOnly(stepNode, {"chain"}, where);
				step.step = RunStep{.chain = reference<ChainEntry>(stepNode, "chain", where)};
			} else {
				failAt(label + ": unknown element <" + std::string(element) + ">");
				unknownStep = true;
				continue;
			}
			childrenOnly(stepNode, {"ends"}, where);
			for (const pugi::xml_node ends : stepNode.children("ends")) {
				const std::string endsLabel = where + " <ends>";
				attributesOnly(ends, {"on"}, endsLabel);
				if (const std::optional<std::size_t> event = choice(ends, "on", endsLabel, kEndEventNames, true)) {
					const auto bit = static_cast<uint8_t>(1U << *event);
					if ((step.ends & bit) != 0) {
						fail(endsLabel, "on " + inQuotes(kEndEventNames[*event]) + " is given twice");
					}
					step.ends |= bit;
				}
			}
			entry.steps.push_back(std::move(step));
		}
		if (entry.steps.empty() && !unknownStep) {
			failAt(label + " has no step");
		}
		if (shortId.empty()) {
			return std::nullopt;
		}
		return entry;
	}

} // namespace assets::entry_loading

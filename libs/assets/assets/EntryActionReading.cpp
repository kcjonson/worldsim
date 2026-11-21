// FileReader's <action> entries and their effects, <actionGroup> entries, and the <actions> files.

#include "assets/EntryLoading.h"

#include <algorithm>

namespace assets::entry_loading {

	std::optional<EffectRow> FileReader::effect(pugi::xml_node node, const std::string& label) {
		const std::string_view element = node.name();
		childrenOnly(node, {}, label);
		if (element == "restore") {
			attributesOnly(node, {"need", "amount", "from", "scale", "scaleBy", "seconds", "upTo", "who"}, label);
			RestoreEffect restore;
			if (const std::optional<NeedId> need = needName(node, "need", label)) {
				restore.need = *need;
			}
			static constexpr std::array<std::string_view, 2> kWho = {"self", "target"};
			if (const std::optional<std::size_t> who = choice(node, "who", label, kWho, false)) {
				restore.onTarget = *who == 1;
			}
			const bool hasAmount = !node.attribute("amount").empty();
			const bool hasFrom = !node.attribute("from").empty();
			if (hasAmount == hasFrom) {
				failAt(label + " gives " + (hasAmount ? "both amount and from" : "neither amount nor from") + "; give exactly one");
			} else if (hasAmount) {
				restore.amount = number(node, "amount", label);
				if (!node.attribute("scale").empty()) {
					refuse(label, node, "scale", "left out; it scales a from property");
				}
			} else {
				static constexpr std::array<std::string_view, 1> kProperties = {"nutrition"};
				if (choice(node, "from", label, kProperties, true)) {
					restore.from = ConsumedProperty::Nutrition;
				}
				restore.scale = number(node, "scale", label);
			}
			if (!node.attribute("scaleBy").empty()) {
				restore.scaleBy = fact(node, "scaleBy", label);
			}
			const bool hasSeconds = !node.attribute("seconds").empty();
			const bool hasUpTo = !node.attribute("upTo").empty();
			if (hasSeconds != hasUpTo) {
				failAt(
					label + " gives " + (hasSeconds ? "seconds without upTo" : "upTo without seconds") +
					"; an open-ended restore gives both"
				);
			} else if (hasSeconds) {
				if (hasFrom) {
					failAt(label + " gives from with seconds and upTo; an open-ended restore gives an amount per that many seconds");
				}
				restore.seconds = boundedNumber(node, "seconds", label, 0.0F, std::numeric_limits<float>::max(), true);
				restore.upTo = boundedNumber(node, "upTo", label, 0.0F, 100.0F);
			}
			return restore;
		}
		if (element == "consume") {
			attributesOnly(node, {"capability", "count", "seconds", "upTo", "need"}, label);
			ConsumeEffect consume;
			if (const std::optional<CapabilityType> parsed = capability(node, "capability", label, true)) {
				consume.capability = *parsed;
			}
			consume.count = reader.labeledInteger(label, node, "count", 1, std::numeric_limits<int>::max());
			const int given = static_cast<int>(!node.attribute("seconds").empty()) + static_cast<int>(!node.attribute("upTo").empty()) +
							  static_cast<int>(!node.attribute("need").empty());
			if (given != 0 && given != 3) {
				failAt(label + " gives some of seconds, upTo, and need; an open-ended consume gives all three");
			} else if (given == 3) {
				consume.seconds = boundedNumber(node, "seconds", label, 0.0F, std::numeric_limits<float>::max(), true);
				consume.upTo = boundedNumber(node, "upTo", label, 0.0F, 100.0F);
				consume.need = needName(node, "need", label);
			}
			return consume;
		}
		if (element == "take") {
			attributesOnly(node, {"clears"}, label);
			return TakeEffect{.clears = boolean(node, "clears", label).value_or(false)};
		}
		if (element == "work") {
			attributesOnly(node, {"rate", "direction"}, label);
			WorkEffect										 work{.rate = required(node, "rate", label)};
			static constexpr std::array<std::string_view, 2> kDirections = {"up", "down"};
			if (const std::optional<std::size_t> direction = choice(node, "direction", label, kDirections, true)) {
				work.direction = static_cast<WorkDirection>(*direction);
			}
			return work;
		}
		if (element == "spawn") {
			attributesOnly(node, {"def", "into"}, label);
			SpawnEffect spawn{.def = node.attribute("def").value()};
			if (const std::optional<uint32_t> id = definition(node, "def", label)) {
				spawn.defNameId = *id;
			}
			static constexpr std::array<std::string_view, 1> kInto = {"hands"};
			spawn.intoHands = choice(node, "into", label, kInto, false).has_value();
			return spawn;
		}
		if (element == "damage") {
			attributesOnly(node, {"amount"}, label);
			return DamageEffect{.amount = boundedNumber(node, "amount", label, 0.0F, std::numeric_limits<float>::max(), true)};
		}
		attributesOnly(node, {}, label);
		if (element == "put") {
			return PutEffect{};
		}
		if (element == "craft") {
			return CraftEffect{};
		}
		return InstallEffect{};
	}

	std::optional<ActionEntry> FileReader::action(pugi::xml_node node) {
		const std::string shortId = declaredId(node, "action");
		const std::string label = entryLabel("action", shortId);
		attributesOnly(
			node, {"id", "seconds", "workFrom", "skill", "hands", "interruptable", "pays", "curve", "asleep", "on", "script"}, label
		);
		reserveScript(node, label);
		ActionEntry entry{.id = fullId(shortId), .file = source};
		std::size_t index = 0;
		bool		unknownEffect = false;
		for (const pugi::xml_node effectNode : node.children()) {
			if (effectNode.type() != pugi::node_element) {
				continue;
			}
			static constexpr std::array<std::string_view, 9> kEffects = {
				"restore", "consume", "take", "put", "work", "craft", "spawn", "install", "damage"
			};
			if (!indexIn(kEffects, effectNode.name())) {
				failAt(label + ": unknown element <" + std::string(effectNode.name()) + ">");
				unknownEffect = true;
				continue;
			}
			if (std::optional<EffectRow> row =
					effect(effectNode, label + " " + std::string("effect ") + std::to_string(++index) + " <" + effectNode.name() + ">")) {
				entry.effects.push_back(std::move(*row));
			}
		}
		if (index == 0 && !unknownEffect) {
			failAt(label + " has no effect");
		}

		const bool openEnded = std::ranges::any_of(entry.effects, [](const EffectRow& row) {
			if (std::holds_alternative<WorkEffect>(row)) {
				return true;
			}
			if (const auto* restore = std::get_if<RestoreEffect>(&row)) {
				return restore->upTo.has_value();
			}
			if (const auto* consume = std::get_if<ConsumeEffect>(&row)) {
				return consume->upTo.has_value();
			}
			return false;
		});
		const bool hasSeconds = !node.attribute("seconds").empty();
		const bool hasWorkFrom = !node.attribute("workFrom").empty();
		if (hasSeconds && hasWorkFrom) {
			failAt(label + " gives both seconds and workFrom; an action has one duration");
		} else if (openEnded && (hasSeconds || hasWorkFrom)) {
			refuse(
				label,
				node,
				hasSeconds ? "seconds" : "workFrom",
				"left out; an open-ended action (a <work> effect or an upTo bound) runs until its bound"
			);
		} else if (hasSeconds) {
			entry.duration = ActionDuration::Seconds;
			entry.seconds = boundedNumber(node, "seconds", label, 0.0F, std::numeric_limits<float>::max(), true);
		} else if (hasWorkFrom) {
			entry.duration = ActionDuration::FromDurability;
			static constexpr std::array<std::string_view, 1> kWorkFrom = {"durability"};
			choice(node, "workFrom", label, kWorkFrom, true);
			entry.skill = required(node, "skill", label);
		} else if (openEnded) {
			entry.duration = ActionDuration::OpenEnded;
		} else {
			failAt(label + " has no duration; give seconds, workFrom with skill, or an open-ended effect");
		}
		if (!hasWorkFrom && !node.attribute("skill").empty()) {
			refuse(label, node, "skill", "left out; it is the skill of a workFrom duration");
		}

		static constexpr std::array<std::string_view, 2> kHands = {"any", "free"};
		if (const std::optional<std::size_t> hands = choice(node, "hands", label, kHands, false)) {
			entry.hands = static_cast<Hands>(*hands);
		}
		if (node.attribute("interruptable").empty()) {
			refuse(label, node, "interruptable", "");
		} else {
			entry.interruptable = boolean(node, "interruptable", label).value_or(false);
		}
		static constexpr std::array<std::string_view, 2> kPays = {"end", "along"};
		if (const std::optional<std::size_t> pays = choice(node, "pays", label, kPays, false)) {
			entry.pays = static_cast<Pays>(*pays);
		}
		if (!node.attribute("curve").empty()) {
			if (entry.pays != Pays::Along) {
				refuse(label, node, "curve", "left out; it shapes an action that pays along");
			} else {
				entry.curve = boundedNumber(node, "curve", label, 0.0F, std::numeric_limits<float>::max(), true);
			}
		}
		entry.asleep = boolean(node, "asleep", label).value_or(false);
		if (!node.attribute("on").empty()) {
			const std::string on = node.attribute("on").value();
			if (on == "loose_item") {
				entry.on = ActionTarget{.kind = ActionTarget::Kind::LooseItem};
			} else if (on == "packaged") {
				entry.on = ActionTarget{.kind = ActionTarget::Kind::Packaged};
			} else if (const std::optional<CapabilityType> named = capabilityFromName(on)) {
				entry.on = ActionTarget{.kind = ActionTarget::Kind::Capability, .capability = *named};
			} else {
				refuse(label, node, "on", "a capability (" + capabilityNames() + "), loose_item, or packaged");
			}
		}
		if (shortId.empty()) {
			return std::nullopt;
		}
		return entry;
	}

	std::optional<ActionEntry> FileReader::actionGroup(pugi::xml_node node) {
		const std::string shortId = declaredId(node, "actionGroup");
		const std::string label = entryLabel("actionGroup", shortId);
		attributesOnly(node, {"id", "script"}, label);
		childrenOnly(node, {}, label);
		reserveScript(node, label);
		ActionEntry entry{.id = fullId(shortId), .file = source, .group = true};
		for (std::string& member : splitNames(node.text().as_string())) {
			entry.members.push_back(EntryReference<ActionEntry>{.written = std::move(member)});
		}
		if (entry.members.empty()) {
			failAt(label + " names no member action");
		}
		if (shortId.empty()) {
			return std::nullopt;
		}
		return entry;
	}

	void FileReader::readActions(pugi::xml_node root) {
		attributesOnly(root, {}, "<actions>");
		childrenOnly(root, {"action", "actionGroup"}, "<actions>");
		for (const pugi::xml_node child : root.children()) {
			std::optional<ActionEntry> parsed;
			if (std::string_view(child.name()) == "action") {
				parsed = action(child);
			} else if (std::string_view(child.name()) == "actionGroup") {
				parsed = actionGroup(child);
			}
			if (parsed) {
				entries.actions.push_back(std::move(*parsed));
			}
		}
	}

} // namespace assets::entry_loading

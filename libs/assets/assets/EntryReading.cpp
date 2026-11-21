// The entry loaders' message helpers, and FileReader's readers of one value: a definition, an item argument, a
// fact, a need id.

#include "assets/EntryLoading.h"

#include <algorithm>
#include <charconv>

namespace assets::entry_loading {

	// --- Names and messages ----------------------------------------------------------------------------------

	std::string inQuotes(std::string_view text) {
		return "\"" + std::string(text) + "\"";
	}

	std::string capabilityNames() {
		return joined(kCapabilityNames);
	}

	std::string numberText(float value) {
		std::array<char, 32> buffer{};
		const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
		return error == std::errc() ? std::string(buffer.data(), end) : std::string("?");
	}

	std::string_view folderOf(std::string_view id) {
		const std::size_t slash = id.rfind('/');
		return slash == std::string_view::npos ? std::string_view() : id.substr(0, slash);
	}

	std::string_view shortOf(std::string_view id) {
		const std::size_t slash = id.rfind('/');
		return slash == std::string_view::npos ? id : id.substr(slash + 1);
	}

	std::string entryLabel(std::string_view element, std::string_view id) {
		return "<" + std::string(element) + " id=" + inQuotes(shortOf(id)) + ">";
	}

	std::string stepLabel(std::size_t index, std::string_view element) {
		return "step " + std::to_string(index + 1) + " <" + std::string(element) + ">";
	}

	std::string rowLabel(std::size_t index, std::string_view element) {
		return "row " + std::to_string(index + 1) + " <" + std::string(element) + ">";
	}

	std::string_view targetName(const ActionTarget& target) {
		switch (target.kind) {
			case ActionTarget::Kind::LooseItem:
				return "loose_item";
			case ActionTarget::Kind::Packaged:
				return "packaged";
			case ActionTarget::Kind::Capability:
				break;
		}
		return capabilityName(target.capability);
	}

	bool sameTarget(const ActionTarget& left, const ActionTarget& right) {
		return left.kind == right.kind && (left.kind != ActionTarget::Kind::Capability || left.capability == right.capability);
	}

	std::optional<uint16_t> factIndex(std::string_view name) {
		const auto found = std::ranges::find(kFacts, name, &FactDeclaration::name);
		if (found == kFacts.end()) {
			return std::nullopt;
		}
		return static_cast<uint16_t>(found - kFacts.begin());
	}

	bool isIdPart(std::string_view name) {
		return !name.empty() && std::ranges::all_of(name, [](char letter) {
			return (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9') ||
				   letter == '_';
		});
	}

	// --- FileReader: values ----------------------------------------------------------------------------------

	std::optional<uint32_t> FileReader::definition(pugi::xml_node node, const char* attribute, const std::string& label) {
		const std::string name = required(node, attribute, label);
		if (name.empty()) {
			return std::nullopt;
		}
		const std::optional<uint32_t> id = loader.definitionId(name);
		if (!id) {
			fail(label, std::string(attribute) + " " + inQuotes(name) + " names no definition");
		}
		return id;
	}

	std::optional<ItemArgument>
	FileReader::itemArgument(pugi::xml_node node, const char* attribute, const std::string& label, bool capabilityAllowed) {
		const std::string written = required(node, attribute, label);
		if (written.empty()) {
			return std::nullopt;
		}
		ItemArgument argument{.written = written};
		if (written == "goal") {
			if (kind != FileKind::Work) {
				fail(
					label,
					std::string(attribute) + " " + inQuotes(written) + " is the item a work option is for, which only a query in work/ has"
				);
				return std::nullopt;
			}
			argument.kind = ItemArgument::Kind::Goal;
			return argument;
		}
		if (const std::optional<CapabilityType> named = capabilityFromName(written); named && capabilityAllowed) {
			argument.kind = ItemArgument::Kind::Capability;
			argument.capability = *named;
			return argument;
		}
		const std::optional<uint32_t> id = loader.definitionId(written);
		if (!id) {
			fail(
				label,
				std::string(attribute) + " " + inQuotes(written) +
					(capabilityAllowed ? " names no definition or capability" : " names no definition")
			);
			return std::nullopt;
		}
		argument.kind = ItemArgument::Kind::Definition;
		argument.defNameId = *id;
		return argument;
	}

	std::optional<FactUse> FileReader::fact(pugi::xml_node node, const char* attribute, const std::string& label) {
		const std::string written = required(node, attribute, label);
		if (written.empty()) {
			return std::nullopt;
		}
		std::string_view  name = written;
		std::string		  argument;
		const std::size_t open = written.find('(');
		if (open != std::string::npos) {
			if (written.back() != ')' || written.size() < open + 3) {
				fail(
					label,
					std::string(attribute) + " " + inQuotes(written) + " must be a fact, or a fact with one argument as contains(tag)"
				);
				return std::nullopt;
			}
			name = std::string_view(written).substr(0, open);
			argument = written.substr(open + 1, written.size() - open - 2);
		}
		const std::optional<uint16_t> index = factIndex(name);
		if (!index) {
			fail(label, std::string(attribute) + " " + inQuotes(written) + " names no fact the sim provides");
			return std::nullopt;
		}
		if (kFacts[*index].takesArgument && argument.empty()) {
			fail(label, std::string(attribute) + " " + inQuotes(written) + " takes one argument, as " + std::string(name) + "(tag)");
			return std::nullopt;
		}
		if (!kFacts[*index].takesArgument && !argument.empty()) {
			fail(label, std::string(attribute) + " " + inQuotes(written) + " takes no argument");
			return std::nullopt;
		}
		return FactUse{.fact = *index, .argument = argument};
	}

	std::optional<NeedId> FileReader::needName(pugi::xml_node node, const char* attribute, const std::string& label) {
		const std::string name = required(node, attribute, label);
		if (name.empty()) {
			return std::nullopt;
		}
		if (!isIdPart(name)) {
			refuse(label, node, attribute, std::string(kIdPartRule));
			return std::nullopt;
		}
		return loader.intern(name);
	}

} // namespace assets::entry_loading

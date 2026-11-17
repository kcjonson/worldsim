#pragma once

#include <pugixml.hpp>

#include <charconv>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace assets {

	/// Drops one leading '+' from config text; false when a sign still follows it ("+-5", "++5").
	[[nodiscard]] inline bool dropLeadingPlus(std::string_view& text) {
		if (text.starts_with('+')) {
			text.remove_prefix(1);
			return !text.starts_with('-') && !text.starts_with('+');
		}
		return true;
	}

	/// A config attribute's whole text as a finite number, a leading '+' allowed: nothing for empty text,
	/// text that is not entirely a number ("30abc" is refused rather than read as 30), or nan and inf.
	[[nodiscard]] inline std::optional<float> parseConfigNumber(std::string_view text) {
		if (!dropLeadingPlus(text)) {
			return std::nullopt;
		}
		float number = 0.0F;
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
		if (text.empty() || error != std::errc() || end != text.data() + text.size() || !std::isfinite(number)) {
			return std::nullopt;
		}
		return number;
	}

	/// A config attribute's whole text as a whole number: a leading '+' allowed, and nothing for empty text,
	/// trailing characters, or a value too large for int.
	[[nodiscard]] inline std::optional<int> parseConfigInteger(std::string_view text) {
		if (!dropLeadingPlus(text)) {
			return std::nullopt;
		}
		int number = 0;
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
		if (text.empty() || error != std::errc() || end != text.data() + text.size()) {
			return std::nullopt;
		}
		return number;
	}

	/// Collects what is wrong with the numbers and true-or-false values of one config file, for the config's validation to report.
	/// A bad value reads as NaN (0 for a whole number) and a problem names the file, the element by its
	/// instance (its name or defName attribute, or its defName child), and the attribute; nothing falls back
	/// to a value written in C++. Element text is trimmed before it is parsed.
	class ConfigReader {
	  public:
		explicit ConfigReader(std::string fileName)
			: file(std::move(fileName)) {}

		/// How a problem names an element: `<Task name="Haul">`, `<Chain defName="Chain_Haul">`, or `<Bonuses>`.
		[[nodiscard]] static std::string describe(const pugi::xml_node& element, const char* elementName) {
			std::string label = std::string("<") + elementName;
			for (const char* key : {"name", "defName"}) {
				const std::string instance = element.attribute(key).as_string();
				if (!instance.empty()) {
					return label + " " + key + "=\"" + instance + "\">";
				}
			}
			const std::string definition = textOf(element.child("defName"));
			if (!definition.empty()) {
				return label + " defName=\"" + definition + "\">";
			}
			return label + ">";
		}

		float attributeNumber(const pugi::xml_node& element, const char* elementName, const char* attribute) {
			return labeledNumber(describe(element, elementName), element, attribute);
		}

		int attributeInteger(
			const pugi::xml_node& element, const char* elementName, const char* attribute, int minimum = std::numeric_limits<int>::min(),
			int maximum = std::numeric_limits<int>::max()) {
			return labeledInteger(describe(element, elementName), element, attribute, minimum, maximum);
		}

		/// An attribute of an element the loader names itself, as `thought 'ate' feeling 1`.
		float labeledNumber(const std::string& label, const pugi::xml_node& element, const char* attribute) {
			return number(label, attribute, element.attribute(attribute));
		}

		int labeledInteger(
			const std::string& label, const pugi::xml_node& element, const char* attribute, int minimum = std::numeric_limits<int>::min(),
			int maximum = std::numeric_limits<int>::max()) {
			return integer(label, attribute, element.attribute(attribute), minimum, maximum);
		}

		float childNumber(const pugi::xml_node& parent, const char* parentName, const char* child) {
			return number(describe(parent, parentName), child, parent.child(child));
		}

		int childInteger(
			const pugi::xml_node& parent, const char* parentName, const char* child, int minimum = std::numeric_limits<int>::min(),
			int maximum = std::numeric_limits<int>::max()) {
			return integer(describe(parent, parentName), child, parent.child(child), minimum, maximum);
		}

		/// An optional true-or-false attribute: nothing when it is left out (the caller gives what that means), and
		/// a problem when it is anything other than exactly `true` or `false`.
		std::optional<bool> attributeBoolean(const pugi::xml_node& element, const char* elementName, const char* attribute) {
			return labeledBoolean(describe(element, elementName), element, attribute);
		}

		std::optional<bool> labeledBoolean(const std::string& label, const pugi::xml_node& element, const char* attribute) {
			return boolean(label, attribute, element.attribute(attribute));
		}

		std::optional<bool> childBoolean(const pugi::xml_node& parent, const char* parentName, const char* child) {
			return labeledChildBoolean(describe(parent, parentName), parent, child);
		}

		std::optional<bool> labeledChildBoolean(const std::string& label, const pugi::xml_node& parent, const char* child) {
			return boolean(label, child, parent.child(child));
		}

		/// Reports a number that was read but is outside what the loader allows, as "must be <expected>, got ...".
		void refuseAttribute(const std::string& label, const pugi::xml_node& element, const char* attribute, const std::string& expected) {
			report(label, attribute, element.attribute(attribute), expected);
		}

		void refuseChild(const pugi::xml_node& parent, const char* parentName, const char* child, const std::string& expected) {
			report(describe(parent, parentName), child, parent.child(child), expected);
		}

		[[nodiscard]] std::size_t problemCount() const { return problemList.size(); }

		[[nodiscard]] std::vector<std::string> takeProblems() { return std::exchange(problemList, {}); }

	  private:
		static constexpr const char* kWhitespace = " \t\r\n";

		static std::string textOf(const pugi::xml_attribute& attribute) { return attribute.as_string(); }
		static std::string textOf(const pugi::xml_node& element) {
			const std::string text = element.text().as_string();
			const std::size_t first = text.find_first_not_of(kWhitespace);
			if (first == std::string::npos) {
				return {};
			}
			return text.substr(first, text.find_last_not_of(kWhitespace) - first + 1);
		}
		static bool present(const pugi::xml_attribute& attribute) { return !attribute.empty(); }
		static bool present(const pugi::xml_node& element) { return !element.empty(); }

		template <typename Source> float number(const std::string& element, const char* name, const Source& source) {
			const std::optional<float> parsed = present(source) ? parseConfigNumber(textOf(source)) : std::nullopt;
			if (parsed.has_value()) {
				return *parsed;
			}
			report(element, name, source, "a number");
			return std::numeric_limits<float>::quiet_NaN();
		}

		template <typename Source> int integer(const std::string& element, const char* name, const Source& source, int minimum, int maximum) {
			const std::optional<int> parsed = present(source) ? parseConfigInteger(textOf(source)) : std::nullopt;
			if (parsed.has_value() && *parsed >= minimum && *parsed <= maximum) {
				return *parsed;
			}
			const bool bounded = minimum != std::numeric_limits<int>::min() || maximum != std::numeric_limits<int>::max();
			report(element, name, source, bounded ? "a whole number from " + std::to_string(minimum) + " to " + std::to_string(maximum) : "a whole number");
			return 0;
		}

		template <typename Source> std::optional<bool> boolean(const std::string& element, const char* name, const Source& source) {
			if (!present(source)) {
				return std::nullopt;
			}
			const std::string text = textOf(source);
			if (text == "true" || text == "false") {
				return text == "true";
			}
			report(element, name, source, "true or false");
			return std::nullopt;
		}

		template <typename Source> void report(const std::string& element, const char* name, const Source& source, const std::string& expected) {
			std::string problem = file + ": " + element + " " + name;
			problem += present(source) ? " must be " + expected + ", got \"" + textOf(source) + "\"" : " is missing";
			problemList.push_back(std::move(problem));
		}

		std::string				 file;
		std::vector<std::string> problemList;
	};

} // namespace assets

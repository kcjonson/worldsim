#pragma once

#include <pugixml.hpp>

#include <charconv>
#include <cmath>
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

	/// Collects what is wrong with the numbers of one config file, for the config's validation to report.
	/// A bad value reads as NaN (0 for a whole number) and a problem names the file, the element with its
	/// name attribute, and the attribute; nothing falls back to a value written in C++. Element text is
	/// trimmed before it is parsed.
	class ConfigReader {
	  public:
		explicit ConfigReader(std::string fileName)
			: file(std::move(fileName)) {}

		float attributeNumber(const pugi::xml_node& element, const char* elementName, const char* attribute) {
			return number(describe(element, elementName), attribute, element.attribute(attribute));
		}

		int attributeInteger(
			const pugi::xml_node& element, const char* elementName, const char* attribute, int minimum = std::numeric_limits<int>::min(),
			int maximum = std::numeric_limits<int>::max()) {
			return integer(describe(element, elementName), attribute, element.attribute(attribute), minimum, maximum);
		}

		float childNumber(const pugi::xml_node& parent, const char* parentName, const char* child) {
			return number(describe(parent, parentName), child, parent.child(child));
		}

		int childInteger(
			const pugi::xml_node& parent, const char* parentName, const char* child, int minimum = std::numeric_limits<int>::min(),
			int maximum = std::numeric_limits<int>::max()) {
			return integer(describe(parent, parentName), child, parent.child(child), minimum, maximum);
		}

		[[nodiscard]] std::vector<std::string>		  takeProblems() { return std::exchange(problemList, {}); }

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

		static std::string describe(const pugi::xml_node& element, const char* elementName) {
			std::string label = std::string("<") + elementName;
			const std::string instance = element.attribute("name").as_string();
			if (!instance.empty()) {
				label += " name=\"" + instance + "\"";
			}
			return label + ">";
		}

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
			report(element, name, source,
				   bounded ? ("a whole number from " + std::to_string(minimum) + " to " + std::to_string(maximum)).c_str() : "a whole number");
			return 0;
		}

		template <typename Source> void report(const std::string& element, const char* name, const Source& source, const char* expected) {
			std::string problem = file + ": " + element + " " + name;
			problem += present(source) ? std::string(" must be ") + expected + ", got \"" + textOf(source) + "\"" : " is missing";
			problemList.push_back(std::move(problem));
		}

		std::string				 file;
		std::vector<std::string> problemList;
	};

} // namespace assets

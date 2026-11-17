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

	/// A config attribute's whole text as a number: nothing for empty text or text that is not entirely a
	/// number, so "30abc" is refused rather than read as 30.
	[[nodiscard]] inline std::optional<float> parseConfigNumber(std::string_view text) {
		float number = 0.0F;
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
		if (text.empty() || error != std::errc() || end != text.data() + text.size()) {
			return std::nullopt;
		}
		return number;
	}

	/// A config attribute's whole text as a whole number, with the same refusals as parseConfigNumber.
	[[nodiscard]] inline std::optional<int> parseConfigInteger(std::string_view text) {
		int number = 0;
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
		if (text.empty() || error != std::errc() || end != text.data() + text.size()) {
			return std::nullopt;
		}
		return number;
	}

	/// Reads the numbers of one config file and keeps what is wrong with them. A missing or malformed
	/// value reads as NaN (or 0 for a whole number) and adds a problem naming the file, the element and
	/// the attribute (a number that is not finite counts as malformed), which the config's validation reports; nothing falls back to a value written in C++.
	class ConfigReader {
	  public:
		explicit ConfigReader(std::string fileName)
			: file(std::move(fileName)) {}

		/// A number held in an attribute of element.
		float attributeNumber(const pugi::xml_node& element, const char* elementName, const char* attribute) {
			return number(elementName, attribute, element.attribute(attribute));
		}

		/// A whole number held in an attribute of element.
		int attributeInteger(const pugi::xml_node& element, const char* elementName, const char* attribute) {
			return integer(elementName, attribute, element.attribute(attribute));
		}

		/// A number held as the text of the child element of parent.
		float childNumber(const pugi::xml_node& parent, const char* parentName, const char* child) {
			return number(parentName, child, parent.child(child));
		}

		/// A whole number held as the text of the child element of parent.
		int childInteger(const pugi::xml_node& parent, const char* parentName, const char* child) {
			return integer(parentName, child, parent.child(child));
		}

		[[nodiscard]] const std::vector<std::string>& problems() const { return problemList; }
		[[nodiscard]] std::vector<std::string>		  takeProblems() { return std::exchange(problemList, {}); }

	  private:
		static const char* textOf(const pugi::xml_attribute& attribute) { return attribute.as_string(); }
		static const char* textOf(const pugi::xml_node& element) { return element.text().as_string(); }
		static bool		   present(const pugi::xml_attribute& attribute) { return !attribute.empty(); }
		static bool		   present(const pugi::xml_node& element) { return !element.empty(); }

		template <typename Source> float number(const char* element, const char* name, const Source& source) {
			const std::optional<float> parsed = present(source) ? parseConfigNumber(textOf(source)) : std::nullopt;
			if (parsed.has_value() && std::isfinite(*parsed)) {
				return *parsed;
			}
			report(element, name, source, "a number");
			return std::numeric_limits<float>::quiet_NaN();
		}

		template <typename Source> int integer(const char* element, const char* name, const Source& source) {
			const std::optional<int> parsed = present(source) ? parseConfigInteger(textOf(source)) : std::nullopt;
			if (parsed.has_value()) {
				return *parsed;
			}
			report(element, name, source, "a whole number");
			return 0;
		}

		template <typename Source> void report(const char* element, const char* name, const Source& source, const char* expected) {
			std::string problem = file + ": <" + element + "> " + name;
			problem += present(source) ? std::string(" must be ") + expected + ", got \"" + textOf(source) + "\"" : " is missing";
			problemList.push_back(std::move(problem));
		}

		std::string				 file;
		std::vector<std::string> problemList;
	};

} // namespace assets

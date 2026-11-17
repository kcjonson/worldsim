#pragma once

// Reading helpers shared by the config loaders that collect every problem into an error list.

#include <pugixml.hpp>

#include <algorithm>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace assets {

	/// The whitespace-separated names in an element's text.
	[[nodiscard]] inline std::vector<std::string> splitNames(const char* text) {
		std::vector<std::string> names;
		std::istringstream		 stream(text);
		std::string				 name;
		while (stream >> name) {
			names.push_back(std::move(name));
		}
		return names;
	}

	/// False when `node` has an attribute not in `allowed`, each one added to `errors` as
	/// "<source>: <what>: unknown attribute '<name>'".
	inline bool onlyAttributes(
		pugi::xml_node							node,
		std::initializer_list<std::string_view> allowed,
		const std::string&						what,
		const std::string&						source,
		std::vector<std::string>&				errors
	) {
		bool clean = true;
		for (const pugi::xml_attribute attribute : node.attributes()) {
			if (std::ranges::find(allowed, std::string_view(attribute.name())) == allowed.end()) {
				errors.push_back(source + ": " + what + ": unknown attribute '" + attribute.name() + "'");
				clean = false;
			}
		}
		return clean;
	}

} // namespace assets

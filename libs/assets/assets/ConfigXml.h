#pragma once

// Reading helpers shared by the config loaders that collect every problem into an error list.

#include <pugixml.hpp>

#include <algorithm>
#include <filesystem>
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

	/// The `rootName` element of the XML file at `path`, which errors call `source`; an empty node, with the reason
	/// added to `errors` as "<source>: not readable XML: ..." or "<source>: the root element is not <rootName>", when
	/// the file can't be read or has another root.
	inline pugi::xml_node readRootElement(
		pugi::xml_document&			 document,
		const std::filesystem::path& path,
		const char*					 rootName,
		const std::string&			 source,
		std::vector<std::string>&	 errors
	) {
		const pugi::xml_parse_result parsed = document.load_file(path.string().c_str());
		if (!parsed) {
			errors.push_back(source + ": not readable XML: " + parsed.description());
			return {};
		}
		const pugi::xml_node root = document.child(rootName);
		if (!root) {
			errors.push_back(source + ": the root element is not <" + rootName + ">");
		}
		return root;
	}

} // namespace assets

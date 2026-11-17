#include "assets/NeedsConfig.h"
#include "assets/ConfigNumber.h"

#include <pugixml.hpp>

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>

namespace assets {

	std::size_t needThresholdIndex(const std::string& name) {
		const auto found = std::ranges::find(kNeedThresholdNames, std::string_view(name), [](const char* threshold) {
			return std::string_view(threshold);
		});
		return static_cast<std::size_t>(found - kNeedThresholdNames.begin());
	}

	NeedsConfigLoad loadNeedsConfig(const std::filesystem::path& file) {
		NeedsConfigLoad	  result;
		const std::string source = file.filename().generic_string();
		const auto		  fail = [&](const std::string& message) {
			   result.errors.push_back(source + ": " + message);
		};

		pugi::xml_document			 document;
		const pugi::xml_parse_result parsed = document.load_file(file.string().c_str());
		if (!parsed) {
			fail(std::string("not readable XML: ") + parsed.description());
			return result;
		}
		const pugi::xml_node root = document.child("Needs");
		if (!root) {
			fail("the root element is not <Needs>");
			return result;
		}

		for (const pugi::xml_node node : root.children()) {
			if (node.type() != pugi::node_element) {
				continue;
			}
			if (std::string_view(node.name()) != "Need") {
				fail("unknown element <" + std::string(node.name()) + ">");
				continue;
			}
			NeedThresholdsDef need{.need = node.attribute("name").as_string()};
			if (need.need.empty()) {
				fail("a <Need> has no name");
				continue;
			}
			const std::string what = "<Need name=\"" + need.need + "\">";
			bool			  valid = true;
			for (const pugi::xml_attribute attribute : node.attributes()) {
				const std::string name = attribute.name();
				if (name != "name" && needThresholdIndex(name) == kNeedThresholdNames.size()) {
					fail(what + ": unknown attribute '" + name + "'");
					valid = false;
				}
			}
			for (std::size_t index = 0; index < kNeedThresholdNames.size(); ++index) {
				const char*				   name = kNeedThresholdNames[index];
				const std::optional<float> value = parseConfigNumber(node.attribute(name).as_string());
				if (!value.has_value() || *value < 0.0F || *value > 100.0F) {
					fail(what + ": " + name + "=\"" + node.attribute(name).as_string() + "\" is not a need percentage from 0 to 100");
					valid = false;
					continue;
				}
				need.thresholds[index] = *value;
				if (index > 0 && valid && need.thresholds[index] >= need.thresholds[index - 1]) {
					fail(what + ": " + name + " must be below " + kNeedThresholdNames[index - 1]);
					valid = false;
				}
			}
			if (std::ranges::find(result.config.needs, need.need, &NeedThresholdsDef::need) != result.config.needs.end()) {
				fail(what + " is given twice");
				valid = false;
			}
			if (valid) {
				result.config.needs.push_back(std::move(need));
			}
		}
		return result;
	}

} // namespace assets

#include "assets/AttributeConfig.h"

#include "assets/ConfigFiles.h"
#include "assets/ConfigNumber.h"
#include "assets/ConfigXml.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace assets {

	namespace {

		struct Declared {
			AttributeRange range;
			std::string	   source;
		};

		std::string nameList(std::span<const std::string_view> names) {
			std::string list;
			for (const std::string_view name : names) {
				list += (list.empty() ? "" : ", ") + std::string(name);
			}
			return list;
		}

		class Loader {
		  public:
			explicit Loader(std::span<const std::string_view> knownNames)
				: names(knownNames) {}

			std::map<std::string, Declared> declared;
			std::set<std::string>			failed;
			std::vector<std::string>		errors;
			bool							unreadable = false;

			void loadFile(const std::filesystem::path& path, const std::string& source) {
				pugi::xml_document			 document;
				const pugi::xml_parse_result parsed = document.load_file(path.string().c_str());
				if (!parsed) {
					unreadable = true;
					fail(source, std::string("not readable XML: ") + parsed.description());
					return;
				}
				const pugi::xml_node root = document.child("Attributes");
				if (!root) {
					unreadable = true;
					fail(source, "the root element is not <Attributes>");
					return;
				}
				std::vector<std::string> declaredHere;
				for (const pugi::xml_node node : root.children()) {
					if (node.type() == pugi::node_pcdata || node.type() == pugi::node_cdata) {
						fail(source, "unexpected text inside <Attributes>");
						continue;
					}
					if (node.type() != pugi::node_element) {
						continue;
					}
					if (std::string_view(node.name()) == "Attribute") {
						const std::size_t errorsBefore = errors.size();
						loadAttribute(node, source, declaredHere);
						if (errors.size() > errorsBefore) {
							failed.insert(node.attribute("name").as_string());
						}
					} else {
						fail(source, "unknown element <" + std::string(node.name()) + ">");
					}
				}
			}

			void check() {
				if (unreadable) {
					return;
				}
				for (const std::string_view name : names) {
					if (!declared.contains(std::string(name)) && !failed.contains(std::string(name))) {
						fail("attributes", "no file gives <Attribute name=\"" + std::string(name) + "\"> a range");
					}
				}
			}

		  private:
			void fail(const std::string& source, const std::string& message) { errors.push_back(source + ": " + message); }

			void loadAttribute(pugi::xml_node node, const std::string& source, std::vector<std::string>& declaredHere) {
				const std::string name = node.attribute("name").as_string();
				const std::string what = "<Attribute name=\"" + name + "\">";
				if (!onlyAttributes(node, {"name", "min", "max"}, what, source, errors)) {
					return;
				}
				if (std::ranges::find(names, name) == names.end()) {
					fail(source, what + ": no such attribute (the attributes are " + nameList(names) + ")");
					return;
				}
				if (std::ranges::find(declaredHere, name) != declaredHere.end()) {
					fail(source, what + " is given twice in this file");
					return;
				}
				declaredHere.push_back(name);

				ConfigReader reader(source);
				AttributeRange range;
				range.min = reader.attributeNumber(node, "Attribute", "min");
				range.max = reader.attributeNumber(node, "Attribute", "max");
				if (std::vector<std::string> problems = reader.takeProblems(); !problems.empty()) {
					errors.insert(errors.end(), problems.begin(), problems.end());
					return;
				}
				if (range.min < kAttributeScaleMin || range.max > kAttributeScaleMax || range.min > range.max) {
					fail(
						source,
						what + ": min " + std::to_string(range.min) + " and max " + std::to_string(range.max) + " must satisfy " +
							std::to_string(kAttributeScaleMin) + " <= min <= max <= " + std::to_string(kAttributeScaleMax)
					);
					return;
				}
				Declared& slot = declared[name];
				if (!slot.source.empty()) {
					LOG_INFO(Engine, "Attribute %s: %s replaces the range from %s", name.c_str(), source.c_str(), slot.source.c_str());
				}
				slot = {.range = std::move(range), .source = source};
			}

			std::span<const std::string_view> names;
		};

	} // namespace

	AttributeConfigLoad loadAttributeConfig(const std::filesystem::path& folder, std::span<const std::string_view> names) {
		AttributeConfigLoad load;
		const ConfigFiles	found = configFilesIn(folder);
		if (found.error.has_value()) {
			load.errors.push_back(*found.error);
			return load;
		}
		Loader loader(names);
		for (const std::filesystem::path& file : found.files) {
			loader.loadFile(file, configFileName(file, folder));
		}
		loader.check();
		if (!loader.errors.empty()) {
			load.errors = std::move(loader.errors);
			return load;
		}
		for (const std::string_view name : names) {
			load.ranges.push_back(loader.declared.at(std::string(name)).range);
		}
		return load;
	}

} // namespace assets

#include "assets/NeedsConfig.h"
#include "assets/ConfigFiles.h"
#include "assets/ConfigNumber.h"
#include "assets/ConfigXml.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <string_view>
#include <utility>

namespace assets {

	namespace {

		/// The <Need> elements of one file; nothing when the file has any error, each reported in `errors`.
		std::vector<NeedDef> needsInFile(const std::filesystem::path& file, const std::string& source, std::vector<std::string>& errors) {
			const std::size_t errorsBefore = errors.size();
			const auto		  fail = [&](const std::string& message) {
				   errors.push_back(source + ": " + message);
			};

			pugi::xml_document			 document;
			const pugi::xml_parse_result parsed = document.load_file(file.string().c_str());
			if (!parsed) {
				fail(std::string("not readable XML: ") + parsed.description());
				return {};
			}
			const pugi::xml_node root = document.child("Needs");
			if (!root) {
				fail("the root element is not <Needs>");
				return {};
			}

			ConfigReader				   reader(source);
			std::vector<NeedDef>		   needs;
			std::set<std::string>		   named;
			for (const pugi::xml_node node : root.children()) {
				if (node.type() != pugi::node_element) {
					continue;
				}
				if (std::string_view(node.name()) != "Need") {
					fail("unknown element <" + std::string(node.name()) + ">");
					continue;
				}
				NeedDef need{.need = node.attribute("name").as_string(), .source = source};
				if (need.need.empty()) {
					fail("a <Need> has no name");
					continue;
				}
				const std::string what = "<Need name=\"" + need.need + "\">";
				if (!named.insert(need.need).second) {
					fail(what + " is given twice");
				}
				onlyAttributes(node, {"name", "seek", "urgent", "critical", "cost"}, what, source, errors);
				bool numbersRead = true;
				for (std::size_t index = 0; index < kNeedThresholdNames.size(); ++index) {
					const char* name = kNeedThresholdNames[index];
					need.thresholds[index] = reader.attributeNumber(node, "Need", name);
					if (std::isnan(need.thresholds[index])) {
						numbersRead = false;
					} else if (need.thresholds[index] < 0.0F || need.thresholds[index] > 100.0F) {
						fail(
							what + " " + name + " must be a need percentage from 0 to 100, got \"" + node.attribute(name).as_string() + "\""
						);
						numbersRead = false;
					}
				}
				for (std::size_t index = 1; numbersRead && index < kNeedThresholdNames.size(); ++index) {
					if (need.thresholds[index] >= need.thresholds[index - 1]) {
						fail(what + " " + kNeedThresholdNames[index] + " must be below " + kNeedThresholdNames[index - 1]);
					}
				}
				need.moodCost = reader.attributeNumber(node, "Need", "cost");
				if (need.moodCost < 0.0F) {
					reader.refuseAttribute(what, node, "cost", "zero or more");
				}
				needs.push_back(std::move(need));
			}
			for (std::string& problem : reader.takeProblems()) {
				errors.push_back(std::move(problem));
			}
			if (errors.size() != errorsBefore) {
				return {};
			}
			return needs;
		}

	} // namespace

	std::size_t needThresholdIndex(const std::string& name) {
		const auto found = std::ranges::find(kNeedThresholdNames, std::string_view(name), [](const char* threshold) {
			return std::string_view(threshold);
		});
		return static_cast<std::size_t>(found - kNeedThresholdNames.begin());
	}

	NeedsConfigLoad loadNeedsConfig(const std::vector<std::filesystem::path>& files, const std::filesystem::path& folder) {
		NeedsConfigLoad result;
		for (const std::filesystem::path& file : files) {
			for (NeedDef& need : needsInFile(file, configFileName(file, folder), result.errors)) {
				const auto existing = std::ranges::find(result.config.needs, need.need, &NeedDef::need);
				if (existing == result.config.needs.end()) {
					result.config.needs.push_back(std::move(need));
					continue;
				}
				LOG_INFO(
					Engine,
					"Needs config: <Need name=\"%s\"> in %s replaces the one in %s",
					need.need.c_str(),
					need.source.c_str(),
					existing->source.c_str()
				);
				*existing = std::move(need);
			}
		}
		return result;
	}

} // namespace assets

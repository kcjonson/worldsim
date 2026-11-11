#include "TreeDefSchema.h"

#include "assets/DetailLevels.h"

#include <pugixml.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <set>

namespace assets {

	namespace {

		// A whole attribute that is one finite number; nullopt when missing or anything else.
		std::optional<double> parseNumber(const pugi::xml_attribute& attr) {
			if (!attr) {
				return std::nullopt;
			}
			const char* text = attr.value();
			char*		end = nullptr;
			const double value = std::strtod(text, &end);
			if (end == text || *end != '\0' || !std::isfinite(value)) {
				return std::nullopt;
			}
			return value;
		}

		// parseNumber for an attribute stored as a float: nullopt when the value is beyond the
		// float range or a nonzero value would narrow to zero.
		std::optional<double> parseFloatNumber(const pugi::xml_attribute& attr) {
			const std::optional<double> value = parseNumber(attr);
			if (!value.has_value() || std::abs(*value) > std::numeric_limits<float>::max() ||
				(*value != 0.0 && static_cast<float>(*value) == 0.0F)) {
				return std::nullopt;
			}
			return value;
		}

		// A positive scale attribute; 1 when left out.
		std::optional<float> readScaleFactor(const pugi::xml_node& node, const char* name, std::vector<TreeDefIssue>& issues) {
			const pugi::xml_attribute attr = node.attribute(name);
			if (!attr) {
				return 1.0F;
			}
			const std::optional<double> value = parseFloatNumber(attr);
			if (!value.has_value() || *value <= 0.0) {
				issues.push_back({"ageClass", std::string(name) + " must be a number above 0 that a float can hold", attr.value()});
				return std::nullopt;
			}
			return static_cast<float>(*value);
		}

	} // namespace

	std::vector<AgeClass> readAgeClasses(const pugi::xml_node& defNode, std::vector<TreeDefIssue>& issues) {
		const pugi::xml_node block = defNode.child("ageClasses");
		if (!block) {
			return {};
		}
		const size_t issuesBefore = issues.size();

		if (defNode.child("variantCount")) {
			issues.push_back({"ageClasses", "<ageClasses> replaces <variantCount>; a def may not have both", defNode.child("variantCount").text().as_string()});
		}

		std::vector<AgeClass>  classes;
		std::set<std::string>  names;
		double				   weightSum = 0.0;
		for (const pugi::xml_node node : block.children("ageClass")) {
			AgeClass ageClass;
			ageClass.name = node.attribute("name").as_string();
			if (ageClass.name.empty()) {
				issues.push_back({"ageClass", "name must not be empty", ""});
			} else if (!names.insert(ageClass.name).second) {
				issues.push_back({"ageClass", "duplicate age class name", ageClass.name});
			}

			const std::optional<double> weight = parseFloatNumber(node.attribute("weight"));
			if (!weight.has_value() || *weight <= 0.0) {
				issues.push_back({"ageClass", "weight must be a number above 0 that a float can hold", node.attribute("weight").value()});
			} else {
				ageClass.weight = static_cast<float>(*weight);
				weightSum += *weight;
			}

			const std::optional<double> variants = parseNumber(node.attribute("variants"));
			if (!variants.has_value() || *variants < 1.0 || *variants > std::numeric_limits<uint16_t>::max() ||
				*variants != std::floor(*variants)) {
				issues.push_back({"ageClass", "variants must be a whole number from 1 to 65535", node.attribute("variants").value()});
			} else {
				ageClass.variants = static_cast<uint16_t>(*variants);
			}

			const std::optional<float> yieldScale = readScaleFactor(node, "yieldScale", issues);
			const std::optional<float> workScale = readScaleFactor(node, "workScale", issues);
			ageClass.yieldScale = yieldScale.value_or(1.0F);
			ageClass.workScale = workScale.value_or(1.0F);
			classes.push_back(std::move(ageClass));
		}

		if (classes.empty()) {
			issues.push_back({"ageClasses", "<ageClasses> needs at least one <ageClass>", ""});
		} else if (classes.size() > std::numeric_limits<uint8_t>::max() + 1U) {
			issues.push_back({"ageClasses", "at most 256 age classes", std::to_string(classes.size())});
		}
		if (issues.size() == issuesBefore && std::abs(weightSum - 1.0) > kAgeWeightTolerance) {
			issues.push_back({"ageClasses", "age class weights must sum to 1", std::to_string(weightSum)});
		}

		if (issues.size() != issuesBefore) {
			return {};
		}
		return classes;
	}

	std::optional<PlacementScale> readPlacementScale(const pugi::xml_node& defNode, std::vector<TreeDefIssue>& issues) {
		const pugi::xml_node node = defNode.child("placement").child("scale");
		if (!node) {
			return std::nullopt;
		}
		const std::optional<double> min = parseFloatNumber(node.attribute("min"));
		const std::optional<double> max = parseFloatNumber(node.attribute("max"));
		if (!min.has_value() || !max.has_value() || *min <= 0.0 || *max < *min) {
			issues.push_back(
				{"scale",
				 "<scale> needs numbers min and max a float can hold with 0 < min <= max",
				 std::string(node.attribute("min").value()) + "," + node.attribute("max").value()}
			);
			return std::nullopt;
		}
		return PlacementScale{static_cast<float>(*min), static_cast<float>(*max)};
	}

	std::optional<uint16_t> readSamplePitch(const pugi::xml_node& defNode, std::vector<TreeDefIssue>& issues) {
		const pugi::xml_node node = defNode.child("placement").child("samplePitch");
		if (!node) {
			return std::nullopt;
		}
		const std::string text = node.text().as_string();
		if (text != "1" && text != "2" && text != "4") {
			issues.push_back({"samplePitch", "<samplePitch> must be 1, 2 or 4 meters", text});
			return std::nullopt;
		}
		return static_cast<uint16_t>(text[0] - '0');
	}

	std::optional<float> readCrownSpacing(const pugi::xml_node& defNode, std::vector<TreeDefIssue>& issues) {
		const pugi::xml_node node = defNode.child("placement").child("crownSpacing");
		if (!node) {
			return std::nullopt;
		}
		const std::string text = node.text().as_string();
		char*			  end = nullptr;
		const double	  value = std::strtod(text.c_str(), &end);
		if (end == text.c_str() || *end != '\0' || !std::isfinite(value) || value <= 0.0 || value > 2.0) {
			issues.push_back({"crownSpacing", "<crownSpacing> must be a number above 0 and at most 2", text});
			return std::nullopt;
		}
		return static_cast<float>(value);
	}

	std::vector<float> readDetailLevels(const pugi::xml_node& defNode, std::vector<TreeDefIssue>& issues) {
		const pugi::xml_node node = defNode.child("detailLevels");
		if (!node) {
			return {};
		}
		const std::string text = node.text().as_string();
		const std::string assetType = defNode.child("assetType").text().as_string();
		if (assetType != "procedural") {
			issues.push_back({"detailLevels", "only a procedural def's generator can cut a mesh for a density", text});
			return {};
		}

		std::vector<float> levels;
		bool			   valid = true;
		size_t			   start = 0;
		while (valid && start <= text.size()) {
			const size_t	  comma = std::min(text.find(',', start), text.size());
			const std::string item = text.substr(start, comma - start);
			const char*		  first = item.c_str();
			while (*first == ' ' || *first == '\t' || *first == '\n' || *first == '\r') {
				++first;
			}
			char*		 end = nullptr;
			const double value = std::strtod(first, &end);
			while (end != nullptr && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
				++end;
			}
			valid = end != first && *end == '\0' && std::isfinite(value) && value > 0.0 && value <= std::numeric_limits<float>::max() &&
					(levels.empty() || static_cast<float>(value) > levels.back());
			levels.push_back(static_cast<float>(value));
			start = comma + 1;
		}
		if (!valid || levels.size() > kMaxDetailLevels) {
			issues.push_back(
				{"detailLevels",
				 "must list 1 to " + std::to_string(kMaxDetailLevels) + " pixels-a-meter densities above 0, strictly ascending, comma separated",
				 text}
			);
			return {};
		}
		return levels;
	}

} // namespace assets

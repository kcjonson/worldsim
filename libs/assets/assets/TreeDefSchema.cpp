#include "TreeDefSchema.h"

#include <pugixml.hpp>

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

		// A positive scale attribute; 1 when left out.
		std::optional<float> readScaleFactor(const pugi::xml_node& node, const char* name, std::vector<TreeDefIssue>& issues) {
			const pugi::xml_attribute attr = node.attribute(name);
			if (!attr) {
				return 1.0F;
			}
			const std::optional<double> value = parseNumber(attr);
			if (!value.has_value() || *value <= 0.0) {
				issues.push_back({"ageClass", std::string(name) + " must be a number above 0", attr.value()});
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

			const std::optional<double> weight = parseNumber(node.attribute("weight"));
			if (!weight.has_value() || *weight <= 0.0) {
				issues.push_back({"ageClass", "weight must be a number above 0", node.attribute("weight").value()});
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
		const std::optional<double> min = parseNumber(node.attribute("min"));
		const std::optional<double> max = parseNumber(node.attribute("max"));
		if (!min.has_value() || !max.has_value() || *min <= 0.0 || *max < *min) {
			issues.push_back(
				{"scale",
				 "<scale> needs numbers min and max with 0 < min <= max",
				 std::string(node.attribute("min").value()) + "," + node.attribute("max").value()}
			);
			return std::nullopt;
		}
		return PlacementScale{static_cast<float>(*min), static_cast<float>(*max)};
	}

} // namespace assets

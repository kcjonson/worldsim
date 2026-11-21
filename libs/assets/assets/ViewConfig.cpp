#include "assets/ViewConfig.h"

#include "assets/ConfigNumber.h"
#include "assets/ConfigXml.h"
#include "assets/PartsAsset.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace assets {

	float ViewConfig::outlineHalfWidthMeters() const {
		return std::max(outlineWidthMeters, outlineTopWidthMeters) * 0.5F;
	}

	GameViewConfig& GameViewConfig::Get() {
		static GameViewConfig instance;
		return instance;
	}

	bool GameViewConfig::loadFromFile(const std::filesystem::path& configRoot, const std::filesystem::path& file) {
		clear();

		const std::string	 source = file.generic_string();
		pugi::xml_document	 document;
		const pugi::xml_node root = readRootElement(document, configRoot / file, "WorldView", source, problemList);
		if (!root) {
			return false;
		}

		ConfigReader reader(source);
		ViewConfig	 read;
		// A number that must be above `low` (or at least it, when `inclusive`) and at most `high`.
		const auto	 bounded = [&](float value, const pugi::xml_node& element, const char* elementName, const char* attribute, float low, bool inclusive,
								   float high, const std::string& expected) {
			  if (std::isnan(value)) {
				  return value; // already reported as missing or malformed
			  }
			  const bool aboveLow = inclusive ? value >= low : value > low;
			  if (!aboveLow || value > high) {
				  if (attribute == nullptr) {
					  reader.refuseChild(root, "WorldView", elementName, expected);
				  } else {
					  reader.refuseAttribute(ConfigReader::describe(element, elementName), element, attribute, expected);
				  }
			  }
			  return value;
		};
		constexpr float kAny = 1.0e30F;
		const auto		child = [&](const char* name, float low, bool inclusive, const std::string& expected) {
			   return bounded(reader.childNumber(root, "WorldView", name), root, name, nullptr, low, inclusive, kAny, expected);
		};
		const auto attribute = [&](const char* elementName, const char* name, float low, bool inclusive, float high, const std::string& expected) {
			const pugi::xml_node element = root.child(elementName);
			return bounded(reader.attributeNumber(element, elementName, name), element, elementName, name, low, inclusive, high, expected);
		};
		const auto color = [&](const char* elementName, const char* name) {
			const pugi::xml_node				   element = root.child(elementName);
			const std::optional<Foundation::Color> parsed = parseRgbHexColor(element.attribute(name).as_string());
			if (!parsed.has_value()) {
				reader.refuseAttribute(ConfigReader::describe(element, elementName), element, name, "a colour, #RRGGBB");
				return Foundation::Color{};
			}
			return *parsed;
		};

		read.heightRatio = child("heightRatio", 0.0F, false, "above 0");
		read.lightAzimuthDegrees = attribute("light", "azimuthDegrees", 0.0F, true, 360.0F, "from 0 to 360");
		read.lightElevationDegrees = attribute("light", "elevationDegrees", 0.0F, true, 90.0F, "from 0 to 90");
		read.ambient = attribute("sideShading", "ambient", 0.0F, true, kAny, "at least 0");
		read.diffuse = attribute("sideShading", "diffuse", 0.0F, true, kAny, "at least 0");
		read.wrap = attribute("sideShading", "wrap", 0.0F, true, kAny, "at least 0");
		read.outlineColor = color("partOutline", "color");
		read.outlineWidthMeters = attribute("partOutline", "widthMeters", 0.0F, false, kAny, "above 0");
		read.outlineTopColor = color("partOutline", "topColor");
		read.outlineTopWidthMeters = attribute("partOutline", "topWidthMeters", 0.0F, false, kAny, "above 0");
		read.outlineTopColor.a = attribute("partOutline", "topOpacity", 0.0F, true, 1.0F, "from 0 to 1");
		read.aspectToleranceRelative = attribute("drawings", "aspectTolerance", 0.0F, false, kAny, "above 0");
		read.curveToleranceMeters = attribute("drawings", "curveToleranceMeters", 0.0F, false, kAny, "above 0");
		read.overflowMeters = attribute("drawings", "overflowMeters", 0.0F, true, kAny, "at least 0");
		read.faceDetailMinWidthMeters = attribute("drawings", "faceDetailMinWidthMeters", 0.0F, true, kAny, "at least 0");
		read.depthStripWidthMeters = child("depthStripWidthMeters", 0.0F, false, "above 0");
		read.depthStripRiseMeters = child("depthStripRiseMeters", 0.0F, false, "above 0");
		read.depthStripMergeMeters = child("depthStripMergeMeters", 0.0F, true, "at least 0");

		std::vector<std::string> problems = reader.takeProblems();
		if (!problems.empty()) {
			clear();
			problemList = std::move(problems);
			LOG_ERROR(Engine, "View config %s has %zu bad value(s)", source.c_str(), problemList.size());
			return false;
		}
		values = read;
		isLoaded = true;
		LOG_INFO(Engine, "Loaded view config from %s", source.c_str());
		return true;
	}

	void GameViewConfig::clear(const std::string& problem) {
		isLoaded = false;
		values = {};
		problemList.clear();
		if (!problem.empty()) {
			problemList.push_back(problem);
		}
	}

} // namespace assets

#pragma once

// Fixtures for parts defs (rotatable-placed-items.md): defs and drawings written to a temp folder, loaded through the
// registry with the shipped view config. Shared by PartsAsset.test.cpp and ItemView.test.cpp.

#include "assets/AssetRegistry.h"
#include "assets/AssetRegistry.test.h"
#include "assets/AssetValidator.h"
#include "assets/PartsAsset.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace assets::parts_fixtures {

	inline std::string number(float value) {
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
		return buffer;
	}

	// An SVG whose viewBox has the proportions of width x height (100 units a metre), filled edge to edge.
	inline std::string svg(float width, float height, const std::string& inner = "") {
		const std::string boxWidth = number(width * 100.0F);
		const std::string boxHeight = number(height * 100.0F);
		return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " + boxWidth + " " + boxHeight + "\"><rect x=\"0\" y=\"0\" width=\"" +
			   boxWidth + "\" height=\"" + boxHeight + "\" fill=\"#808080\"/>" + inner + "</svg>";
	}

	// An SVG of width x height metres (100 units a metre) holding only `inner`.
	inline std::string bareSvg(float width, float height, const std::string& inner) {
		return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " + number(width * 100.0F) + " " + number(height * 100.0F) + "\">" + inner + "</svg>";
	}

	inline std::string partsDef(const std::string& name, const std::string& inner) {
		return "<?xml version=\"1.0\"?><AssetDefinitions><AssetDef><defName>" + name + "</defName><assetType>parts</assetType>" + inner +
			   "</AssetDef></AssetDefinitions>";
	}

	inline std::string rect(float minX, float minY, float maxX, float maxY) {
		return "<rect minX=\"" + number(minX) + "\" minY=\"" + number(minY) + "\" maxX=\"" + number(maxX) + "\" maxY=\"" + number(maxY) + "\"/>";
	}

	// A plain block part: a rect footprint, a height, plain colours.
	inline std::string block(const std::string& name, float minX, float minY, float maxX, float maxY, float bottom, float top, const std::string& extra = "") {
		return "<part name=\"" + name + "\"><footprint>" + rect(minX, minY, maxX, maxY) + "</footprint><height bottom=\"" + number(bottom) +
			   "\" top=\"" + number(top) + "\"/><top color=\"#9A6A3F\"/>" + (top > bottom ? "<sides color=\"#9A6A3F\"/>" : "") + extra + "</part>";
	}

	inline std::string place(const std::string& part, float x = 0.0F, float y = 0.0F, float turnDegrees = 0.0F) {
		return "<place part=\"" + part + "\" x=\"" + number(x) + "\" y=\"" + number(y) + "\" turnDegrees=\"" + number(turnDegrees) + "\"/>";
	}

	class PartsTest : public ::testing::Test {
	  protected:
		void SetUp() override { ASSERT_TRUE(loadTestViewConfig()); }
		void TearDown() override { AssetRegistry::Get().clearDefinitions(); }

		const AssetDefinition* load(const std::string& name) {
			AssetRegistry& registry = AssetRegistry::Get();
			registry.clearDefinitions();
			registry.loadDefinitionsFromFolder(folder.root.string());
			return registry.getDefinition(name);
		}

		void writeDef(const std::string& name, const std::string& inner) { folder.write(name + "/" + name + ".xml", partsDef(name, inner)); }

		std::vector<std::string> issues(const std::string& defName, Severity severity = Severity::Error) const {
			std::vector<std::string> out;
			for (const ValidationIssue& issue : AssetRegistry::Get().getValidationReport().issues) {
				if (issue.defName == defName && issue.severity == severity) {
					out.push_back(issue.message);
				}
			}
			return out;
		}

		::testing::AssertionResult hasIssue(const std::string& defName, const std::string& part, Severity severity = Severity::Error) const {
			const std::vector<std::string> found = issues(defName, severity);
			if (std::any_of(found.begin(), found.end(), [&](const std::string& message) { return message.find(part) != std::string::npos; })) {
				return ::testing::AssertionSuccess();
			}
			std::string all;
			for (const std::string& message : found) {
				all += "\n  " + message;
			}
			return ::testing::AssertionFailure() << "no issue containing '" << part << "' for " << defName << "; got:" << all;
		}

		// The workbench of section 4.4, every drawing at its face's proportions.
		void writeWorkbench(const std::string& extraParts = "", const std::string& extraPlacements = "") {
			const std::string dir = "Workbench/";
			folder.write(dir + "carcass-front.svg", svg(2.84F, 0.72F));
			folder.write(dir + "carcass-back.svg", svg(2.84F, 0.72F));
			folder.write(dir + "carcass-end.svg", svg(0.84F, 0.72F));
			folder.write(dir + "worktop-top.svg", svg(3.0F, 1.0F, "<g data-lod=\"60\"><rect x=\"10\" y=\"10\" width=\"20\" height=\"20\" fill=\"#000\"/></g>"));
			folder.write(dir + "worktop-end.svg", svg(1.0F, 0.08F));
			folder.write(dir + "vise-top.svg", svg(0.32F, 0.3F));
			folder.write(dir + "vise-front.svg", svg(0.32F, 0.18F));
			folder.write(dir + "vise-end.svg", svg(0.3F, 0.18F));
			folder.write(dir + "toolbox-top.svg", svg(0.46F, 0.28F));
			folder.write(dir + "toolbox-front.svg", svg(0.46F, 0.2F));
			folder.write(dir + "toolbox-back.svg", svg(0.46F, 0.2F));
			folder.write(dir + "toolbox-end.svg", svg(0.28F, 0.2F));
			writeDef(
				"Workbench",
				"<category>Furniture</category><handsRequired>2</handsRequired><capabilities><craftable/></capabilities>"
				"<detailLevels>8, 24, 60</detailLevels><parts>"
				"<part name=\"carcass\"><footprint>" + rect(-1.42F, -0.42F, 1.42F, 0.42F) + "</footprint><height bottom=\"0\" top=\"0.72\"/>"
				"<top color=\"#9A6A3F\"/><sides color=\"#9A6A3F\"><face side=\"front\" drawing=\"carcass-front.svg\"/>"
				"<face side=\"back\" drawing=\"carcass-back.svg\"/><face side=\"left right\" drawing=\"carcass-end.svg\"/></sides></part>"
				"<part name=\"worktop\"><footprint>" + rect(-1.5F, -0.5F, 1.5F, 0.5F) + "</footprint><height bottom=\"0.72\" top=\"0.8\"/>"
				"<top drawing=\"worktop-top.svg\"/><sides color=\"#B38654\"><face side=\"left right\" drawing=\"worktop-end.svg\"/></sides></part>"
				"<part name=\"vise\"><footprint>" + rect(-0.16F, -0.15F, 0.16F, 0.15F) + "</footprint><height bottom=\"0.8\" top=\"0.98\"/>"
				"<top drawing=\"vise-top.svg\"/><sides color=\"#8D9193\"><face side=\"front back\" drawing=\"vise-front.svg\"/>"
				"<face side=\"left right\" drawing=\"vise-end.svg\"/></sides></part>"
				"<part name=\"toolbox\"><footprint>" + rect(-0.23F, -0.14F, 0.23F, 0.14F) + "</footprint><height bottom=\"0.8\" top=\"1.0\"/>"
				"<top drawing=\"toolbox-top.svg\"/><sides color=\"#8B4A37\"><face side=\"front\" drawing=\"toolbox-front.svg\"/>"
				"<face side=\"back\" drawing=\"toolbox-back.svg\"/><face side=\"left right\" drawing=\"toolbox-end.svg\"/></sides></part>" +
					extraParts + "</parts><placements>" + place("carcass") + place("worktop") + place("vise", 1.18F, 0.39F) +
					place("toolbox", -1.09F, -0.28F) + extraPlacements + "</placements>"
				"<useAreas>" + rect(-1.2F, 0.5F, 1.2F, 1.2F) + "</useAreas><clearance>" + rect(-1.5F, 0.5F, 1.5F, 1.1F) + "</clearance>"
			);
		}

		TempAssetFolder folder;
	};

} // namespace assets::parts_fixtures

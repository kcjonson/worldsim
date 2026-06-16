// Construction Registry Implementation
// Loads materials.xml, constraints.xml, and snapping.xml via pugixml.

#include "assets/ConstructionRegistry.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>

namespace engine::assets {

	// ---------------------------------------------------------------------------
	// Singleton
	// ---------------------------------------------------------------------------

	ConstructionRegistry& ConstructionRegistry::Get() {
		static ConstructionRegistry instance;
		return instance;
	}

	// ---------------------------------------------------------------------------
	// Helpers
	// ---------------------------------------------------------------------------

	int64_t ConstructionRegistry::toMm(float meters) {
		return static_cast<int64_t>(std::llround(static_cast<double>(meters) * 1000.0));
	}

	PatternColor ConstructionRegistry::parseColor(const std::string& hex) {
		// Accepts "#RRGGBBAA" (9 chars). Silently returns opaque black on bad input.
		PatternColor c;
		if (hex.size() != 9 || hex[0] != '#') {
			LOG_WARNING(Engine, "Construction: bad color format '%s' (expected #RRGGBBAA)", hex.c_str());
			return c;
		}

		auto parseHexByte = [](const char* s) -> uint8_t {
			char buf[3] = {s[0], s[1], '\0'};
			return static_cast<uint8_t>(std::strtoul(buf, nullptr, 16));
		};

		c.r = parseHexByte(hex.c_str() + 1);
		c.g = parseHexByte(hex.c_str() + 3);
		c.b = parseHexByte(hex.c_str() + 5);
		c.a = parseHexByte(hex.c_str() + 7);
		return c;
	}

	StyleColor ConstructionRegistry::parseStyleColor(const std::string& hex) {
		const PatternColor c = parseColor(hex);
		return StyleColor{c.r / 255.0F, c.g / 255.0F, c.b / 255.0F, c.a / 255.0F};
	}

	// ---------------------------------------------------------------------------
	// Load entry points
	// ---------------------------------------------------------------------------

	bool ConstructionRegistry::load(const std::string& folderPath) {
		namespace fs = std::filesystem;

		if (!fs::exists(folderPath)) {
			LOG_ERROR(Engine, "Construction config folder not found: %s", folderPath.c_str());
			return false;
		}

		bool ok = true;
		ok = loadMaterials((fs::path(folderPath) / "materials.xml").string()) && ok;
		ok = loadConstraints((fs::path(folderPath) / "constraints.xml").string()) && ok;
		ok = loadSnapping((fs::path(folderPath) / "snapping.xml").string()) && ok;
		ok = loadRendering((fs::path(folderPath) / "rendering.xml").string()) && ok;
		return ok;
	}

	bool ConstructionRegistry::loadMaterials(const std::string& xmlPath) {
		pugi::xml_document	   doc;
		pugi::xml_parse_result result = doc.load_file(xmlPath.c_str());

		if (!result) {
			LOG_ERROR(Engine, "Failed to load construction materials XML: %s - %s", xmlPath.c_str(), result.description());
			return false;
		}

		pugi::xml_node root = doc.child("ConstructionMaterials");
		if (!root) {
			LOG_ERROR(Engine, "No ConstructionMaterials root element in: %s", xmlPath.c_str());
			return false;
		}

		// Foundation materials
		pugi::xml_node foundationNode = root.child("Foundation");
		if (!foundationNode) {
			LOG_ERROR(Engine, "No Foundation element in construction materials: %s", xmlPath.c_str());
			return false;
		}

		size_t loaded = 0;
		for (pugi::xml_node matNode : foundationNode.children("Material")) {
			MaterialDef mat;

			mat.name = matNode.attribute("name").as_string();
			if (mat.name.empty()) {
				LOG_WARNING(Engine, "Construction material missing name attribute, skipping");
				continue;
			}

			mat.costRatePerSquareMeter = matNode.child("costRatePerSquareMeter").text().as_float(0.0F);
			mat.workRatePerSquareMeter = matNode.child("workRatePerSquareMeter").text().as_float(0.0F);
			mat.hp = matNode.child("hp").text().as_float(0.0F);
			mat.flammability = matNode.child("flammability").text().as_float(0.0F);
			mat.beauty = matNode.child("beauty").text().as_float(0.0F);
			mat.speedModifier = matNode.child("speedModifier").text().as_float(1.0F);

			// Pattern block
			if (auto patNode = matNode.child("pattern")) {
				mat.pattern.emitter = patNode.child("emitter").text().as_string();
				mat.pattern.seed = static_cast<uint32_t>(patNode.child("seed").text().as_uint(0));

				for (pugi::xml_node colorNode : patNode.child("palette").children("color")) {
					mat.pattern.palette.push_back(parseColor(colorNode.text().as_string()));
				}
			}

			auto [it, inserted] = materials.emplace(mat.name, std::move(mat));
			if (!inserted) {
				LOG_WARNING(Engine, "Duplicate construction material '%s' ignored", it->first.c_str());
				continue;
			}
			++loaded;
		}

		// Wall materials — thickness presets merged into the same MaterialDef.
		// A wall material reuses the per-area cost/work/hp/flammability from its
		// MaterialDef (walls compute work from length×thickness×material), with each
		// preset's multipliers scaling the base rates. A Wall <Material> whose name
		// already appears in the Foundation map has its presets appended; a new name
		// creates a fresh entry with zeroed foundation fields.
		pugi::xml_node wallNode = root.child("Wall");
		if (wallNode) {
			for (pugi::xml_node matNode : wallNode.children("Material")) {
				std::string matName = matNode.attribute("name").as_string();
				if (matName.empty()) {
					LOG_WARNING(Engine, "Wall material missing name attribute, skipping");
					continue;
				}

				// Merge into existing foundation entry or create a new one.
				MaterialDef& mat = materials[matName];
				if (mat.name.empty()) {
					mat.name = matName;
					// Parse per-area rates in case this material has no Foundation block.
					mat.costRatePerSquareMeter = matNode.child("costRatePerSquareMeter").text().as_float(0.0F);
					mat.workRatePerSquareMeter = matNode.child("workRatePerSquareMeter").text().as_float(0.0F);
					mat.hp = matNode.child("hp").text().as_float(0.0F);
					mat.flammability = matNode.child("flammability").text().as_float(0.0F);
					mat.beauty = matNode.child("beauty").text().as_float(0.0F);
					mat.speedModifier = matNode.child("speedModifier").text().as_float(1.0F);

					if (auto patNode = matNode.child("pattern")) {
						mat.pattern.emitter = patNode.child("emitter").text().as_string();
						mat.pattern.seed = static_cast<uint32_t>(patNode.child("seed").text().as_uint(0));
						for (pugi::xml_node colorNode : patNode.child("palette").children("color")) {
							mat.pattern.palette.push_back(parseColor(colorNode.text().as_string()));
						}
					}
					++loaded;
				}

				// Parse <Preset> children.
				for (pugi::xml_node presetNode : matNode.children("Preset")) {
					ThicknessPreset preset;
					preset.name = presetNode.attribute("name").as_string();
					if (preset.name.empty()) {
						LOG_WARNING(Engine, "Wall preset for '%s' missing name attribute, skipping", matName.c_str());
						continue;
					}

					preset.thicknessMeters = presetNode.child("thicknessMeters").text().as_float(0.0F);
					preset.thicknessMm = toMm(preset.thicknessMeters);
					preset.halfThicknessMm = preset.thicknessMm / 2;
					preset.costMultiplier = presetNode.child("costMultiplier").text().as_float(1.0F);
					preset.workMultiplier = presetNode.child("workMultiplier").text().as_float(1.0F);
					preset.hpMultiplier = presetNode.child("hpMultiplier").text().as_float(1.0F);
					preset.insulation = presetNode.child("insulation").text().as_float(0.0F);

					mat.wallThicknesses.push_back(std::move(preset));
				}

				LOG_DEBUG(Engine, "Loaded %zu wall presets for material '%s'", mat.wallThicknesses.size(), matName.c_str());
			}
		}

		// Opening types — doors/windows carved into walls. Loaded from the same
		// materials.xml (parallel to <Foundation> and <Wall>) so loadFromFolder is
		// unchanged. Cost/work are constants per type, not area-derived. material is a
		// config name validated by ConfigValidator against the materials map above.
		pugi::xml_node openingNode = root.child("Opening");
		if (openingNode) {
			for (pugi::xml_node typeNode : openingNode.children("Type")) {
				OpeningTypeDef type;
				type.name = typeNode.attribute("name").as_string();
				if (type.name.empty()) {
					LOG_WARNING(Engine, "Opening type missing name attribute, skipping");
					continue;
				}

				type.material = typeNode.child("material").text().as_string();
				type.widthMeters = typeNode.child("widthMeters").text().as_float(0.0F);
				type.widthMm = toMm(type.widthMeters);
				type.pathable = typeNode.child("pathable").text().as_bool(false);
				type.costItems = typeNode.child("costItems").text().as_float(0.0F);
				type.workUnits = typeNode.child("workUnits").text().as_float(0.0F);

				openingTypeList.push_back(std::move(type));
			}
			LOG_DEBUG(Engine, "Loaded %zu opening types", openingTypeList.size());
		}

		hasMaterials = (loaded > 0);
		LOG_INFO(Engine, "Loaded %zu construction materials from %s", loaded, xmlPath.c_str());
		return hasMaterials;
	}

	bool ConstructionRegistry::loadConstraints(const std::string& xmlPath) {
		pugi::xml_document	   doc;
		pugi::xml_parse_result result = doc.load_file(xmlPath.c_str());

		if (!result) {
			LOG_ERROR(Engine, "Failed to load construction constraints XML: %s - %s", xmlPath.c_str(), result.description());
			return false;
		}

		pugi::xml_node root = doc.child("ConstructionConstraints");
		if (!root) {
			LOG_ERROR(Engine, "No ConstructionConstraints root element in: %s", xmlPath.c_str());
			return false;
		}

		ConstraintConfig cfg;

		cfg.pathingClearanceMeters = root.child("pathingClearanceMeters").text().as_float(0.7F);
		cfg.pathingClearanceMm = toMm(cfg.pathingClearanceMeters);

		cfg.minCornerAngleDegrees = root.child("minCornerAngleDegrees").text().as_float(30.0F);

		cfg.minVertexSpacingMeters = root.child("minVertexSpacingMeters").text().as_float(0.5F);
		cfg.minVertexSpacingMm = toMm(cfg.minVertexSpacingMeters);

		cfg.segmentClearanceMeters = root.child("segmentClearanceMeters").text().as_float(1.0F);
		cfg.segmentClearanceMm = toMm(cfg.segmentClearanceMeters);

		cfg.minAreaSquareMeters = root.child("minAreaSquareMeters").text().as_float(4.0F);
		cfg.maxAreaSquareMeters = root.child("maxAreaSquareMeters").text().as_float(2500.0F);

		cfg.maxPoints = root.child("maxPoints").text().as_int(32);

		cfg.openingMarginMeters = root.child("openingMarginMeters").text().as_float(0.3F);
		cfg.openingMarginMm = toMm(cfg.openingMarginMeters);

		cfg.refundPercent = root.child("refundPercent").text().as_float(50.0F);

		cfg.builderCapBase = root.child("builderCapBase").text().as_int(1);
		cfg.builderCapPerSquareMeter = root.child("builderCapPerSquareMeter").text().as_float(0.1F);

		// Wall constraints
		cfg.minSegmentLengthMeters = root.child("minSegmentLengthMeters").text().as_float(0.5F);
		cfg.minSegmentLengthMm = toMm(cfg.minSegmentLengthMeters);

		cfg.minWallJunctionAngleDegrees = root.child("minWallJunctionAngleDegrees").text().as_float(30.0F);

		cfg.minParallelClearanceMeters = root.child("minParallelClearanceMeters").text().as_float(0.8F);
		cfg.minParallelClearanceMm = toMm(cfg.minParallelClearanceMeters);

		constraintConfig = cfg;
		hasConstraints = true;

		LOG_INFO(Engine, "Loaded construction constraints from %s", xmlPath.c_str());
		return true;
	}

	bool ConstructionRegistry::loadSnapping(const std::string& xmlPath) {
		pugi::xml_document	   doc;
		pugi::xml_parse_result result = doc.load_file(xmlPath.c_str());

		if (!result) {
			LOG_ERROR(Engine, "Failed to load construction snapping XML: %s - %s", xmlPath.c_str(), result.description());
			return false;
		}

		pugi::xml_node root = doc.child("ConstructionSnapping");
		if (!root) {
			LOG_ERROR(Engine, "No ConstructionSnapping root element in: %s", xmlPath.c_str());
			return false;
		}

		SnappingConfig cfg;

		cfg.angleIncrementDegrees = root.child("angleIncrementDegrees").text().as_float(15.0F);

		cfg.vertexSnapRadiusMeters = root.child("vertexSnapRadiusMeters").text().as_float(0.4F);
		cfg.vertexSnapRadiusMm = toMm(cfg.vertexSnapRadiusMeters);

		cfg.edgeSnapRadiusMeters = root.child("edgeSnapRadiusMeters").text().as_float(0.3F);
		cfg.edgeSnapRadiusMm = toMm(cfg.edgeSnapRadiusMeters);

		cfg.smartGuideRangeMeters = root.child("smartGuideRangeMeters").text().as_float(8.0F);
		cfg.smartGuideRangeMm = toMm(cfg.smartGuideRangeMeters);

		cfg.originCloseRadiusMeters = root.child("originCloseRadiusMeters").text().as_float(0.5F);
		cfg.originCloseRadiusMm = toMm(cfg.originCloseRadiusMeters);

		snappingConfig = cfg;
		hasSnapping = true;

		LOG_INFO(Engine, "Loaded construction snapping config from %s", xmlPath.c_str());
		return true;
	}

	bool ConstructionRegistry::loadRendering(const std::string& xmlPath) {
		RenderingConfig cfg; // built-in defaults; the file overlays whatever it provides

		pugi::xml_document	   doc;
		pugi::xml_parse_result result = doc.load_file(xmlPath.c_str());
		if (!result) {
			// Tolerant: a missing/unreadable file keeps the defaults, which are a
			// complete valid style, so the renderer never loses its look.
			LOG_WARNING(
				Engine, "Construction rendering XML not loaded (%s): %s - using built-in defaults", xmlPath.c_str(), result.description()
			);
			renderingConfig = cfg;
			hasRendering = true;
			return true;
		}

		pugi::xml_node root = doc.child("ConstructionRendering");
		if (!root) {
			LOG_ERROR(Engine, "No ConstructionRendering root element in: %s", xmlPath.c_str());
			return false;
		}

		// Each field is optional: a missing element keeps the struct's default.
		auto color = [](pugi::xml_node parent, const char* tag, StyleColor def) -> StyleColor {
			pugi::xml_node node = parent.child(tag);
			if (!node) {
				return def;
			}
			const std::string hex = node.text().as_string();
			if (hex.empty()) {
				return def;
			}
			return ConstructionRegistry::parseStyleColor(hex);
		};
		auto number = [](pugi::xml_node parent, const char* tag, float def) -> float {
			pugi::xml_node node = parent.child(tag);
			return node ? node.text().as_float(def) : def;
		};

		if (pugi::xml_node f = root.child("Foundation")) {
			cfg.foundation.blueprintFill = color(f, "blueprintFill", cfg.foundation.blueprintFill);
			cfg.foundation.fallbackColor = color(f, "fallbackColor", cfg.foundation.fallbackColor);
			cfg.foundation.outlineColor = color(f, "outlineColor", cfg.foundation.outlineColor);
			cfg.foundation.progressAlphaMin = number(f, "progressAlphaMin", cfg.foundation.progressAlphaMin);
			cfg.foundation.progressAlphaMax = number(f, "progressAlphaMax", cfg.foundation.progressAlphaMax);
			cfg.foundation.outlineAlphaMin = number(f, "outlineAlphaMin", cfg.foundation.outlineAlphaMin);
			cfg.foundation.outlineAlphaMax = number(f, "outlineAlphaMax", cfg.foundation.outlineAlphaMax);
			cfg.foundation.outlineWidthBlueprint = number(f, "outlineWidthBlueprint", cfg.foundation.outlineWidthBlueprint);
			cfg.foundation.outlineWidthBuilt = number(f, "outlineWidthBuilt", cfg.foundation.outlineWidthBuilt);
		}
		if (pugi::xml_node w = root.child("Wall")) {
			cfg.wall.blueprintFill = color(w, "blueprintFill", cfg.wall.blueprintFill);
			cfg.wall.fallbackColor = color(w, "fallbackColor", cfg.wall.fallbackColor);
			cfg.wall.outlineColor = color(w, "outlineColor", cfg.wall.outlineColor);
			cfg.wall.junctionColor = color(w, "junctionColor", cfg.wall.junctionColor);
			cfg.wall.progressAlphaMin = number(w, "progressAlphaMin", cfg.wall.progressAlphaMin);
			cfg.wall.progressAlphaMax = number(w, "progressAlphaMax", cfg.wall.progressAlphaMax);
			cfg.wall.outlineAlphaMin = number(w, "outlineAlphaMin", cfg.wall.outlineAlphaMin);
			cfg.wall.outlineAlphaMax = number(w, "outlineAlphaMax", cfg.wall.outlineAlphaMax);
			cfg.wall.outlineWidthBlueprint = number(w, "outlineWidthBlueprint", cfg.wall.outlineWidthBlueprint);
			cfg.wall.outlineWidthBuilt = number(w, "outlineWidthBuilt", cfg.wall.outlineWidthBuilt);
			cfg.wall.junctionAlphaBlueprint = number(w, "junctionAlphaBlueprint", cfg.wall.junctionAlphaBlueprint);
			cfg.wall.junctionAlphaBuilt = number(w, "junctionAlphaBuilt", cfg.wall.junctionAlphaBuilt);
		}
		if (pugi::xml_node o = root.child("Opening")) {
			cfg.opening.doorFallbackColor = color(o, "doorFallbackColor", cfg.opening.doorFallbackColor);
			cfg.opening.glassColor = color(o, "glassColor", cfg.opening.glassColor);
			cfg.opening.fillAlpha = number(o, "fillAlpha", cfg.opening.fillAlpha);
			cfg.opening.outlineAlpha = number(o, "outlineAlpha", cfg.opening.outlineAlpha);
			cfg.opening.outlineDarken = number(o, "outlineDarken", cfg.opening.outlineDarken);
			cfg.opening.outlineWidthBuilt = number(o, "outlineWidthBuilt", cfg.opening.outlineWidthBuilt);
			cfg.opening.outlineWidthBlueprint = number(o, "outlineWidthBlueprint", cfg.opening.outlineWidthBlueprint);
			cfg.opening.jambWidth = number(o, "jambWidth", cfg.opening.jambWidth);
			cfg.opening.jambDarken = number(o, "jambDarken", cfg.opening.jambDarken);
			cfg.opening.jambAlpha = number(o, "jambAlpha", cfg.opening.jambAlpha);
			cfg.opening.glassInset = number(o, "glassInset", cfg.opening.glassInset);
			cfg.opening.mullionSpacingMeters = number(o, "mullionSpacingMeters", cfg.opening.mullionSpacingMeters);
			cfg.opening.mullionAlpha = number(o, "mullionAlpha", cfg.opening.mullionAlpha);
			cfg.opening.progressAlphaMin = number(o, "progressAlphaMin", cfg.opening.progressAlphaMin);
			cfg.opening.progressAlphaMax = number(o, "progressAlphaMax", cfg.opening.progressAlphaMax);
			cfg.opening.ghostAlpha = number(o, "ghostAlpha", cfg.opening.ghostAlpha);
		}
		if (pugi::xml_node p = root.child("Preview")) {
			cfg.preview.guideColor = color(p, "guideColor", cfg.preview.guideColor);
			cfg.preview.originHaloColor = color(p, "originHaloColor", cfg.preview.originHaloColor);
			cfg.preview.snapVertexColor = color(p, "snapVertexColor", cfg.preview.snapVertexColor);
			cfg.preview.snapEdgeColor = color(p, "snapEdgeColor", cfg.preview.snapEdgeColor);
			cfg.preview.fillPreviewAlpha = number(p, "fillPreviewAlpha", cfg.preview.fillPreviewAlpha);
			cfg.preview.bandPreviewAlpha = number(p, "bandPreviewAlpha", cfg.preview.bandPreviewAlpha);
			cfg.preview.lineWidth = number(p, "lineWidth", cfg.preview.lineWidth);
			cfg.preview.guideWidth = number(p, "guideWidth", cfg.preview.guideWidth);
			cfg.preview.vertexRadiusPx = number(p, "vertexRadiusPx", cfg.preview.vertexRadiusPx);
			cfg.preview.invalidVertexRadiusPx = number(p, "invalidVertexRadiusPx", cfg.preview.invalidVertexRadiusPx);
			cfg.preview.originHaloMinRadiusPx = number(p, "originHaloMinRadiusPx", cfg.preview.originHaloMinRadiusPx);
		}

		renderingConfig = cfg;
		hasRendering = true;
		LOG_INFO(Engine, "Loaded construction rendering style from %s", xmlPath.c_str());
		return true;
	}

	// ---------------------------------------------------------------------------
	// Queries
	// ---------------------------------------------------------------------------

	void ConstructionRegistry::clear() {
		materials.clear();
		openingTypeList.clear();
		constraintConfig = ConstraintConfig{};
		snappingConfig = SnappingConfig{};
		renderingConfig = RenderingConfig{};
		hasMaterials = false;
		hasConstraints = false;
		hasSnapping = false;
		hasRendering = false;
	}

	const MaterialDef* ConstructionRegistry::getMaterial(const std::string& name) const {
		auto it = materials.find(name);
		if (it != materials.end()) {
			return &it->second;
		}
		return nullptr;
	}

	const std::unordered_map<std::string, MaterialDef>& ConstructionRegistry::getAllMaterials() const {
		return materials;
	}

	const MaterialDef* ConstructionRegistry::getWallMaterial(const std::string& name) const {
		auto it = materials.find(name);
		if (it != materials.end() && !it->second.wallThicknesses.empty()) {
			return &it->second;
		}
		return nullptr;
	}

	const ThicknessPreset* ConstructionRegistry::getThicknessPreset(const std::string& materialName, const std::string& presetName) const {
		const MaterialDef* mat = getMaterial(materialName);
		if (!mat) {
			return nullptr;
		}
		for (const auto& preset : mat->wallThicknesses) {
			if (preset.name == presetName) {
				return &preset;
			}
		}
		return nullptr;
	}

	const OpeningTypeDef* ConstructionRegistry::getOpeningType(const std::string& name) const {
		for (const auto& type : openingTypeList) {
			if (type.name == name) {
				return &type;
			}
		}
		return nullptr;
	}

	const std::vector<OpeningTypeDef>& ConstructionRegistry::openingTypes() const {
		return openingTypeList;
	}

	const ConstraintConfig& ConstructionRegistry::constraints() const {
		return constraintConfig;
	}

	const SnappingConfig& ConstructionRegistry::snapping() const {
		return snappingConfig;
	}

	const RenderingConfig& ConstructionRegistry::rendering() const {
		return renderingConfig;
	}

	bool ConstructionRegistry::materialsLoaded() const {
		return hasMaterials;
	}
	bool ConstructionRegistry::constraintsLoaded() const {
		return hasConstraints;
	}
	bool ConstructionRegistry::snappingLoaded() const {
		return hasSnapping;
	}
	bool ConstructionRegistry::renderingLoaded() const {
		return hasRendering;
	}

} // namespace engine::assets

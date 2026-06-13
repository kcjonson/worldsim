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
    return ok;
}

bool ConstructionRegistry::loadMaterials(const std::string& xmlPath) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(xmlPath.c_str());

    if (!result) {
        LOG_ERROR(Engine, "Failed to load construction materials XML: %s - %s",
                  xmlPath.c_str(), result.description());
        return false;
    }

    pugi::xml_node root = doc.child("ConstructionMaterials");
    if (!root) {
        LOG_ERROR(Engine, "No ConstructionMaterials root element in: %s", xmlPath.c_str());
        return false;
    }

    // Only Foundation materials for now; wall/opening sections can be added here later.
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
        mat.hp                     = matNode.child("hp").text().as_float(0.0F);
        mat.flammability           = matNode.child("flammability").text().as_float(0.0F);
        mat.beauty                 = matNode.child("beauty").text().as_float(0.0F);
        mat.speedModifier          = matNode.child("speedModifier").text().as_float(1.0F);

        // Pattern block
        if (auto patNode = matNode.child("pattern")) {
            mat.pattern.emitter = patNode.child("emitter").text().as_string();
            mat.pattern.seed    = static_cast<uint32_t>(patNode.child("seed").text().as_uint(0));

            for (pugi::xml_node colorNode : patNode.child("palette").children("color")) {
                mat.pattern.palette.push_back(parseColor(colorNode.text().as_string()));
            }
        }

        materials.emplace(mat.name, std::move(mat));
        ++loaded;
    }

    hasMaterials = (loaded > 0);
    LOG_INFO(Engine, "Loaded %zu construction materials from %s", loaded, xmlPath.c_str());
    return hasMaterials;
}

bool ConstructionRegistry::loadConstraints(const std::string& xmlPath) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(xmlPath.c_str());

    if (!result) {
        LOG_ERROR(Engine, "Failed to load construction constraints XML: %s - %s",
                  xmlPath.c_str(), result.description());
        return false;
    }

    pugi::xml_node root = doc.child("ConstructionConstraints");
    if (!root) {
        LOG_ERROR(Engine, "No ConstructionConstraints root element in: %s", xmlPath.c_str());
        return false;
    }

    ConstraintConfig cfg;

    cfg.pathingClearanceMeters = root.child("pathingClearanceMeters").text().as_float(0.7F);
    cfg.pathingClearanceMm     = toMm(cfg.pathingClearanceMeters);

    cfg.minCornerAngleDegrees = root.child("minCornerAngleDegrees").text().as_float(30.0F);

    cfg.minVertexSpacingMeters = root.child("minVertexSpacingMeters").text().as_float(0.5F);
    cfg.minVertexSpacingMm     = toMm(cfg.minVertexSpacingMeters);

    cfg.segmentClearanceMeters = root.child("segmentClearanceMeters").text().as_float(1.0F);
    cfg.segmentClearanceMm     = toMm(cfg.segmentClearanceMeters);

    cfg.minAreaSquareMeters = root.child("minAreaSquareMeters").text().as_float(4.0F);
    cfg.maxAreaSquareMeters = root.child("maxAreaSquareMeters").text().as_float(2500.0F);

    cfg.maxPoints = root.child("maxPoints").text().as_int(32);

    cfg.openingMarginMeters = root.child("openingMarginMeters").text().as_float(0.3F);
    cfg.openingMarginMm     = toMm(cfg.openingMarginMeters);

    cfg.refundPercent = root.child("refundPercent").text().as_float(50.0F);

    cfg.builderCapBase            = root.child("builderCapBase").text().as_int(1);
    cfg.builderCapPerSquareMeter  = root.child("builderCapPerSquareMeter").text().as_float(0.1F);

    constraintConfig = cfg;
    hasConstraints = true;

    LOG_INFO(Engine, "Loaded construction constraints from %s", xmlPath.c_str());
    return true;
}

bool ConstructionRegistry::loadSnapping(const std::string& xmlPath) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(xmlPath.c_str());

    if (!result) {
        LOG_ERROR(Engine, "Failed to load construction snapping XML: %s - %s",
                  xmlPath.c_str(), result.description());
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
    cfg.vertexSnapRadiusMm     = toMm(cfg.vertexSnapRadiusMeters);

    cfg.edgeSnapRadiusMeters = root.child("edgeSnapRadiusMeters").text().as_float(0.3F);
    cfg.edgeSnapRadiusMm     = toMm(cfg.edgeSnapRadiusMeters);

    cfg.smartGuideRangeMeters = root.child("smartGuideRangeMeters").text().as_float(8.0F);
    cfg.smartGuideRangeMm     = toMm(cfg.smartGuideRangeMeters);

    cfg.originCloseRadiusMeters = root.child("originCloseRadiusMeters").text().as_float(0.5F);
    cfg.originCloseRadiusMm     = toMm(cfg.originCloseRadiusMeters);

    snappingConfig = cfg;
    hasSnapping = true;

    LOG_INFO(Engine, "Loaded construction snapping config from %s", xmlPath.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

void ConstructionRegistry::clear() {
    materials.clear();
    constraintConfig = ConstraintConfig{};
    snappingConfig = SnappingConfig{};
    hasMaterials = false;
    hasConstraints = false;
    hasSnapping = false;
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

const ConstraintConfig& ConstructionRegistry::constraints() const {
    return constraintConfig;
}

const SnappingConfig& ConstructionRegistry::snapping() const {
    return snappingConfig;
}

bool ConstructionRegistry::materialsLoaded() const { return hasMaterials; }
bool ConstructionRegistry::constraintsLoaded() const { return hasConstraints; }
bool ConstructionRegistry::snappingLoaded() const { return hasSnapping; }

} // namespace engine::assets

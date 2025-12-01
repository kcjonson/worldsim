// Asset Registry Implementation
// Handles XML parsing with pugixml and asset generation.

#include "assets/AssetRegistry.h"

#include <utils/Log.h>
#include <vector/Tessellator.h>

#include <pugixml.hpp>

#include <cstdlib>
#include <sstream>

namespace engine::assets {

// ============================================================================
// GeneratorParams Implementation
// ============================================================================

std::string GeneratorParams::getString(const char* key, const std::string& defaultVal) const {
	auto it = params.find(key);
	if (it != params.end()) {
		return it->second;
	}
	return defaultVal;
}

float GeneratorParams::getFloat(const char* key, float defaultVal) const {
	auto it = params.find(key);
	if (it != params.end()) {
		try {
			return std::stof(it->second);
		} catch (...) {
			return defaultVal;
		}
	}
	return defaultVal;
}

void GeneratorParams::getFloatRange(const char* key, float& outMin, float& outMax, float defaultMin, float defaultMax) const {
	outMin = defaultMin;
	outMax = defaultMax;

	auto it = params.find(key);
	if (it == params.end()) {
		return;
	}

	const auto& str = it->second;
	auto		commaPos = str.find(',');
	if (commaPos == std::string::npos) {
		// Single value - use for both min and max
		try {
			float val = std::stof(str);
			outMin = val;
			outMax = val;
		} catch (...) {
			// Keep defaults
		}
		return;
	}

	// Parse min,max format
	try {
		outMin = std::stof(str.substr(0, commaPos));
		outMax = std::stof(str.substr(commaPos + 1));
	} catch (...) {
		outMin = defaultMin;
		outMax = defaultMax;
	}
}

int32_t GeneratorParams::getInt(const char* key, int32_t defaultVal) const {
	auto it = params.find(key);
	if (it != params.end()) {
		try {
			return std::stoi(it->second);
		} catch (...) {
			return defaultVal;
		}
	}
	return defaultVal;
}

void GeneratorParams::setString(const char* key, const std::string& value) {
	params[key] = value;
}

void GeneratorParams::setFloat(const char* key, float value) {
	params[key] = std::to_string(value);
}

bool GeneratorParams::has(const char* key) const {
	return params.find(key) != params.end();
}

// ============================================================================
// GeneratorRegistry Implementation
// ============================================================================

GeneratorRegistry& GeneratorRegistry::Get() {
	static GeneratorRegistry instance;
	return instance;
}

void GeneratorRegistry::registerGenerator(const char* name, GeneratorFactory factory) {
	factories[name] = std::move(factory);
	LOG_DEBUG(Engine, "Registered generator: %s", name);
}

std::unique_ptr<IAssetGenerator> GeneratorRegistry::create(const char* name) {
	auto it = factories.find(name);
	if (it != factories.end()) {
		return it->second();
	}
	LOG_WARNING(Engine, "Generator not found: %s", name);
	return nullptr;
}

bool GeneratorRegistry::hasGenerator(const char* name) const {
	return factories.find(name) != factories.end();
}

// ============================================================================
// AssetRegistry Implementation
// ============================================================================

AssetRegistry& AssetRegistry::Get() {
	static AssetRegistry instance;
	return instance;
}

namespace {

AssetType parseAssetType(const char* str) {
	if (str == nullptr) {
		return AssetType::Procedural;
	}
	std::string s(str);
	if (s == "simple" || s == "Simple") {
		return AssetType::Simple;
	}
	return AssetType::Procedural;
}

AssetComplexity parseComplexity(const char* str) {
	if (str == nullptr) {
		return AssetComplexity::Simple;
	}
	std::string s(str);
	if (s == "complex" || s == "Complex") {
		return AssetComplexity::Complex;
	}
	return AssetComplexity::Simple;
}

RenderingTier parseRenderingTier(const char* str) {
	if (str == nullptr) {
		return RenderingTier::Instanced;
	}
	std::string s(str);
	if (s == "batched" || s == "Batched") {
		return RenderingTier::Batched;
	}
	if (s == "individual" || s == "Individual") {
		return RenderingTier::Individual;
	}
	return RenderingTier::Instanced;
}

AnimationType parseAnimationType(const char* str) {
	if (str == nullptr) {
		return AnimationType::None;
	}
	std::string s(str);
	if (s == "parametric" || s == "Parametric") {
		return AnimationType::Parametric;
	}
	if (s == "bezier" || s == "BezierDeform") {
		return AnimationType::BezierDeform;
	}
	return AnimationType::None;
}

Distribution parseDistribution(const char* str) {
	if (str == nullptr) {
		return Distribution::Uniform;
	}
	std::string s(str);
	if (s == "clumped" || s == "Clumped") {
		return Distribution::Clumped;
	}
	if (s == "spaced" || s == "Spaced") {
		return Distribution::Spaced;
	}
	return Distribution::Uniform;
}

/// Parse "min,max" format into two integers
void parseIntRange(const std::string& str, int32_t& outMin, int32_t& outMax, int32_t defaultMin, int32_t defaultMax) {
	outMin = defaultMin;
	outMax = defaultMax;
	if (str.empty()) {
		return;
	}
	auto commaPos = str.find(',');
	if (commaPos != std::string::npos) {
		try {
			outMin = std::stoi(str.substr(0, commaPos));
			outMax = std::stoi(str.substr(commaPos + 1));
		} catch (...) {
			// Keep defaults
		}
	} else {
		try {
			outMin = std::stoi(str);
			outMax = outMin;
		} catch (...) {
			// Keep defaults
		}
	}
}

/// Parse "min,max" format into two floats
void parseFloatRange(const std::string& str, float& outMin, float& outMax, float defaultMin, float defaultMax) {
	outMin = defaultMin;
	outMax = defaultMax;
	if (str.empty()) {
		return;
	}
	auto commaPos = str.find(',');
	if (commaPos != std::string::npos) {
		try {
			outMin = std::stof(str.substr(0, commaPos));
			outMax = std::stof(str.substr(commaPos + 1));
		} catch (...) {
			// Keep defaults
		}
	} else {
		try {
			outMin = std::stof(str);
			outMax = outMin;
		} catch (...) {
			// Keep defaults
		}
	}
}

}  // namespace

bool AssetRegistry::loadDefinitions(const std::string& xmlPath) {
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(xmlPath.c_str());

	if (!result) {
		LOG_ERROR(Engine, "Failed to load XML: %s - %s", xmlPath.c_str(), result.description());
		return false;
	}

	pugi::xml_node root = doc.child("AssetDefinitions");
	if (!root) {
		LOG_ERROR(Engine, "Missing <AssetDefinitions> root element in %s", xmlPath.c_str());
		return false;
	}

	int loadedCount = 0;
	for (pugi::xml_node defNode : root.children("AssetDef")) {
		AssetDefinition def;

		// Required fields
		def.defName = defNode.child_value("defName");
		if (def.defName.empty()) {
			LOG_WARNING(Engine, "Skipping asset definition with empty defName");
			continue;
		}

		def.label = defNode.child_value("label");
		if (def.label.empty()) {
			def.label = def.defName;
		}

		// Asset type
		def.assetType = parseAssetType(defNode.child_value("assetType"));

		// Generator (for procedural assets)
		pugi::xml_node genNode = defNode.child("generator");
		if (genNode) {
			def.generatorName = genNode.child_value("name");

			// Parse generator parameters
			pugi::xml_node paramsNode = genNode.child("params");
			if (paramsNode) {
				for (pugi::xml_node param : paramsNode.children()) {
					def.params.setString(param.name(), param.child_value());
				}
			}
		}

		// SVG path (for simple assets)
		def.svgPath = defNode.child_value("svgPath");

		// Rendering settings
		pugi::xml_node renderNode = defNode.child("rendering");
		if (renderNode) {
			def.complexity = parseComplexity(renderNode.child_value("complexity"));
			def.renderingTier = parseRenderingTier(renderNode.child_value("tier"));
		}

		// Animation settings
		pugi::xml_node animNode = defNode.child("animation");
		if (animNode) {
			def.animation.enabled = true;
			def.animation.type = parseAnimationType(animNode.child_value("type"));
			def.animation.windResponse = static_cast<float>(animNode.child("windResponse").text().as_double(0.3));

			// Parse sway frequency range
			std::string swayStr = animNode.child_value("swayFrequency");
			if (!swayStr.empty()) {
				auto commaPos = swayStr.find(',');
				if (commaPos != std::string::npos) {
					try {
						def.animation.swayFrequencyMin = std::stof(swayStr.substr(0, commaPos));
						def.animation.swayFrequencyMax = std::stof(swayStr.substr(commaPos + 1));
					} catch (...) {
						// Keep defaults
					}
				} else {
					try {
						def.animation.swayFrequencyMin = std::stof(swayStr);
						def.animation.swayFrequencyMax = def.animation.swayFrequencyMin;
					} catch (...) {
						// Keep defaults
					}
				}
			}
		}

		// Placement settings - per-biome configuration
		pugi::xml_node placementNode = defNode.child("placement");
		if (placementNode) {
			// Parse per-biome placement configs
			for (pugi::xml_node biomeNode : placementNode.children("biome")) {
				BiomePlacement bp;

				// Biome name (required)
				bp.biomeName = biomeNode.attribute("name").as_string();
				if (bp.biomeName.empty()) {
					LOG_WARNING(Engine, "Skipping biome placement with empty name");
					continue;
				}

				// Spawn chance
				bp.spawnChance = static_cast<float>(biomeNode.child("spawnChance").text().as_double(0.3));

				// Distribution type
				bp.distribution = parseDistribution(biomeNode.child_value("distribution"));

				// Clumping parameters (for Distribution::Clumped)
				pugi::xml_node clumpingNode = biomeNode.child("clumping");
				if (clumpingNode) {
					parseIntRange(
						clumpingNode.child_value("clumpSize"),
						bp.clumping.clumpSizeMin,
						bp.clumping.clumpSizeMax,
						3,
						12
					);
					parseFloatRange(
						clumpingNode.child_value("clumpRadius"),
						bp.clumping.clumpRadiusMin,
						bp.clumping.clumpRadiusMax,
						0.5F,
						2.0F
					);
					parseFloatRange(
						clumpingNode.child_value("clumpSpacing"),
						bp.clumping.clumpSpacingMin,
						bp.clumping.clumpSpacingMax,
						3.0F,
						8.0F
					);
				}

				// Spacing parameters (for Distribution::Spaced)
				pugi::xml_node spacingNode = biomeNode.child("spacing");
				if (spacingNode) {
					bp.spacing.minDistance =
						static_cast<float>(spacingNode.child("minDistance").text().as_double(2.0));
				}

				def.placement.biomes.push_back(std::move(bp));
			}
		}

		// Variant count
		def.variantCount = static_cast<uint32_t>(defNode.child("variantCount").text().as_uint(1));

		// Store definition
		definitions[def.defName] = std::move(def);
		loadedCount++;
	}

	LOG_DEBUG(Engine, "Loaded %d asset definitions from %s", loadedCount, xmlPath.c_str());
	return loadedCount > 0;
}

const AssetDefinition* AssetRegistry::getDefinition(const std::string& defName) const {
	auto it = definitions.find(defName);
	if (it != definitions.end()) {
		return &it->second;
	}
	return nullptr;
}

const renderer::TessellatedMesh* AssetRegistry::getTemplate(const std::string& defName) {
	// Check cache first
	auto cacheIt = templateCache.find(defName);
	if (cacheIt != templateCache.end()) {
		return &cacheIt->second;
	}

	// Get definition
	const AssetDefinition* def = getDefinition(defName);
	if (def == nullptr) {
		LOG_ERROR(Engine, "Definition not found: %s", defName.c_str());
		return nullptr;
	}

	// Generate asset
	GeneratedAsset asset;
	if (!generateAsset(defName, 42, asset)) {  // Use fixed seed for template
		LOG_ERROR(Engine, "Failed to generate asset: %s", defName.c_str());
		return nullptr;
	}

	// Tessellate
	renderer::TessellatedMesh mesh;
	if (!tessellateAsset(asset, mesh)) {
		LOG_ERROR(Engine, "Failed to tessellate asset: %s", defName.c_str());
		return nullptr;
	}

	// Cache and return
	templateCache[defName] = std::move(mesh);
	return &templateCache[defName];
}

bool AssetRegistry::generateAsset(const std::string& defName, uint32_t seed, GeneratedAsset& outAsset) {
	const AssetDefinition* def = getDefinition(defName);
	if (def == nullptr) {
		LOG_ERROR(Engine, "Definition not found: %s", defName.c_str());
		return false;
	}

	if (def->assetType != AssetType::Procedural) {
		LOG_ERROR(Engine, "Asset %s is not procedural", defName.c_str());
		return false;
	}

	// Create generator
	auto generator = GeneratorRegistry::Get().create(def->generatorName.c_str());
	if (!generator) {
		LOG_ERROR(Engine, "Generator not found: %s", def->generatorName.c_str());
		return false;
	}

	// Generate
	GenerationContext ctx;
	ctx.seed = seed;
	ctx.variantIndex = 0;

	outAsset.clear();
	return generator->generate(ctx, def->params, outAsset);
}

bool AssetRegistry::tessellateAsset(const GeneratedAsset& asset, renderer::TessellatedMesh& outMesh) {
	outMesh.clear();

	renderer::Tessellator tessellator;

	for (const auto& path : asset.paths) {
		if (path.vertices.size() < 3) {
			continue;
		}

		renderer::VectorPath vectorPath;
		vectorPath.vertices = path.vertices;
		vectorPath.isClosed = path.isClosed;

		renderer::TessellatedMesh pathMesh;
		if (!tessellator.Tessellate(vectorPath, pathMesh)) {
			LOG_WARNING(Engine, "Failed to tessellate path with %zu vertices", path.vertices.size());
			continue;
		}

		// Append to output mesh (with index offset)
		uint16_t baseIndex = static_cast<uint16_t>(outMesh.vertices.size());
		for (const auto& v : pathMesh.vertices) {
			outMesh.vertices.push_back(v);
		}
		for (const auto& idx : pathMesh.indices) {
			outMesh.indices.push_back(baseIndex + idx);
		}
	}

	return !outMesh.vertices.empty();
}

void AssetRegistry::clear() {
	definitions.clear();
	templateCache.clear();
}

std::vector<std::string> AssetRegistry::getDefinitionNames() const {
	std::vector<std::string> names;
	names.reserve(definitions.size());
	for (const auto& [name, _] : definitions) {
		names.push_back(name);
	}
	return names;
}

}  // namespace engine::assets

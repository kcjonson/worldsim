// Asset Registry Implementation
// Handles XML parsing with pugixml and asset generation.

#include "assets/AssetRegistry.h"
#include "assets/lua/LuaGenerator.h"

#include <utils/Log.h>
#include <vector/SVGLoader.h>
#include <vector/Tessellator.h>

#include <pugixml.hpp>

#include <cstdlib>
#include <filesystem>
#include <limits>
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

		RelationshipKind parseRelationshipKind(const char* name) {
			if (name == nullptr) {
				return RelationshipKind::Affinity;
			}
			std::string s(name);
			if (s == "requires") {
				return RelationshipKind::Requires;
			}
			if (s == "affinity") {
				return RelationshipKind::Affinity;
			}
			if (s == "avoids") {
				return RelationshipKind::Avoids;
			}
			return RelationshipKind::Affinity;
		}

		CapabilityQuality parseCapabilityQuality(const char* str) {
			if (str == nullptr) {
				return CapabilityQuality::Normal;
			}
			std::string s(str);
			if (s == "terrible" || s == "Terrible") {
				return CapabilityQuality::Terrible;
			}
			if (s == "poor" || s == "Poor") {
				return CapabilityQuality::Poor;
			}
			if (s == "good" || s == "Good") {
				return CapabilityQuality::Good;
			}
			if (s == "excellent" || s == "Excellent") {
				return CapabilityQuality::Excellent;
			}
			// "normal", "Normal", "clean", "Clean" all map to Normal
			return CapabilityQuality::Normal;
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
			// Ensure min <= max for std::uniform_int_distribution
			if (outMin > outMax) {
				std::swap(outMin, outMax);
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
			// Ensure min <= max for std::uniform_real_distribution
			if (outMin > outMax) {
				std::swap(outMin, outMax);
			}
		}

	} // namespace

	void AssetRegistry::setSharedScriptsPath(const std::filesystem::path& path) {
		m_sharedScriptsPath = path;
		LOG_DEBUG(Engine, "Set shared scripts path: %s", path.string().c_str());
	}

	bool AssetRegistry::loadDefinitions(const std::string& xmlPath) {
		namespace fs = std::filesystem;

		pugi::xml_document	   doc;
		pugi::xml_parse_result result = doc.load_file(xmlPath.c_str());

		// Extract base folder from XML path for relative path resolution
		// Use fs::absolute() to ensure baseFolder is always an absolute path
		fs::path baseFolder = fs::absolute(xmlPath).parent_path();

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
				def.scriptPath = genNode.child_value("scriptPath");

				// Parse generator parameters
				pugi::xml_node paramsNode = genNode.child("params");
				if (paramsNode) {
					for (pugi::xml_node param : paramsNode.children()) {
						def.params.setString(param.name(), param.child_value());
					}
				}
			}

			// SVG path and world height (for simple assets)
			def.svgPath = defNode.child_value("svgPath");
			def.worldHeight = static_cast<float>(defNode.child("worldHeight").text().as_double(1.0));

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
						parseIntRange(clumpingNode.child_value("clumpSize"), bp.clumping.clumpSizeMin, bp.clumping.clumpSizeMax, 3, 12);
						parseFloatRange(
							clumpingNode.child_value("clumpRadius"), bp.clumping.clumpRadiusMin, bp.clumping.clumpRadiusMax, 0.5F, 2.0F
						);
						parseFloatRange(
							clumpingNode.child_value("clumpSpacing"), bp.clumping.clumpSpacingMin, bp.clumping.clumpSpacingMax, 3.0F, 8.0F
						);
					}

					// Spacing parameters (for Distribution::Spaced)
					pugi::xml_node spacingNode = biomeNode.child("spacing");
					if (spacingNode) {
						bp.spacing.minDistance = static_cast<float>(spacingNode.child("minDistance").text().as_double(2.0));
					}

					// Tile-type proximity (e.g., <biome name="Wetland" near="Water" distance="2">)
					bp.nearTileType = biomeNode.attribute("near").as_string();
					bp.nearDistance = biomeNode.attribute("distance").as_float(0.0F);

					def.placement.biomes.push_back(std::move(bp));
				}

				// Parse groups (self-declared group membership)
				pugi::xml_node groupsNode = placementNode.child("groups");
				if (groupsNode) {
					for (pugi::xml_node groupNode : groupsNode.children("group")) {
						const char* groupName = groupNode.text().as_string();
						if (groupName != nullptr && groupName[0] != '\0') {
							def.placement.groups.push_back(groupName);
						}
					}
				}

				// Parse relationships (entity-to-entity spawn rules)
				pugi::xml_node relationshipsNode = placementNode.child("relationships");
				if (relationshipsNode) {
					for (pugi::xml_node relNode : relationshipsNode.children()) {
						PlacementRelationship rel;
						rel.kind = parseRelationshipKind(relNode.name());
						rel.distance = relNode.attribute("distance").as_float(5.0F);

						// Parse target (defName, group, or type="same")
						if (relNode.attribute("defName")) {
							rel.target.type = EntityRef::Type::DefName;
							rel.target.value = relNode.attribute("defName").as_string();
						} else if (relNode.attribute("group")) {
							rel.target.type = EntityRef::Type::Group;
							rel.target.value = relNode.attribute("group").as_string();
						} else if (std::string(relNode.attribute("type").as_string()) == "same") {
							rel.target.type = EntityRef::Type::Same;
						}

						// Kind-specific attributes
						if (rel.kind == RelationshipKind::Affinity) {
							rel.strength = relNode.attribute("strength").as_float(1.5F);
						} else if (rel.kind == RelationshipKind::Avoids) {
							rel.penalty = relNode.attribute("penalty").as_float(0.5F);
						} else if (rel.kind == RelationshipKind::Requires) {
							std::string effect = relNode.attribute("effect").as_string();
							rel.required = (effect == "required");
						}

						def.placement.relationships.push_back(rel);
					}
				}
			}

			// Variant count
			def.variantCount = static_cast<uint32_t>(defNode.child("variantCount").text().as_uint(1));

			// Item category (for storage matching and UI grouping)
			std::string categoryStr = defNode.child_value("category");
			if (!categoryStr.empty()) {
				if (categoryStr == "RawMaterial") {
					def.category = ItemCategory::RawMaterial;
				} else if (categoryStr == "Food") {
					def.category = ItemCategory::Food;
				} else if (categoryStr == "Tool") {
					def.category = ItemCategory::Tool;
				} else if (categoryStr == "Furniture") {
					def.category = ItemCategory::Furniture;
				} else {
					LOG_WARNING(Engine, "Unknown item category '%s' in %s", categoryStr.c_str(), def.defName.c_str());
				}
			}

			// Hands required to carry (default 1, use 2 for large items like furniture)
			def.handsRequired = static_cast<uint8_t>(defNode.child("handsRequired").text().as_uint(1));

			// Item properties (for entities that can be carried/stored)
			pugi::xml_node itemNode = defNode.child("item");
			if (itemNode) {
				ItemProperties itemProps;
				itemProps.stackSize = itemNode.child("stackSize").text().as_uint(1);

				// Parse edible capability within item
				pugi::xml_node edibleNode = itemNode.child("edible");
				if (edibleNode) {
					EdibleCapability edible;
					edible.nutrition = edibleNode.attribute("nutrition").as_float(0.3F);
					edible.quality = parseCapabilityQuality(edibleNode.attribute("quality").as_string());
					edible.spoilable = edibleNode.attribute("spoilable").as_bool(false);
					itemProps.edible = edible;
				}

				def.itemProperties = itemProps;
			}

			// Capabilities - what actions can be performed on/with this entity
			pugi::xml_node capabilitiesNode = defNode.child("capabilities");
			if (capabilitiesNode) {
				// Edible capability
				pugi::xml_node edibleNode = capabilitiesNode.child("edible");
				if (edibleNode) {
					EdibleCapability edible;
					edible.nutrition = edibleNode.attribute("nutrition").as_float(0.3F);
					edible.quality = parseCapabilityQuality(edibleNode.attribute("quality").as_string());
					edible.spoilable = edibleNode.attribute("spoilable").as_bool(false);
					def.capabilities.edible = edible;
				}

				// Drinkable capability
				pugi::xml_node drinkableNode = capabilitiesNode.child("drinkable");
				if (drinkableNode) {
					DrinkableCapability drinkable;
					drinkable.quality = parseCapabilityQuality(drinkableNode.attribute("quality").as_string());
					def.capabilities.drinkable = drinkable;
				}

				// Sleepable capability
				pugi::xml_node sleepableNode = capabilitiesNode.child("sleepable");
				if (sleepableNode) {
					SleepableCapability sleepable;
					sleepable.quality = parseCapabilityQuality(sleepableNode.attribute("quality").as_string());
					sleepable.recoveryMultiplier = sleepableNode.attribute("recoveryMultiplier").as_float(1.0F);
					def.capabilities.sleepable = sleepable;
				}

				// Toilet capability
				pugi::xml_node toiletNode = capabilitiesNode.child("toilet");
				if (toiletNode) {
					ToiletCapability toilet;
					toilet.hygieneBonus = toiletNode.attribute("hygieneBonus").as_bool(false);
					def.capabilities.toilet = toilet;
				}

				// Waste capability (marker for bio piles)
				pugi::xml_node wasteNode = capabilitiesNode.child("waste");
				if (wasteNode) {
					def.capabilities.waste = WasteCapability{};
				}

				// Carryable capability (ground items like stones)
				// In unified model, the entity's defName IS the item - no separate itemDefName needed
				pugi::xml_node carryableNode = capabilitiesNode.child("carryable");
				if (carryableNode) {
					CarryableCapability carryable;
					carryable.quantity = carryableNode.attribute("quantity").as_uint(1);
					def.capabilities.carryable = carryable;
				}

				// Harvestable capability (bushes, plants that yield resources)
				pugi::xml_node harvestableNode = capabilitiesNode.child("harvestable");
				if (harvestableNode) {
					std::string yieldName = harvestableNode.attribute("yield").as_string("");
					if (yieldName.empty()) {
						LOG_WARNING(
							Engine,
							"AssetDef '%s' has <harvestable> without valid 'yield' attribute; skipping capability",
							def.defName.c_str()
						);
					} else {
						HarvestableCapability harvestable;
						harvestable.yieldDefName = yieldName;
						harvestable.amountMin = harvestableNode.attribute("amountMin").as_uint(1);
						harvestable.amountMax = harvestableNode.attribute("amountMax").as_uint(3);

						// Validate amountMax >= amountMin (swap if reversed to avoid invalid distribution)
						if (harvestable.amountMax < harvestable.amountMin) {
							LOG_WARNING(
								Engine,
								"AssetDef '%s' harvestable: amountMax (%u) < amountMin (%u); swapping values",
								def.defName.c_str(),
								harvestable.amountMax,
								harvestable.amountMin
							);
							std::swap(harvestable.amountMin, harvestable.amountMax);
						}

						harvestable.duration = harvestableNode.attribute("duration").as_float(4.0F);
						harvestable.destructive = harvestableNode.attribute("destructive").as_bool(true);
						harvestable.regrowthTime = harvestableNode.attribute("regrowthTime").as_float(0.0F);
						harvestable.totalResourceMin = harvestableNode.attribute("totalResourceMin").as_uint(0);
						harvestable.totalResourceMax = harvestableNode.attribute("totalResourceMax").as_uint(0);

						// Validate totalResourceMax >= totalResourceMin
						if (harvestable.totalResourceMax < harvestable.totalResourceMin) {
							LOG_WARNING(
								Engine,
								"AssetDef '%s' harvestable: totalResourceMax (%u) < totalResourceMin (%u); swapping",
								def.defName.c_str(),
								harvestable.totalResourceMax,
								harvestable.totalResourceMin
							);
							std::swap(harvestable.totalResourceMin, harvestable.totalResourceMax);
						}

						def.capabilities.harvestable = harvestable;
					}
				}

				// Craftable capability (crafting stations)
				pugi::xml_node craftableNode = capabilitiesNode.child("craftable");
				if (craftableNode) {
					def.capabilities.craftable = CraftableCapability{};
				}

				// Storage capability (containers)
				pugi::xml_node storageNode = capabilitiesNode.child("storage");
				if (storageNode) {
					StorageCapability storage;
					storage.maxCapacity = storageNode.child("capacity").text().as_uint(50);
					storage.maxStackSize = storageNode.child("stackSize").text().as_uint(999);

					// Parse accepted categories
					pugi::xml_node acceptsNode = storageNode.child("acceptsCategories");
					if (acceptsNode) {
						for (pugi::xml_node catNode : acceptsNode.children("category")) {
							std::string catName = catNode.text().as_string();
							if (catName == "RawMaterial") {
								storage.acceptedCategories.push_back(ItemCategory::RawMaterial);
							} else if (catName == "Food") {
								storage.acceptedCategories.push_back(ItemCategory::Food);
							} else if (catName == "Tool") {
								storage.acceptedCategories.push_back(ItemCategory::Tool);
							} else if (catName == "Furniture") {
								storage.acceptedCategories.push_back(ItemCategory::Furniture);
							} else if (!catName.empty()) {
								LOG_WARNING(Engine, "Unknown storage category '%s' in %s", catName.c_str(), def.defName.c_str());
							}
						}
					}
					def.capabilities.storage = storage;
				}
			}

			// Store base folder for relative path resolution
			def.baseFolder = baseFolder;

			// Store definition
			definitions[def.defName] = std::move(def);
			loadedCount++;
		}

		LOG_DEBUG(Engine, "Loaded %d asset definitions from %s", loadedCount, xmlPath.c_str());
		return loadedCount > 0;
	}

	size_t AssetRegistry::loadDefinitionsFromFolder(const std::string& folderPath) {
		namespace fs = std::filesystem;

		if (!fs::exists(folderPath)) {
			LOG_ERROR(Engine, "Asset definitions folder not found: %s", folderPath.c_str());
			return 0;
		}

		if (!fs::is_directory(folderPath)) {
			LOG_ERROR(Engine, "Path is not a directory: %s", folderPath.c_str());
			return 0;
		}

		size_t totalLoaded = 0;
		size_t filesProcessed = 0;

		// Recursively iterate through all files in the folder
		// Only load "primary" XML files that match the FolderName/FolderName.xml pattern
		try {
			for (const auto& entry : fs::recursive_directory_iterator(folderPath)) {
				if (!entry.is_regular_file()) {
					continue;
				}

				// Only process .xml files
				if (entry.path().extension() != ".xml") {
					continue;
				}

				// Check if this is a primary XML file (FolderName/FolderName.xml pattern)
				// This prevents loading helper XMLs or non-primary definition files
				std::string filename = entry.path().stem().string();
				std::string parentFolder = entry.path().parent_path().filename().string();
				if (filename != parentFolder) {
					LOG_DEBUG(
						Engine,
						"Skipping non-primary XML: %s (filename '%s' != parent '%s')",
						entry.path().string().c_str(),
						filename.c_str(),
						parentFolder.c_str()
					);
					continue;
				}

				filesProcessed++;
				size_t beforeCount = definitions.size();

				if (loadDefinitions(entry.path().string())) {
					size_t loaded = definitions.size() - beforeCount;
					totalLoaded += loaded;
					LOG_DEBUG(Engine, "Loaded %zu definitions from %s", loaded, entry.path().string().c_str());
				}
			}
		} catch (const fs::filesystem_error& e) {
			LOG_ERROR(Engine, "Filesystem error scanning '%s': %s", folderPath.c_str(), e.what());
			return totalLoaded;
		}

		LOG_INFO(
			Engine, "Asset folder scan complete: %zu definitions from %zu XML files in %s", totalLoaded, filesProcessed, folderPath.c_str()
		);

		// Build group index from loaded definitions
		buildGroupIndex();

		// Build string interning index for memory-efficient storage
		buildDefNameIndex();

		return totalLoaded;
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

		renderer::TessellatedMesh mesh;

		if (def->assetType == AssetType::Simple) {
			// Load SVG and tessellate directly
			if (def->svgPath.empty()) {
				LOG_ERROR(Engine, "Simple asset %s has no svgPath", defName.c_str());
				return nullptr;
			}

			// Resolve SVG path relative to asset folder
			std::string resolvedSvgPath = def->resolvePath(def->svgPath).string();
			LOG_DEBUG(Engine, "Resolved SVG path: %s -> %s", def->svgPath.c_str(), resolvedSvgPath.c_str());

			std::vector<renderer::LoadedSVGShape> shapes;
			constexpr float						  kCurveTolerance = 0.5F;
			if (!renderer::loadSVG(resolvedSvgPath, kCurveTolerance, shapes)) {
				LOG_ERROR(Engine, "Failed to load SVG: %s (resolved from %s)", resolvedSvgPath.c_str(), def->svgPath.c_str());
				return nullptr;
			}

			// Calculate SVG bounding box for normalization
			float minY = std::numeric_limits<float>::max();
			float maxY = std::numeric_limits<float>::lowest();
			for (const auto& shape : shapes) {
				for (const auto& svgPath : shape.paths) {
					for (const auto& v : svgPath.vertices) {
						minY = std::min(minY, v.y);
						maxY = std::max(maxY, v.y);
					}
				}
			}
			float svgHeight = maxY - minY;
			float scaleFactor = (svgHeight > 0.001F) ? (def->worldHeight / svgHeight) : 1.0F;
			LOG_INFO(
				Engine,
				"SVG '%s': minY=%.2f, maxY=%.2f, svgHeight=%.2f, worldHeight=%.2f, scaleFactor=%.4f",
				defName.c_str(),
				minY,
				maxY,
				svgHeight,
				def->worldHeight,
				scaleFactor
			);

			// Convert SVG shapes to GeneratedAsset with normalization
			GeneratedAsset asset;
			for (const auto& shape : shapes) {
				for (const auto& svgPath : shape.paths) {
					GeneratedPath genPath;
					genPath.vertices.reserve(svgPath.vertices.size());
					for (const auto& v : svgPath.vertices) {
						genPath.vertices.push_back({v.x * scaleFactor, v.y * scaleFactor});
					}
					genPath.fillColor = shape.fillColor;
					genPath.isClosed = svgPath.isClosed;
					asset.addPath(std::move(genPath));
				}
			}

			if (!tessellateAsset(asset, mesh)) {
				LOG_ERROR(Engine, "Failed to tessellate SVG asset: %s", defName.c_str());
				return nullptr;
			}
		} else {
			// Generate procedural asset
			GeneratedAsset asset;
			if (!generateAsset(defName, 42, asset)) { // Use fixed seed for template
				LOG_ERROR(Engine, "Failed to generate asset: %s", defName.c_str());
				return nullptr;
			}

			if (!tessellateAsset(asset, mesh)) {
				LOG_ERROR(Engine, "Failed to tessellate asset: %s", defName.c_str());
				return nullptr;
			}
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

		// Set up generation context
		GenerationContext ctx;
		ctx.seed = seed;
		ctx.variantIndex = 0;
		outAsset.clear();

		// Check if this is a Lua script generator
		if (def->isLuaGenerator()) {
			try {
				// Resolve script path - check for @shared/ prefix
				std::string		  resolvedScriptPath;
				const std::string kSharedPrefix = "@shared/";
				if (def->scriptPath.compare(0, kSharedPrefix.size(), kSharedPrefix) == 0) {
					// Script is in shared folder - resolve using shared scripts path
					if (m_sharedScriptsPath.empty()) {
						LOG_ERROR(Engine, "Shared scripts path not configured, but @shared/ prefix used in: %s", def->scriptPath.c_str());
						return false;
					}
					std::string relativePath = def->scriptPath.substr(kSharedPrefix.size());
					resolvedScriptPath = (m_sharedScriptsPath / relativePath).string();
					LOG_DEBUG(Engine, "Resolved shared script: %s -> %s", def->scriptPath.c_str(), resolvedScriptPath.c_str());
				} else {
					// Script is local to asset folder
					resolvedScriptPath = def->resolvePath(def->scriptPath).string();
					LOG_DEBUG(Engine, "Resolved local script: %s -> %s", def->scriptPath.c_str(), resolvedScriptPath.c_str());
				}

				LuaGenerator luaGen(resolvedScriptPath);
				return luaGen.generate(ctx, def->params, outAsset);
			} catch (const std::exception& e) {
				LOG_ERROR(Engine, "LuaGenerator error for '%s': %s", def->scriptPath.c_str(), e.what());
				return false;
			}
		}

		// Create C++ generator from registry
		auto generator = GeneratorRegistry::Get().create(def->generatorName.c_str());
		if (!generator) {
			LOG_ERROR(Engine, "Generator not found: %s", def->generatorName.c_str());
			return false;
		}

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
				outMesh.colors.push_back(path.fillColor); // Preserve path color per-vertex
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
		groupIndex.clear();
	}

	std::vector<std::string> AssetRegistry::getDefinitionNames() const {
		std::vector<std::string> names;
		names.reserve(definitions.size());
		for (const auto& [name, _] : definitions) {
			names.push_back(name);
		}
		return names;
	}

	// ============================================================================
	// Group Index Implementation
	// ============================================================================

	void AssetRegistry::buildGroupIndex() {
		groupIndex.clear();

		for (const auto& [defName, def] : definitions) {
			for (const auto& group : def.placement.groups) {
				groupIndex[group].push_back(defName);
			}
		}

		if (!groupIndex.empty()) {
			LOG_DEBUG(Engine, "Built group index: %zu groups", groupIndex.size());
		}
	}

	std::vector<std::string> AssetRegistry::getGroupMembers(const std::string& groupName) const {
		auto it = groupIndex.find(groupName);
		if (it != groupIndex.end()) {
			return it->second;
		}
		return {};
	}

	std::vector<std::string> AssetRegistry::getGroups() const {
		std::vector<std::string> groups;
		groups.reserve(groupIndex.size());
		for (const auto& [name, _] : groupIndex) {
			groups.push_back(name);
		}
		return groups;
	}

	bool AssetRegistry::hasGroup(const std::string& groupName) const {
		return groupIndex.find(groupName) != groupIndex.end();
	}

	// ============================================================================
	// String Interning Implementation
	// ============================================================================

	void AssetRegistry::buildDefNameIndex() {
		m_defNameToId.clear();
		m_idToDefName.clear();
		m_capabilityMasks.clear();

		// Reserve ID 0 as "invalid"
		m_idToDefName.push_back("");
		m_capabilityMasks.push_back(0);

		// Build ID mapping for all definitions
		uint32_t nextId = 1;
		for (const auto& [defName, def] : definitions) {
			m_defNameToId[defName] = nextId;
			m_idToDefName.push_back(defName);

			// Pre-compute capability mask for this definition
			uint8_t mask = 0;
			if (def.capabilities.edible.has_value()) {
				mask |= (1 << static_cast<uint8_t>(CapabilityType::Edible));
			}
			if (def.capabilities.drinkable.has_value()) {
				mask |= (1 << static_cast<uint8_t>(CapabilityType::Drinkable));
			}
			if (def.capabilities.sleepable.has_value()) {
				mask |= (1 << static_cast<uint8_t>(CapabilityType::Sleepable));
			}
			if (def.capabilities.toilet.has_value()) {
				mask |= (1 << static_cast<uint8_t>(CapabilityType::Toilet));
			}
			if (def.capabilities.waste.has_value()) {
				mask |= (1 << static_cast<uint8_t>(CapabilityType::Waste));
			}
			if (def.capabilities.carryable.has_value()) {
				mask |= (1 << static_cast<uint8_t>(CapabilityType::Carryable));
			}
			if (def.capabilities.harvestable.has_value()) {
				mask |= (1 << static_cast<uint8_t>(CapabilityType::Harvestable));
			}
			if (def.capabilities.craftable.has_value()) {
				mask |= (1 << static_cast<uint8_t>(CapabilityType::Craftable));
			}
			if (def.capabilities.storage.has_value()) {
				mask |= (1 << static_cast<uint8_t>(CapabilityType::Storage));
			}
			m_capabilityMasks.push_back(mask);

			nextId++;
		}

		LOG_DEBUG(Engine, "Built defName index: %zu entries", m_idToDefName.size() - 1);
	}

	uint32_t AssetRegistry::getDefNameId(const std::string& defName) const {
		auto it = m_defNameToId.find(defName);
		if (it != m_defNameToId.end()) {
			return it->second;
		}
		return 0; // Invalid ID
	}

	const std::string& AssetRegistry::getDefName(uint32_t id) const {
		if (id < m_idToDefName.size()) {
			return m_idToDefName[id];
		}
		static const std::string kEmptyString;
		return kEmptyString;
	}

	uint8_t AssetRegistry::getCapabilityMask(uint32_t id) const {
		if (id < m_capabilityMasks.size()) {
			return m_capabilityMasks[id];
		}
		return 0;
	}

	bool AssetRegistry::hasCapability(uint32_t id, CapabilityType capability) const {
		uint8_t mask = getCapabilityMask(id);
		return (mask & (1 << static_cast<uint8_t>(capability))) != 0;
	}

	uint32_t AssetRegistry::registerSyntheticDefinition(const std::string& defName, uint8_t capabilityMask) {
		// Check if already registered
		auto it = m_defNameToId.find(defName);
		if (it != m_defNameToId.end()) {
			return it->second; // Already registered, return existing ID
		}

		// Assign new ID
		auto newId = static_cast<uint32_t>(m_idToDefName.size());
		m_defNameToId[defName] = newId;
		m_idToDefName.push_back(defName);
		m_capabilityMasks.push_back(capabilityMask);

		LOG_DEBUG(Engine, "Registered synthetic definition '%s' with ID %u, capabilities 0x%02X", defName.c_str(), newId, capabilityMask);

		return newId;
	}

	// ============================================================================
	// Testing API
	// ============================================================================

	void AssetRegistry::registerTestDefinition(AssetDefinition def) {
		std::string defName = def.defName;
		definitions[defName] = std::move(def);

		// Rebuild indices to include new definition
		buildDefNameIndex();

		LOG_DEBUG(Engine, "Registered test definition: %s", defName.c_str());
	}

	void AssetRegistry::clearDefinitions() {
		definitions.clear();
		templateCache.clear();
		groupIndex.clear();
		m_defNameToId.clear();
		m_idToDefName.clear();
		m_capabilityMasks.clear();
		LOG_DEBUG(Engine, "Cleared all definitions");
	}

} // namespace engine::assets

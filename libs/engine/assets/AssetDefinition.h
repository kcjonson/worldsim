#pragma once

// Asset Definition Types
// Data structures for asset definitions parsed from XML.
// Designed for C++ generators now with Lua drop-in compatibility later.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "placement/PlacementTypes.h"

namespace engine::assets {

	// ─────────────────────────────────────────────────────────────────────────
	// Entity Capability System
	// Capabilities define what actions can be performed on/with an entity.
	// Used by AI to find entities that fulfill needs.
	// ─────────────────────────────────────────────────────────────────────────

	/// Capability type - what kind of interaction an entity supports
	enum class CapabilityType {
		Edible,		// Entity can be eaten to restore hunger
		Drinkable,	// Entity can be drunk from to restore thirst
		Sleepable,	// Entity can be slept on to restore energy
		Toilet,		// Entity can be used to relieve bladder
		Waste,		// Entity is waste (bio pile) - used for clustering toilet locations
		Carryable,	// Entity can be picked up directly (ground items like stones)
		Harvestable, // Entity can be harvested for items (bushes, plants)
		Craftable	// Entity is a crafting station where items can be made
	};

	/// Quality level for capabilities (affects mood, health effects)
	enum class CapabilityQuality {
		Terrible, // Ground sleeping, dirty water
		Poor,	  // Basic/raw
		Normal,	  // Standard
		Good,	  // Comfortable
		Excellent // Luxury
	};

	/// Edible capability - entity can be eaten
	struct EdibleCapability {
		float			  nutrition = 0.3F; // How much hunger is restored (0-1 scale per eat action)
		CapabilityQuality quality = CapabilityQuality::Normal;
		bool			  spoilable = false; // Does it decay over time?
	};

	/// Drinkable capability - entity can be drunk from
	struct DrinkableCapability {
		CapabilityQuality quality = CapabilityQuality::Normal; // Affects health (dirty water = illness risk)
	};

	/// Sleepable capability - entity can be slept on
	struct SleepableCapability {
		CapabilityQuality quality = CapabilityQuality::Normal;
		float			  recoveryMultiplier = 1.0F; // Energy recovery rate (0.5 = slow, 1.0 = normal, 1.2 = good)
	};

	/// Toilet capability - entity can be used to relieve bladder
	struct ToiletCapability {
		bool hygieneBonus = false; // Does using this improve hygiene?
	};

	/// Waste capability - entity is waste (bio pile) for clustering toilet locations
	struct WasteCapability {
		// No properties yet - just a marker capability
	};

	/// Carryable capability - entity can be picked up directly into inventory
	/// The entity itself goes into inventory (unified entity/item model)
	struct CarryableCapability {
		uint32_t quantity = 1; // How many to add when picked up
	};

	/// Harvestable capability - entity yields items when harvested
	/// Used for plants, bushes, trees that produce resources
	struct HarvestableCapability {
		std::string yieldDefName;		 // Item definition name to yield (e.g., "Stick", "Berry")
		uint32_t	amountMin = 1;		 // Minimum items yielded per harvest
		uint32_t	amountMax = 3;		 // Maximum items yielded per harvest
		float		duration = 4.0F;	 // Seconds to complete harvest action
		bool		destructive = true;	 // If true, entity is removed after harvest
		float		regrowthTime = 0.0F; // Seconds until harvestable again (0 = never, only if not destructive)
	};

	/// Craftable capability - entity is a crafting station
	/// Used for workbenches, crafting spots, and other production stations
	struct CraftableCapability {
		// For now, this is just a marker capability
		// Future: could include speed modifiers, quality bonuses, etc.
	};

	// ─────────────────────────────────────────────────────────────────────────
	// Item Properties (for entities that can exist in inventory)
	// Unified model: entities can be "in world" or "in inventory"
	// ─────────────────────────────────────────────────────────────────────────

	/// Item properties for entities that can be carried/stored in inventory
	/// If an entity has ItemProperties, it can exist in inventory.
	struct ItemProperties {
		uint32_t stackSize = 1; // Max stack size in inventory

		/// Edible properties (if item can be eaten from inventory)
		std::optional<EdibleCapability> edible;

		/// Check if this item is edible
		[[nodiscard]] bool isEdible() const { return edible.has_value(); }

		/// Get nutrition value (0 if not edible)
		[[nodiscard]] float getNutrition() const {
			return edible.has_value() ? edible->nutrition : 0.0F;
		}

		/// Get quality (Normal if not edible)
		[[nodiscard]] CapabilityQuality getQuality() const {
			return edible.has_value() ? edible->quality : CapabilityQuality::Normal;
		}
	};

	/// Container for all capabilities an entity may have
	struct EntityCapabilities {
		std::optional<EdibleCapability>		 edible;
		std::optional<DrinkableCapability>	 drinkable;
		std::optional<SleepableCapability>	 sleepable;
		std::optional<ToiletCapability>		 toilet;
		std::optional<WasteCapability>		 waste;
		std::optional<CarryableCapability>	 carryable;
		std::optional<HarvestableCapability> harvestable;
		std::optional<CraftableCapability>	 craftable;

		/// Check if entity has any capabilities
		[[nodiscard]] bool hasAny() const {
			return edible.has_value() || drinkable.has_value() || sleepable.has_value() || toilet.has_value() || waste.has_value() ||
				   carryable.has_value() || harvestable.has_value() || craftable.has_value();
		}

		/// Check if entity has a specific capability type
		[[nodiscard]] bool has(CapabilityType type) const {
			switch (type) {
				case CapabilityType::Edible:
					return edible.has_value();
				case CapabilityType::Drinkable:
					return drinkable.has_value();
				case CapabilityType::Sleepable:
					return sleepable.has_value();
				case CapabilityType::Toilet:
					return toilet.has_value();
				case CapabilityType::Waste:
					return waste.has_value();
				case CapabilityType::Carryable:
					return carryable.has_value();
				case CapabilityType::Harvestable:
					return harvestable.has_value();
				case CapabilityType::Craftable:
					return craftable.has_value();
			}
			return false;
		}
	};

	/// Asset type - how the shape is defined
	enum class AssetType {
		Simple,	   // Pre-made SVG file
		Procedural // Generated by C++ or Lua code
	};

	/// Distribution pattern for asset placement
	enum class Distribution {
		Uniform, // Random placement, no clustering
		Clumped, // Groups together in patches
		Spaced	 // Maintains minimum distance between instances
	};

	/// Asset complexity - affects rendering strategy
	enum class AssetComplexity {
		Simple, // Uses GPU instancing (grass, small flora)
		Complex // Individual tessellation (trees, buildings)
	};

	/// Animation type - how animation is applied
	enum class AnimationType {
		None,		 // No animation
		Parametric,	 // Simple sin-based wind sway (vertex shader)
		BezierDeform // Full Bezier curve deformation (CPU, expensive)
	};

	/// Rendering tier - determines batching strategy
	enum class RenderingTier {
		Instanced, // Single template + GPU instancing
		Batched,   // Multiple variants in batched draw calls
		Individual // Each instance drawn separately
	};

	/// Key-value parameter store for generator configuration.
	/// Supports string, float, and range values parsed from XML.
	class GeneratorParams {
	  public:
		GeneratorParams() = default;

		/// Get a string parameter
		std::string getString(const char* key, const std::string& defaultVal = "") const;

		/// Get a float parameter
		float getFloat(const char* key, float defaultVal = 0.0F) const;

		/// Get a float range parameter (min,max format in XML)
		void getFloatRange(const char* key, float& outMin, float& outMax, float defaultMin = 0.0F, float defaultMax = 1.0F) const;

		/// Get an integer parameter
		int32_t getInt(const char* key, int32_t defaultVal = 0) const;

		/// Set a string parameter
		void setString(const char* key, const std::string& value);

		/// Set a float parameter (stored as string for consistency)
		void setFloat(const char* key, float value);

		/// Check if a parameter exists
		bool has(const char* key) const;

	  private:
		std::unordered_map<std::string, std::string> params;
	};

	/// Animation parameters parsed from asset definition
	struct AnimationParams {
		bool		  enabled = false;
		AnimationType type = AnimationType::None;
		float		  windResponse = 0.3F;	   // How much wind affects this asset (0-1)
		float		  swayFrequencyMin = 0.5F; // Animation speed range
		float		  swayFrequencyMax = 1.0F;
	};

	/// Clumping parameters for Distribution::Clumped
	struct ClumpingParams {
		int32_t clumpSizeMin = 3;		// Number of instances per clump (min)
		int32_t clumpSizeMax = 12;		// Number of instances per clump (max)
		float	clumpRadiusMin = 0.5F;	// Radius in tile-widths within which clump instances are distributed (min)
		float	clumpRadiusMax = 2.0F;	// Radius in tile-widths within which clump instances are distributed (max)
		float	clumpSpacingMin = 3.0F; // Minimum distance in tiles between centers of different clumps
		float	clumpSpacingMax = 8.0F; // Maximum distance in tiles between centers of different clumps
	};

	/// Spacing parameters for Distribution::Spaced
	struct SpacingParams {
		float minDistance = 2.0F; // Minimum tiles between instances
	};

	/// Per-biome placement configuration.
	/// Each biome can have different spawn behavior for the same asset.
	/// E.g., grass in grassland: dense/uniform; grass in forest: sparse/clumped.
	struct BiomePlacement {
		std::string	   biomeName;		   // "Grassland", "Forest", etc.
		float		   spawnChance = 0.3F; // Probability at each spawn point (0-1)
		Distribution   distribution = Distribution::Uniform;
		ClumpingParams clumping; // Only used if distribution == Clumped
		SpacingParams  spacing;	 // Only used if distribution == Spaced

		// Tile-type proximity - for rules like "near Water"
		std::string nearTileType;		 // e.g., "Water" (empty = no restriction)
		float		nearDistance = 0.0F; // Max tiles from nearTileType to spawn
	};

	/// Placement parameters - where assets spawn in the world.
	/// Contains per-biome configuration for flexible spawn behavior.
	struct PlacementParams {
		std::vector<BiomePlacement> biomes; // Per-biome spawn configuration

		// Entity placement system fields (self-declared groups and relationships)
		std::vector<std::string>		   groups;		  // Groups this asset belongs to (e.g., "trees", "flowers")
		std::vector<PlacementRelationship> relationships; // Entity-to-entity spawn rules

		/// Find placement config for a specific biome name (returns nullptr if not found)
		[[nodiscard]] const BiomePlacement* findBiome(const std::string& biomeName) const {
			for (const auto& bp : biomes) {
				if (bp.biomeName == biomeName) {
					return &bp;
				}
			}
			return nullptr;
		}
	};

	/// Complete asset definition parsed from XML
	struct AssetDefinition {
		std::string			  defName; // Unique identifier (e.g., "Flora_GrassBlade")
		std::string			  label;   // Human-readable name
		AssetType			  assetType = AssetType::Procedural;
		std::string			  generatorName;	  // Generator to use (e.g., "GrassBlade") - C++ generators
		std::string			  scriptPath;		  // For Lua generators: path to Lua script (relative to assets/)
		std::string			  svgPath;			  // For Simple assets: path to SVG file
		std::filesystem::path baseFolder;		  // Folder containing this asset's definition (for relative path resolution)
		float				  worldHeight = 1.0F; // World height in meters (for SVG normalization)
		GeneratorParams		  params;			  // Parameters for generator
		AnimationParams		  animation;
		PlacementParams					  placement;	   // Where this asset spawns
		EntityCapabilities				  capabilities;	   // What actions can be performed on/with this entity
		std::optional<ItemProperties>	  itemProperties;  // Properties when in inventory (if carryable)
		AssetComplexity					  complexity = AssetComplexity::Simple;
		RenderingTier					  renderingTier = RenderingTier::Instanced;
		uint32_t						  variantCount = 1; // Number of variants to pre-generate

		/// Check if this entity can exist in inventory
		[[nodiscard]] bool isCarryable() const { return itemProperties.has_value(); }

		/// Check if this entity is edible when in inventory
		[[nodiscard]] bool isEdible() const {
			return itemProperties.has_value() && itemProperties->isEdible();
		}

		/// Check if this definition uses a Lua script generator
		[[nodiscard]] bool isLuaGenerator() const { return !scriptPath.empty(); }

		/// Resolve a relative path to absolute using this asset's base folder
		[[nodiscard]] std::filesystem::path resolvePath(const std::string& relativePath) const {
			if (relativePath.empty()) {
				return {};
			}
			std::filesystem::path rel(relativePath);
			if (rel.is_absolute()) {
				return rel;
			}
			return baseFolder / rel;
		}
	};

} // namespace engine::assets

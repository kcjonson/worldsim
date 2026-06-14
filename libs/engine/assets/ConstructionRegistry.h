#pragma once

// Construction Registry
// Loads and exposes construction configuration: materials, shape constraints,
// and snapping parameters. Follows the WorkTypeRegistry singleton pattern.
//
// See /docs/technical/building-construction-architecture.md section D10 for design.
// See /docs/design/game-systems/world/building-construction.md for the Config Surface.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::assets {

	// ---------------------------------------------------------------------------
	// Plain-data structs
	// ---------------------------------------------------------------------------

	/// RGBA color stored as four 0-255 bytes. Used for material palette entries.
	struct PatternColor {
		uint8_t r = 0;
		uint8_t g = 0;
		uint8_t b = 0;
		uint8_t a = 255;
	};

	/// Visual pattern block shared by all structure materials.
	struct PatternDef {
		/// Element emitter type name (e.g. "planks", "courses").
		std::string emitter;
		/// Base seed for deterministic per-structure pattern generation.
		uint32_t seed = 0;
		/// Colour palette fed to the emitter (at least one entry required).
		std::vector<PatternColor> palette;
	};

	/// A discrete wall thickness option for a material (e.g. Light / Standard / Heavy).
	/// Wall work and cost are computed as: length × thicknessMeters × material rate × multiplier.
	/// thicknessMm and halfThicknessMm are pre-quantized at load for geometry consumers
	/// (geometry::WallSegment uses halfThicknessMm for band offsetting).
	struct ThicknessPreset {
		/// Display name, e.g. "Light", "Standard", "Heavy".
		std::string name;
		/// Thickness in meters (floating-point, for UI display).
		float thicknessMeters = 0.0F;
		/// Pre-quantized thickness: round(thicknessMeters * 1000). Integer mm.
		int64_t thicknessMm = 0;
		/// Half-thickness in mm (thicknessMm / 2). Used by band offsetting.
		int64_t halfThicknessMm = 0;
		/// Multiplies the material's base costRatePerSquareMeter for wall area.
		float costMultiplier = 1.0F;
		/// Multiplies the material's base workRatePerSquareMeter for wall area.
		float workMultiplier = 1.0F;
		/// Multiplies the material's base hp for wall area.
		float hpMultiplier = 1.0F;
		/// Thermal resistance starter value; design TBD.
		float insulation = 0.0F;
	};

	/// Per-material definition for one structure kind (e.g. Foundation/Wood).
	/// All rates are in SI units: meters, square meters, work units.
	///
	/// A single MaterialDef carries both foundation rates and optional wall thickness
	/// presets. This avoids a parallel map lookup when the drawing tool needs both the
	/// palette and the preset list for the same material. wallThicknesses is empty for
	/// materials that have no wall configuration (e.g. future Stone before its item
	/// asset exists).
	struct MaterialDef {
		/// Canonical name, e.g. "Wood", "Stone".
		std::string name;
		/// Material items consumed per square meter of foundation floor.
		float costRatePerSquareMeter = 0.0F;
		/// Work units required per square meter (drives colonist build time).
		float workRatePerSquareMeter = 0.0F;
		/// Hit points per square meter of structure.
		float hp = 0.0F;
		/// Fire susceptibility: 0.0 = fireproof, 1.0 = highly flammable.
		float flammability = 0.0F;
		/// Beauty contribution per square meter.
		float beauty = 0.0F;
		/// Walking speed multiplier on the finished floor (1.0 = no change).
		float speedModifier = 1.0F;
		/// Visual pattern used by the renderer.
		PatternDef pattern;
		/// Wall thickness presets for this material. Empty if the material has no
		/// wall configuration. Populated from the <Wall> section of materials.xml.
		std::vector<ThicknessPreset> wallThicknesses;
	};

	/// An opening type (e.g. Door, Window). Cost and work are CONSTANTS per type,
	/// not area-derived: the build effort of a door does not depend on the wall it
	/// sits in. widthMeters is the clear width along the wall centerline; widthMm is
	/// the pre-quantized mirror for the geometry/validator layer. material is a
	/// config NAME resolved against the materials map (ConfigValidator checks it).
	struct OpeningTypeDef {
		/// Canonical name, e.g. "Door", "Window".
		std::string name;
		/// Material consumed, by name (must reference a loaded MaterialDef).
		std::string material;
		/// Clear width in meters (floating-point, for UI display and validation).
		float widthMeters = 0.0F;
		/// Pre-quantized width: round(widthMeters * 1000). Integer mm.
		int64_t widthMm = 0;
		/// Whether colonists can walk through the finished opening (doors yes, windows no).
		bool pathable = false;
		/// Fixed material items consumed to build the opening (constant per type).
		float costItems = 0.0F;
		/// Fixed work units to build the opening (constant per type, drives build time).
		float workUnits = 0.0F;
	};

	/// Shape and gameplay constraints for all foundations and walls.
	/// Float members are in meters/degrees/percent (UI-layer friendly).
	/// Int64 members are pre-quantized to millimeters (geometry/validator layer).
	struct ConstraintConfig {
		// Pathing
		float	pathingClearanceMeters = 0.7F;
		int64_t pathingClearanceMm = 700;

		// Corner angles (shared by foundations and wall junctions)
		float minCornerAngleDegrees = 30.0F;

		// Vertex spacing
		float	minVertexSpacingMeters = 0.5F;
		int64_t minVertexSpacingMm = 500;

		// Edge clearance (non-adjacent segments of the same polygon)
		float	segmentClearanceMeters = 1.0F;
		int64_t segmentClearanceMm = 1000;

		// Area
		float minAreaSquareMeters = 4.0F;
		float maxAreaSquareMeters = 2500.0F;

		// Polygon complexity
		int maxPoints = 32;

		// Opening margin (distance from corner/junction to opening edge)
		float	openingMarginMeters = 0.3F;
		int64_t openingMarginMm = 300;

		// Demolition refund
		float refundPercent = 50.0F;

		// Concurrent builder caps
		int	  builderCapBase = 1;
		float builderCapPerSquareMeter = 0.1F;

		// Wall constraints
		// minWallJunctionAngleDegrees is identical in value to minCornerAngleDegrees (both
		// 30°) and enforces the same pathfinding invariant for wall junctions. Stored
		// separately so callers can express intent clearly and the XML is self-documenting.
		float	minSegmentLengthMeters = 0.5F;
		int64_t minSegmentLengthMm = 500;

		float minWallJunctionAngleDegrees = 30.0F;

		// Minimum face-to-face clearance between parallel walls (thickness included).
		// Must be >= pathingClearanceMeters so colonists can pass between walls.
		float	minParallelClearanceMeters = 0.8F;
		int64_t minParallelClearanceMm = 800;
	};

	/// Snapping parameters for the drawing tools.
	/// Float members are in meters/degrees.
	/// Int64 members are pre-quantized to millimeters.
	struct SnappingConfig {
		float angleIncrementDegrees = 15.0F;

		float	vertexSnapRadiusMeters = 0.4F;
		int64_t vertexSnapRadiusMm = 400;

		float	edgeSnapRadiusMeters = 0.3F;
		int64_t edgeSnapRadiusMm = 300;

		float	smartGuideRangeMeters = 8.0F;
		int64_t smartGuideRangeMm = 8000;

		float	originCloseRadiusMeters = 0.5F;
		int64_t originCloseRadiusMm = 500;
	};

	// ---------------------------------------------------------------------------
	// Registry
	// ---------------------------------------------------------------------------

	/// Singleton registry for all construction configuration.
	/// Load via load() or the individual loadXxx() methods before first use.
	class ConstructionRegistry {
	  public:
		/// Returns the singleton instance.
		static ConstructionRegistry& Get();

		// --- Loading ---

		/// Load all three construction config files from a folder.
		/// Expects materials.xml, constraints.xml, snapping.xml in folderPath.
		/// @return true if all three loaded successfully
		bool load(const std::string& folderPath);

		/// Load materials.xml from path.
		bool loadMaterials(const std::string& xmlPath);

		/// Load constraints.xml from path.
		bool loadConstraints(const std::string& xmlPath);

		/// Load snapping.xml from path.
		bool loadSnapping(const std::string& xmlPath);

		/// Reset all loaded data.
		void clear();

		// --- Material queries ---

		/// Get a material by name (e.g. "Wood"). The same MaterialDef carries both
		/// foundation rates and optional wall thickness presets; callers that only
		/// need wall presets can use getWallMaterial or getThicknessPreset instead.
		/// @return pointer to MaterialDef, or nullptr if not found
		[[nodiscard]] const MaterialDef* getMaterial(const std::string& name) const;

		/// Get all loaded materials.
		[[nodiscard]] const std::unordered_map<std::string, MaterialDef>& getAllMaterials() const;

		/// Get a material that has wall thickness presets, by name.
		/// Equivalent to getMaterial but returns nullptr for materials whose
		/// wallThicknesses vector is empty.
		/// @return pointer to MaterialDef, or nullptr if not found or no wall presets
		[[nodiscard]] const MaterialDef* getWallMaterial(const std::string& name) const;

		/// Get a specific thickness preset by material name and preset name.
		/// @return pointer to ThicknessPreset, or nullptr if either is not found
		[[nodiscard]] const ThicknessPreset* getThicknessPreset(const std::string& materialName, const std::string& presetName) const;

		// --- Opening queries ---

		/// Get an opening type by name (e.g. "Door").
		/// @return pointer to OpeningTypeDef, or nullptr if not found
		[[nodiscard]] const OpeningTypeDef* getOpeningType(const std::string& name) const;

		/// Get all loaded opening types, in load order (stable for determinism).
		[[nodiscard]] const std::vector<OpeningTypeDef>& openingTypes() const;

		// --- Constraint / snapping queries ---

		[[nodiscard]] const ConstraintConfig& constraints() const;
		[[nodiscard]] const SnappingConfig&	  snapping() const;

		// --- Load state ---

		[[nodiscard]] bool materialsLoaded() const;
		[[nodiscard]] bool constraintsLoaded() const;
		[[nodiscard]] bool snappingLoaded() const;

	  private:
		ConstructionRegistry() = default;

		/// Parse a hex RGBA string like "#C8915AFF" into a PatternColor.
		static PatternColor parseColor(const std::string& hex);

		/// Quantize a float meters value to int64 millimeters.
		static int64_t toMm(float meters);

		std::unordered_map<std::string, MaterialDef> materials;
		std::vector<OpeningTypeDef>					 openingTypeList; // load order, stable for determinism
		ConstraintConfig							 constraintConfig;
		SnappingConfig								 snappingConfig;

		bool hasMaterials = false;
		bool hasConstraints = false;
		bool hasSnapping = false;
	};

} // namespace engine::assets

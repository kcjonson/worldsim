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

	/// Per-material definition for one structure kind (e.g. Foundation/Wood).
	/// All rates are in SI units: meters, square meters, work units.
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
	};

	/// Shape and gameplay constraints for all foundations.
	/// Float members are in meters/degrees/percent (UI-layer friendly).
	/// Int64 members are pre-quantized to millimeters (geometry/validator layer).
	struct ConstraintConfig {
		// Pathing
		float	pathingClearanceMeters = 0.7F;
		int64_t pathingClearanceMm = 700;

		// Corner angles
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

		/// Get a foundation material by name (e.g. "Wood").
		/// @return pointer to MaterialDef, or nullptr if not found
		[[nodiscard]] const MaterialDef* getMaterial(const std::string& name) const;

		/// Get all loaded foundation materials.
		[[nodiscard]] const std::unordered_map<std::string, MaterialDef>& getAllMaterials() const;

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
		ConstraintConfig							 constraintConfig;
		SnappingConfig								 snappingConfig;

		bool hasMaterials = false;
		bool hasConstraints = false;
		bool hasSnapping = false;
	};

} // namespace engine::assets

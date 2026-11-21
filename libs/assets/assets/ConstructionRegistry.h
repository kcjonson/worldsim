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

namespace assets {

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
		/// The wall material this preset belongs to (a key for getThicknessPreset).
		std::string material;
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
		/// Whether sight passes through the finished opening (doors AND windows yes).
		/// Independent of pathable: a window blocks movement but not sight. The Vision
		/// System treats a transparent opening as a gap in the wall's occluder line.
		bool transparentToSight = false;
		/// Fixed material items consumed to build the opening (constant per type).
		float costItems = 0.0F;
		/// Fixed work units to build the opening (constant per type, drives build time).
		float workUnits = 0.0F;
	};

	/// A config value the loader could not use (an enum token naming no listed value).
	/// The loader keeps the default and records it here; ConfigValidator turns each
	/// into a validation error.
	struct ConfigParseError {
		std::string message;
		std::string context;
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
		float builderSpotMinSpacingMeters = 1.0F;

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

	/// What a guide through some other point than the previous one rounds when a
	/// length increment applies: its distance along the guide, or the segment length.
	enum class GuideRounding { GuideDistance, SegmentLength };

	/// How competing guides are ranked: by source first, or purely by distance.
	enum class GuidePriorityRule { SourceFirst, Closest };

	/// Starting value of each player-facing Drawing Setting, one field per setting: what a
	/// new player meets, every helper off, Round lengths and Show key guide on, a 1 m grid.
	struct DrawingSettingsDefaults {
		bool  angleSteps = false;
		bool  guidesFromShape = false;
		bool  alignToExistingBuildings = false;
		bool  guidesFromWorldAxes = false;
		bool  guideCrossings = false;
		bool  commonLengths = false;
		bool  guideDistances = false;
		bool  angleReadout = false;
		bool  roundLengths = true;
		bool  showKeyGuide = true;
		float rectangleGridSpacingMeters = 1.0F;
	};

	/// The most common lengths the snap engine keeps and reports, so a snap never allocates
	/// and no length catches without its label. The validator refuses a larger
	/// commonLengthMaxCount.
	inline constexpr int kMaxCommonLengthCount = 16;

	/// Snapping parameters for the drawing tools.
	/// Float members are in meters/degrees.
	/// Int64 members are pre-quantized to millimeters.
	struct SnappingConfig {
		float angleIncrementDegrees = 15.0F;

		float	vertexSnapRadiusMeters = 0.4F;
		int64_t vertexSnapRadiusMm = 400;

		float	edgeSnapRadiusMeters = 0.3F;
		int64_t edgeSnapRadiusMm = 300;

		// How far from a placed item's pivot the nearest wall face or foundation edge turns it square to
		// itself (rotatable-placed-items 14.3).
		float itemAlignRangeMeters = 2.0F;

		// The reach of every guide from committed structures: a corner (a foundation
		// corner, or in the wall tool a wall vertex) within this distance of the cursor
		// gives guides, and an edge within it gives a nearby direction. The shape's own
		// points always give guides, whatever their distance.
		float guideRangeMeters = 8.0F;

		// Perpendicular tolerance for a guide lock: the cursor snaps onto a guide
		// line when it is within this distance of it.
		float guideToleranceMeters = 0.3F;

		float guideCrossingRadiusMeters = 1.0F;
		float guideDirectionToleranceDegrees = 1.0F;
		int	  nearbyGuideDirectionLimit = 4;
		int	  guideCornerLimit = 16;

		// Each tied value follows another key while its tie flag is set (window =
		// angle step / 2, crossing minimum = angle step, merge distance = guide
		// tolerance) and is the explicit value otherwise. Read the effective*()
		// accessors, not the explicit fields.
		bool  tieAngleWindowToStep = true;
		float guideAngleWindowDegrees = 7.5F;
		bool  tieCrossingAngleToStep = true;
		float guideCrossingMinAngleDegrees = 15.0F;
		bool  tieMergeDistanceToTolerance = true;
		float guideMergeDistanceMeters = 0.3F;

		GuidePriorityRule priorityRule = GuidePriorityRule::SourceFirst;

		bool  commonLengthSourceBuildings = true;
		bool  commonLengthSourceShape = true;
		float commonLengthRangeMeters = 30.0F;
		bool  commonLengthAllBuildings = false;
		float commonLengthCatchMeters = 0.3F;
		float commonLengthMergeMeters = 0.05F;
		int	  commonLengthMaxCount = 6;
		float commonLengthMinimumMeters = 0.5F;
		bool  commonLengthAfterRawCursor = true;
		bool  commonLengthBeatsCrossing = false;
		bool  commonLengthWallsReadInsetFoundation = true;

		float		  lengthIncrementMeters = 0.1F;
		GuideRounding lengthRoundingOnGuides = GuideRounding::GuideDistance;

		bool rectangleAdjacencySnaps = true;

		// Two left releases on the world this close in time are a double-click: with the wall
		// tool, the second ends the chain.
		float doubleClickSeconds = 0.4F;
		// A left release this close to its press, in logical pixels, is a click: in the
		// rectangle tool it leaves the first corner waiting rather than finishing a drag. A
		// double-click's second release must also be this close to the first.
		float doubleClickDistancePx = 4.0F;

		DrawingSettingsDefaults drawingSettingsDefaults;

		float	originCloseRadiusMeters = 0.5F;
		int64_t originCloseRadiusMm = 500;

		[[nodiscard]] float effectiveGuideAngleWindowDegrees() const {
			return tieAngleWindowToStep ? angleIncrementDegrees / 2.0F : guideAngleWindowDegrees;
		}
		[[nodiscard]] float effectiveGuideCrossingMinAngleDegrees() const {
			return tieCrossingAngleToStep ? angleIncrementDegrees : guideCrossingMinAngleDegrees;
		}
		[[nodiscard]] float effectiveGuideMergeDistanceMeters() const {
			return tieMergeDistanceToTolerance ? guideToleranceMeters : guideMergeDistanceMeters;
		}
	};

	// ---------------------------------------------------------------------------
	// Rendering style
	// ---------------------------------------------------------------------------
	// The construction draw layer (DrawingSystem) renders foundations, walls, and
	// openings with Renderer::Primitives, NOT the SVG/vector asset pipeline. These
	// structs make the whole committed look data-driven: every color, alpha ramp,
	// outline weight, and door/window detail dimension comes from rendering.xml.
	// Defaults below mirror the values the renderer shipped with, so a missing or
	// partial file degrades to the original look rather than breaking. Material FILL
	// color still comes from each material's palette in materials.xml; these styles
	// govern the treatment around it (blueprint tint, progress ramp, edges, detail).
	// z-order stays in code: it is structural layering, not look-and-feel.

	/// RGBA in float [0,1] (the renderer's native form), parsed from "#RRGGBBAA".
	struct StyleColor {
		float r = 0.0F;
		float g = 0.0F;
		float b = 0.0F;
		float a = 1.0F;
	};

	/// Committed-foundation look. The fill is the material palette color; its alpha
	/// ramps progressAlphaMin..Max with build progress (Built renders at Max). A
	/// blueprint wears the cool outlineColor (alpha ramps outlineAlphaMin..Max); a
	/// Built foundation wears a darker shade of its own material (builtEdgeDarken), so
	/// finished structures read as solid material and blue stays the "planned" cue.
	/// blueprintFill is the always-present faint base under the progress fill.
	struct FoundationStyle {
		StyleColor blueprintFill{0.5F, 0.65F, 0.9F, 0.18F};
		StyleColor fallbackColor{0.5F, 0.65F, 0.9F, 1.0F}; // used when the palette is empty
		StyleColor outlineColor{0.55F, 0.72F, 1.0F, 1.0F}; // blueprint edge; rgb only, alpha ramps
		float	   progressAlphaMin = 0.15F;
		float	   progressAlphaMax = 0.85F;
		float	   outlineAlphaMin = 0.6F;
		float	   outlineAlphaMax = 1.0F;
		float	   outlineWidthBlueprint = 1.5F;
		float	   outlineWidthBuilt = 2.0F;
		float	   builtEdgeDarken = 0.55F; // Built edge = material color * this
	};

	/// Committed-wall look. Same ramp model as foundations. A Built wall renders nearly
	/// opaque with a darker-material edge (builtEdgeDarken), so it stands clearly proud
	/// of the floor; a blueprint keeps the cool outlineColor. A corner's junction polygon
	/// is filled as part of one wall meeting there, so it wears that wall's style.
	struct WallStyle {
		StyleColor blueprintFill{0.5F, 0.65F, 0.9F, 0.22F};
		StyleColor fallbackColor{0.5F, 0.65F, 0.9F, 1.0F};
		StyleColor outlineColor{0.6F, 0.78F, 1.0F, 1.0F}; // blueprint edge; rgb only, alpha ramps
		float	   progressAlphaMin = 0.2F;
		float	   progressAlphaMax = 0.97F;
		float	   outlineAlphaMin = 0.65F;
		float	   outlineAlphaMax = 1.0F;
		float	   outlineWidthBlueprint = 1.5F;
		float	   outlineWidthBuilt = 2.0F;
		float	   builtEdgeDarken = 0.5F; // Built edge = material color * this
	};

	/// Procedural door/window look. The leaf/frame fill is the opening material color
	/// (fallback doorFallbackColor), at fillAlpha; the outline is that color darkened
	/// by outlineDarken at outlineAlpha. Doors get darkened jamb caps (jambWidth of the
	/// clear width, jambDarken, jambAlpha) and a center seam; windows get an inset glass
	/// pane (glassColor, glassInset across the thickness) crossed by mullion bars spaced
	/// ~mullionSpacingMeters apart. The whole fill is scaled by the opening's build alpha
	/// (progressAlphaMin..Max); the placement ghost draws at ghostAlpha.
	struct OpeningStyle {
		StyleColor doorFallbackColor{0.55F, 0.40F, 0.25F, 1.0F};
		StyleColor glassColor{0.56F, 0.80F, 0.95F, 0.72F};
		float	   fillAlpha = 0.92F;
		float	   outlineAlpha = 0.95F;
		float	   outlineDarken = 0.7F;
		float	   outlineWidthBuilt = 2.0F;
		float	   outlineWidthBlueprint = 1.0F;
		float	   jambWidth = 0.11F;
		float	   jambDarken = 0.5F;
		float	   jambAlpha = 0.95F;
		float	   glassInset = 0.18F;
		float	   mullionSpacingMeters = 0.7F;
		float	   mullionAlpha = 0.9F;
		float	   progressAlphaMin = 0.25F; // blueprint ramp floor
		float	   progressAlphaMax = 0.85F; // blueprint ramp cap (Built renders fully opaque)
		float	   ghostAlpha = 0.5F;
	};

	enum class GuideLineStyle { Solid, Dashed, Dotted };

	enum class GuideColorMode { One, BySource };

	/// Drawing-tool preview chrome that is construction-specific (the validity green/red
	/// still comes from the shared UI theme). guideColor is the one guide color (guide
	/// lines, right-angle markers, crossing dots, the common-length marks), opaque: its
	/// opacity comes from guideOpacity and markerOpacity. By source, guideColorLength colors
	/// the common-length marks. angleReadoutDecimals sets the angle readout's places and its
	/// square tolerance. originHaloColor is the dotted close-radius ring round the first
	/// point; snap-target rings use snapVertexColor (vertex/endpoint) and snapEdgeColor
	/// (edge/T-junction). The alphas tint the in-progress polygon fill and the wall-band
	/// preview; the *Px radii and widths are screen-space sizes that never scale with zoom.
	/// Of them, only the labels (guideLabelPx, the segment and size labels, and the plates
	/// behind them) follow the interface size.
	struct PreviewStyle {
		StyleColor guideColor{0.373F, 0.702F, 0.784F, 1.0F};
		StyleColor originHaloColor{0.7F, 0.85F, 1.0F, 0.6F};
		StyleColor snapVertexColor{1.0F, 0.85F, 0.3F, 0.9F};
		StyleColor snapEdgeColor{0.4F, 0.85F, 1.0F, 0.9F};
		float	   fillPreviewAlpha = 0.15F;
		float	   bandPreviewAlpha = 0.2F;
		float	   lineWidth = 2.0F;
		float	   guideWidth = 1.5F;
		float	   vertexRadiusPx = 4.0F;
		float	   invalidVertexRadiusPx = 7.0F;
		float	   originHaloMinRadiusPx = 8.0F;

		GuideLineStyle guideLineStyle = GuideLineStyle::Dotted;
		float		   guideDashPx = 1.0F;
		float		   guideGapPx = 4.5F;
		float		   guideOpacity = 0.6F;
		float		   markerOpacity = 0.9F;
		GuideColorMode guideColorMode = GuideColorMode::One;
		StyleColor	   guideColorShape{0.549F, 0.769F, 1.0F, 1.0F};
		StyleColor	   guideColorNearby{1.0F, 0.678F, 0.361F, 1.0F};
		StyleColor	   guideColorWorld{0.498F, 0.878F, 0.651F, 1.0F};
		StyleColor	   guideColorLength{0.949F, 0.604F, 0.847F, 1.0F};
		int			   guideDistanceDecimals = 2;
		float		   guideLabelPx = 11.0F;
		int			   angleReadoutDecimals = 1;
		float		   rightAngleMarkerPx = 8.0F;
		float		   guideCrossingDotPx = 3.0F;
		float		   cursorRadiusPx = 6.0F;
	};

	/// The overlays over a parts item (rotatable-placed-items.md, sections 14.4 and 16), on the placement ghost and in
	/// the asset manager: use areas filled at useAreaFillAlpha with a solid edge, the clearance hatched (lines
	/// hatchSpacingMeters apart, thinned on screen to one at least hatchMinGapPx apart) with a dashed edge, each part's
	/// footprint, the collider, and the depth strips with their anchors. refusedTint multiplies the ghost's colours, and
	/// replaces the overlays', where placement is refused. Widths and the label size are logical pixels.
	struct ItemOverlayStyle {
		StyleColor useAreaColor{0.30F, 0.80F, 0.62F, 1.0F};
		StyleColor clearanceColor{0.86F, 0.84F, 0.78F, 1.0F};
		StyleColor footprintColor{0.85F, 0.55F, 0.95F, 1.0F};
		StyleColor colliderColor{0.0F, 0.9F, 1.0F, 1.0F};
		StyleColor stripColor{0.45F, 0.55F, 1.0F, 1.0F};
		StyleColor anchorColor{0.95F, 0.35F, 0.75F, 1.0F};
		StyleColor refusedTint{1.0F, 0.3F, 0.3F, 1.0F};
		StyleColor labelBacking{0.06F, 0.07F, 0.08F, 0.75F};
		float	   useAreaFillAlpha = 0.28F;
		float	   hatchAlpha = 0.6F;
		float	   footprintAlpha = 0.85F;
		float	   hatchSpacingMeters = 0.1F;
		float	   hatchMinGapPx = 4.0F;
		float	   edgeWidth = 1.5F;
		float	   hatchWidth = 1.0F;
		float	   footprintWidth = 1.0F;
		float	   colliderWidth = 2.0F;
		float	   dashPx = 6.0F;
		float	   gapPx = 4.0F;
		float	   labelPx = 12.0F;
		/// The reason beside a refused ghost, in refusedTint: its text scale (1 is 16 logical px), its offset from the
		/// pivot, and its shadow's offset, all times the interface size.
		float refusalTextScale = 0.8F;
		float refusalOffsetXPx = 14.0F;
		float refusalOffsetYPx = -22.0F;
		float refusalShadowPx = 1.0F;
	};

	/// All construction rendering style, loaded from rendering.xml.
	struct RenderingConfig {
		FoundationStyle	 foundation;
		WallStyle		 wall;
		OpeningStyle	 opening;
		PreviewStyle	 preview;
		ItemOverlayStyle itemOverlays;
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

		/// Load rendering.xml from path. Tolerant of a missing file: keeps the built-in
		/// defaults and still returns true, so the renderer always has a usable style.
		bool loadRendering(const std::string& xmlPath);

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
		[[nodiscard]] const RenderingConfig&  rendering() const;

		/// Enum tokens the last loadSnapping / loadRendering could not match.
		[[nodiscard]] const std::vector<ConfigParseError>& snappingParseErrors() const;
		[[nodiscard]] const std::vector<ConfigParseError>& renderingParseErrors() const;

		// --- Load state ---

		[[nodiscard]] bool materialsLoaded() const;
		[[nodiscard]] bool constraintsLoaded() const;
		[[nodiscard]] bool snappingLoaded() const;
		[[nodiscard]] bool renderingLoaded() const;

	  private:
		ConstructionRegistry() = default;

		/// Parse a hex RGBA string like "#C8915AFF" into a PatternColor.
		static PatternColor parseColor(const std::string& hex);

		/// Parse a hex RGBA string into a float StyleColor (parseColor / 255).
		static StyleColor parseStyleColor(const std::string& hex);

		/// Quantize a float meters value to int64 millimeters.
		static int64_t toMm(float meters);

		std::unordered_map<std::string, MaterialDef> materials;
		std::vector<OpeningTypeDef>					 openingTypeList; // load order, stable for determinism
		ConstraintConfig							 constraintConfig;
		SnappingConfig								 snappingConfig;
		RenderingConfig								 renderingConfig;
		std::vector<ConfigParseError>				 snappingErrors;
		std::vector<ConfigParseError>				 renderingErrors;

		bool hasMaterials = false;
		bool hasConstraints = false;
		bool hasSnapping = false;
		bool hasRendering = false;
	};

} // namespace assets

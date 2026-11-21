#pragma once

// A placed item defined as parts (<assetType>parts</assetType>; docs/technical/rendering/rotatable-placed-items.md,
// sections 4, 5, 6.1, 11.1): upright prisms with a rectangular or regular n-sided footprint, a bottom and a top
// height, colours and drawings, placed inside the item by offset and turn. Loading reads the XML (PartsAuthored),
// runs the optional parts script on it, then resolves it (PartsAsset): footprint polygons and faces in each part's
// frame, every drawing read once and mapped into metres, use areas and clearance as polygons, and the derived
// collider. Every length is in metres in the item's frame, +Y toward the item's front (down the screen at turn 0).

#include "assets/AssetDefinition.h"
#include "assets/AssetValidator.h"

#include <graphics/Color.h>
#include <vector/SVGLoader.h>

#include <glm/vec2.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pugi {
	class xml_node;
}

namespace assets {

	// ─── As written: the XML, which the parts script may add to ───────────────────────────────────────────

	struct AuthoredFootprint {
		enum class Shape : uint8_t { None, Rect, Round };
		Shape	 shape = Shape::None;
		uint32_t shapeCount = 0; ///< footprint shapes written; exactly one is valid
		float	 minX = 0.0F;
		float	 minY = 0.0F;
		float	 maxX = 0.0F;
		float	 maxY = 0.0F;
		float	 x = 0.0F;
		float	 y = 0.0F;
		float	 radius = 0.0F;
		float	 sides = 0.0F; ///< as written; a whole number from 3 to 32 is valid
	};

	struct AuthoredFace {
		std::vector<std::string> sides; ///< the face names one <face side="..."> lists
		std::string				 drawing;
	};

	struct AuthoredPart {
		std::string				  name;
		AuthoredFootprint		  footprint;
		bool					  hasHeight = false;
		float					  bottom = 0.0F;
		float					  top = 0.0F;
		std::string				  topColor;
		std::string				  topDrawing;
		bool					  hasSides = false;
		std::string				  sideColor;
		std::string				  sidesDrawing;
		std::vector<AuthoredFace> faces;
	};

	struct AuthoredPlacement {
		std::string part;
		float		x = 0.0F;
		float		y = 0.0F;
		float		turnDegrees = 0.0F;
	};

	struct AuthoredAreaShape {
		enum class Kind : uint8_t { Around, Circle, Rect, Drawing };
		Kind		kind = Kind::Rect;
		float		distanceMeters = 0.0F; ///< Around
		float		x = 0.0F;			   ///< Circle
		float		y = 0.0F;
		float		radius = 0.0F;
		float		minX = 0.0F; ///< Rect, and the rectangle a Drawing maps onto
		float		minY = 0.0F;
		float		maxX = 0.0F;
		float		maxY = 0.0F;
		std::string file; ///< Drawing
	};

	struct AuthoredArea {
		std::string					   name;
		std::vector<AuthoredAreaShape> shapes;
	};

	struct PartsAuthored {
		std::vector<AuthoredPart>		 parts;
		std::vector<AuthoredPlacement>	 placements;
		std::optional<std::vector<AuthoredArea>> useAreas;	///< unset: no <useAreas>, so the default one
		std::optional<AuthoredArea>				 clearance; ///< unset: no <clearance>
		std::string								 scriptPath; ///< <partsScript>, relative to the item's folder
	};

	// ─── Resolved: what the rest of the game reads ────────────────────────────────────────────────────────

	/// A side face: the footprint edge from `start` to `end` in the footprint's winding, in the part's frame.
	struct PartFace {
		glm::vec2				start{0.0F};
		glm::vec2				end{0.0F};
		glm::vec2				normal{0.0F}; ///< outward, unit length
		float					lengthMeters = 0.0F;
		std::optional<uint32_t> drawing;	  ///< into PartsAsset::drawings; unset is a plain face of the side colour
	};

	struct PartDefinition {
		std::string			   name;
		std::vector<glm::vec2> footprint; ///< in the part's frame, vertex angle increasing from +X toward +Y
		glm::vec2			   boundsMin{0.0F};
		glm::vec2			   boundsMax{0.0F};
		float				   bottomMeters = 0.0F;
		float				   topMeters = 0.0F;
		Foundation::Color	   topColor;
		std::optional<uint32_t> topDrawing; ///< covers the footprint's bounding box
		Foundation::Color	   sideColor;
		/// One a footprint edge, in winding order (a rect's back, right, front, left); none for a flat part.
		std::vector<PartFace> faces;

		/// A top only: no sides and no collision.
		[[nodiscard]] bool flat() const { return topMeters == bottomMeters; }
	};

	struct PartPlacement {
		uint32_t  part = 0; ///< into PartsAsset::parts
		glm::vec2 offsetMeters{0.0F};
		float	  turnRadians = 0.0F; ///< positive from +X toward +Y, about the part's own origin
	};

	struct ItemDrawingShape {
		renderer::LoadedSVGShape shape;					///< in metres over [0, size]
		float					 minPixelsPerMeter = 0.0F; ///< its effective data-lod
	};

	/// One drawing at the size it covers: its viewBox mapped onto [0, width] x [0, height] metres, its top
	/// left at the origin. A file covering two sizes is read once and mapped once for each.
	struct ItemDrawing {
		std::string					  file;
		glm::vec2					  sizeMeters{0.0F};
		std::vector<ItemDrawingShape> shapes;
		/// Its shapes' colours averaged by drawn area and alpha, opaque: what a face too narrow for its drawing shows.
		Foundation::Color averageColor;
	};

	struct AreaPolygon {
		std::vector<glm::vec2>				outer; ///< vertex angle increasing from +X toward +Y
		std::vector<std::vector<glm::vec2>> holes; ///< wound the other way
	};

	/// An area's shapes, unioned, in the item's frame. Empty polygons: no area.
	struct ItemArea {
		std::string				 name; ///< empty for an unnamed one
		std::vector<AreaPolygon> polygons;
	};

	struct PartsAsset {
		std::vector<PartDefinition> parts;
		std::vector<PartPlacement>	placements;
		std::vector<ItemDrawing>	drawings;
		std::vector<ItemArea>		useAreas;
		ItemArea					clearance;
		/// The tallest top among the parts with height, the height sight and sorting read; 0 for a flat-only item.
		float bodyTopMeters = 0.0F;
		/// Each placement's footprint in the item's frame (turn 0), in the winding of its part's; empty for a part
		/// whose footprint rule 3 refused.
		std::vector<std::vector<glm::vec2>> placedFootprints;
		/// Placements by placements, row-major: whether two footprints overlap in plan (rule 9), and for those that
		/// do, +1 when the row's draws after the column's (it is above), -1 when before, 0 when neither is above.
		std::vector<bool>	planOverlap;
		std::vector<int8_t> planOrder;

		[[nodiscard]] bool overlapsInPlan(size_t first, size_t second) const { return planOverlap[first * placements.size() + second]; }
	};

	/// The tunables loading reads (rotatable-placed-items.md, section 18).
	struct PartsLoadConfig {
		/// `drawings aspectTolerance`, `curveToleranceMeters`, and `overflowMeters` in the view config (ViewConfig.h).
		float aspectToleranceRelative = 0.0F;
		float curveToleranceMeters = 0.0F;
		float overflowMeters = 0.0F;
		/// The default use area's distance: `pathingClearanceMeters` in assets/config/construction/constraints.xml,
		/// which ConstructionRegistry reads after the definitions load, so its value is held here.
		float defaultUseDistanceMeters = 0.7F;
	};

	/// The largest colonist collider's radius: a use area has room when a disc this size fits in it.
	inline constexpr float kLargestColonistRadiusMeters = 0.3F;

	/// The def elements a parts def may not have (rule 1).
	inline constexpr std::array<const char*, 6> kElementsPartsRefuses = {"svgPath", "worldHeight", "variantCount", "placement", "collision", "generator"};

	/// A colour written `#RRGGBB`, opaque; nothing when it is written any other way.
	[[nodiscard]] std::optional<Foundation::Color> parseRgbHexColor(std::string_view text);

	/// The convex hull of points, vertex angle increasing from +X toward +Y (monotone chain).
	[[nodiscard]] std::vector<glm::vec2> convexHull(std::vector<glm::vec2> points);

	/// The y range a convex ring covers on the vertical line at `x`.
	[[nodiscard]] std::pair<float, float> ySpanAt(const std::vector<glm::vec2>& ring, float x);

	/// A placement's footprint turned with the item by `itemTurnRadians` (section 6.2), in the same winding.
	[[nodiscard]] std::vector<glm::vec2> turnedFootprint(const PartsAsset& asset, size_t placement, float itemTurnRadians);

	/// A parts def's reserved region (section 4.2): its collider unioned with its clearance, in the item's frame at
	/// turn 0. Empty for a def with neither.
	[[nodiscard]] std::vector<AreaPolygon> reservedRegion(const AssetDefinition& def);

	/// The pairs of the parts order (section 6.6) among placements whose footprints on the screen are
	/// `screenFootprints` (one a placement, turned with the item): entry i lists the placements that draw after
	/// placement i. Footprints that overlap in plan put the higher later; disjoint ones sharing a vertical screen line
	/// put the one further south later; the rest are unordered.
	[[nodiscard]] std::vector<std::vector<size_t>> partsDrawnAfter(const PartsAsset& asset, const std::vector<std::vector<glm::vec2>>& screenFootprints);

	/// The placements in the order they draw at a turn: a topological sort of partsDrawnAfter, ties broken by placement
	/// order. A cycle, which validation refuses (rule 9), leaves the rest in placement order.
	[[nodiscard]] std::vector<uint32_t> partsDrawOrder(const PartsAsset& asset, float itemTurnRadians);

	/// Reads a parts def's <parts>, <placements>, <useAreas>, <clearance>, and <partsScript>. What can't be read as
	/// written (a malformed number, an unknown element) is an error in `report`.
	[[nodiscard]] PartsAuthored readPartsXml(const pugi::xml_node& defNode, const std::string& defName, ValidationReport& report);

	/// Runs a parts def's <partsScript> on what the XML wrote: it may add parts, placements, and area shapes and
	/// change a part's footprint or heights. Returns the script's error, empty when it ran.
	[[nodiscard]] std::string runPartsScript(const std::filesystem::path& script, PartsAuthored& authored);

	struct ResolvedParts {
		PartsAsset					asset;
		std::vector<CollisionShape> colliders; ///< the derived collider's rings (section 11.1), each a Polygon
	};

	/// Resolves what was written into the item, loading each drawing from `folder`, and checks it: every rule of
	/// section 5 that the def and its drawings decide, all but the view size (12), is an error (rule 8 a warning) in `report` naming the def, the part, and the face, area, or file. The collider's
	/// rings are checked by the registry's collision admission (rule 10).
	[[nodiscard]] ResolvedParts resolveParts(
		const PartsAuthored& authored, const std::filesystem::path& folder, const std::string& defName, const PartsLoadConfig& config,
		ValidationReport& report
	);

} // namespace assets

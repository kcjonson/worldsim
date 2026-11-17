#pragma once

// Internal to SnapEngine: the guide step's stages (building-construction D12, Guides),
// in the order the engine runs them. Exposed so SnapGuides.test.cpp can read what a
// stage decided (how many corners were used, which nearby directions were kept, why a
// crossing was rejected). Not for use outside SnapEngine.cpp and the tests.
//
// Positions are double world meters; committed geometry is read as integer millimeters
// and converted exactly. The vectors in GuideStudy are cleared, never shrunk, so a study
// reused across calls stops allocating once it has seen its largest input.

#include "ConstructionWorld.h"
#include "DrawingAids.h"
#include "SnapEngine.h"

#include <core/Vec2d.h>
#include <core/Vec2i64.h>
#include <math/Types.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace assets {
	struct SnappingConfig;
}

namespace construction::snap_guides {

	using geometry::Vec2d;

	// A node lies on a line, or two points coincide, within this.
	inline constexpr double kCoincidenceMeters = 0.001;
	// Distances closer than this rank as a tie.
	inline constexpr double kDistanceTieMeters = 1e-9;

	// Added to the outer-face-flush inset on every edge. Without it the flush corner would
	// land the band's outer face right on the foundation edge, but the two mm roundings on a
	// diagonal edge (the inset point's quantize plus geometry::band's perpendicular llround)
	// can together push that corner ~1.4 mm outside the ring, tripping the validator's
	// containment check. Axis-aligned edges have no rounding, so the bias seats them a flat
	// 2 mm inside the edge; 2 mm is sub-pixel at any zoom.
	inline constexpr std::int64_t kFlushInsetSafetyMm = 2;

	/// The inward offset (meters) of an outer-face-flush wall of the given half-thickness.
	[[nodiscard]] double flushInsetMeters(std::int64_t halfThicknessMm);

	/// Integer millimeters as double meters, exactly.
	[[nodiscard]] Vec2d meters(const geometry::Vec2i64& millimeters);
	[[nodiscard]] Vec2d toVec2d(::Foundation::Vec2 point);

	enum class SnapTool { Foundation, Wall };

	/// What a committed structure is: a foundation or a single wall segment. A corner and
	/// a nearby direction share a structure when they name the same one.
	enum class StructureKind { Foundation, WallSegment };

	struct StructureKey {
		StructureKind kind = StructureKind::Foundation;
		std::uint64_t id = 0;

		bool operator==(const StructureKey&) const = default;
	};

	/// Everything one guide study reads.
	struct GuideContext {
		const assets::SnappingConfig*		   snapping = nullptr;
		const ConstructionWorld*			   world = nullptr;
		const std::vector<::Foundation::Vec2>* points = nullptr;
		Vec2d								   cursor;
		DrawingAids							   aids;
		SnapTool							   tool = SnapTool::Foundation;
		std::int64_t						   wallHalfThicknessMm = 0;
		// The wall tool's lines use points[0] and the most recent maxPoints - 1 points.
		int maxPoints = 32;
	};

	/// A committed corner within the guide range. Its structures and edge directions are
	/// ranges into GuideStudy::cornerStructures and GuideStudy::cornerEdgeDirections.
	struct GuideCorner {
		Vec2d		point;
		double		distanceMeters = 0.0;
		std::size_t order = 0; // foundations() then ring index, then vertices()
		std::size_t firstStructure = 0;
		std::size_t structureCount = 0;
		std::size_t firstEdgeDirection = 0;
		std::size_t edgeDirectionCount = 0;
	};

	/// A committed edge of a structure the cursor may be near: a foundation ring edge, or
	/// in the wall tool a wall segment's centerline.
	struct NearbyEdge {
		StructureKey structure;
		double		 angle = 0.0; // radians, modulo 180 degrees, in [0, pi)
		double		 distanceMeters = 0.0;
		std::size_t	 order = 0;
	};

	/// What the nearby direction walk decided for one edge in range.
	enum class NearbyEdgeStatus {
		Kept,		 // a new direction
		AxisAligned, // within the direction tolerance of the world axes; the world guides own it
		Duplicate,	 // within the direction tolerance of a direction already kept
		OverLimit,	 // a new direction past nearbyGuideDirectionLimit
	};

	struct NearbyWalkEntry {
		std::size_t		 edge = 0; // index into GuideStudy::edges
		NearbyEdgeStatus status = NearbyEdgeStatus::Kept;
	};

	/// A kept nearby direction, with the structures in range that have an edge along it
	/// (a range into GuideStudy::matchingStructures).
	struct NearbyDirection {
		double		 angle = 0.0;
		StructureKey supplier;
		std::size_t	 firstMatchingStructure = 0;
		std::size_t	 matchingStructureCount = 0;
	};

	enum class GuideNodeKind { ShapePoint, CommittedCorner };

	/// The point a line was generated through: a placed point (index into the points) or a
	/// committed corner (index into GuideStudy::corners).
	struct GuideNode {
		Vec2d		  point;
		GuideNodeKind kind = GuideNodeKind::ShapePoint;
		std::size_t	  index = 0;
	};

	enum class GuideLineStatus {
		Missed, // not captured
		Kept,	// captured and kept after the merge
		Merged, // captured, dropped into a kept line (mergedInto)
	};

	/// One line of D12's closed list. `id` is its generation order.
	struct GuideLine {
		std::size_t		id = 0;
		GuideSource		source = GuideSource::DrawnShape;
		GuideRelation	relation = GuideRelation::Parallel;
		GuideNode		node;
		double			angle = 0.0; // radians, modulo 180 degrees, in [0, pi)
		Vec2d			direction;
		double			distanceMeters = 0.0; // from the cursor
		Vec2d			foot;				  // of the cursor
		bool			capturedByDistance = false;
		bool			capturedByAngle = false;
		GuideLineStatus status = GuideLineStatus::Missed;
		std::size_t		mergedInto = 0; // index into lines, when Merged

		[[nodiscard]] bool captured() const { return capturedByDistance || capturedByAngle; }
	};

	/// Why a pair of kept lines does or does not give a crossing.
	enum class CrossingStatus {
		Kept,
		CrossingsOff,		  // the guideCrossings aid is off
		TooShallow,			  // they meet under the crossing minimum angle, or are parallel
		TooFar,				  // farther than guideCrossingRadiusMeters from the cursor
		SharedGeneratingNode, // both pass through a node one of them was generated through
	};

	struct GuideCrossingRecord {
		std::size_t	   firstLine = 0; // the earlier generated of the two, index into lines
		std::size_t	   secondLine = 0;
		Vec2d		   point;
		double		   distanceMeters = 0.0;
		CrossingStatus status = CrossingStatus::Kept;
	};

	/// A single kept line or a kept crossing, ranked against the others.
	struct GuideCandidate {
		bool		isCrossing = false;
		std::size_t bestLine = 0;  // index into lines; the higher-ranked line of a crossing
		std::size_t otherLine = 0; // a crossing's other line
		Vec2d		point;
		double		distanceMeters = 0.0;
		std::size_t firstGeneration = 0;
		std::size_t secondGeneration = 0; // a crossing's later line; unused for a single line
	};

	/// The guide step's working state for one cursor. Every stage clears what it fills.
	struct GuideStudy {
		std::vector<Vec2d>		 shapePoints;
		std::vector<std::size_t> shapeNodes; // indices into shapePoints that lines go through

		std::vector<GuideCorner>  corners; // in range, nearest first; the first cornersUsed are used
		std::size_t				  cornersUsed = 0;
		std::vector<StructureKey> cornerStructures;
		std::vector<Vec2d>		  cornerEdgeDirections;
		std::vector<SegmentId>	  vertexSegments; // scratch: one wall vertex's segments, sorted, while collecting corners

		std::vector<NearbyEdge>		 edges; // every non-degenerate edge of a structure not rejected by range
		std::vector<NearbyWalkEntry> walk;	// the edges in range, nearest first
		std::vector<StructureKey>	 structuresInRange;
		std::vector<NearbyDirection> directions;
		std::vector<StructureKey>	 matchingStructures;

		std::vector<GuideLine>			 lines;
		std::vector<std::size_t>		 captured; // indices into lines, in merge rank order
		std::vector<std::size_t>		 kept;	   // indices into lines, in merge rank order
		std::vector<GuideCrossingRecord> crossings;
		std::vector<GuideCandidate>		 candidates;
		std::optional<std::size_t>		 winner; // index into candidates
	};

	// --- Angles and lines -------------------------------------------------------

	/// `angle` folded into [0, pi).
	[[nodiscard]] double normalizeLineAngle(double angle);
	/// The smaller difference of two angles modulo `period`.
	[[nodiscard]] double angularDifference(double first, double second, double period);
	[[nodiscard]] double lineDistance(Vec2d point, const GuideLine& line);
	[[nodiscard]] Vec2d	 footOnLine(Vec2d point, const GuideLine& line);

	// --- The stages, in order ---------------------------------------------------

	/// The placed points as doubles, and which of them lines go through.
	void collectShapeNodes(const GuideContext& context, GuideStudy& study);

	/// Committed corners within guideRangeMeters, nearest first, capped at
	/// guideCornerLimit (only with alignToExistingBuildings). Foundation ring vertices,
	/// in the wall tool at their outer-face-flush inset, then in the wall tool wall
	/// vertices. A foundation whose bounds are out of range is skipped whole.
	void collectCorners(const GuideContext& context, GuideStudy& study);

	/// Nearby directions (only with alignToExistingBuildings): edges in range, nearest
	/// first, axis-aligned and duplicate directions skipped, the first
	/// nearbyGuideDirectionLimit kept, each with the structures in range along it.
	void collectNearbyDirections(const GuideContext& context, GuideStudy& study);

	/// The closed line list, as each source's aid allows.
	void generateLines(const GuideContext& context, GuideStudy& study);

	/// Distance capture for every line; angle capture for shape lines through the
	/// previous or first point.
	void captureLines(const GuideContext& context, GuideStudy& study);

	/// Near-duplicate captured lines dropped into the higher-ranked one.
	void mergeLines(const GuideContext& context, GuideStudy& study);

	/// Crossings among the kept lines, each with its status.
	void formCrossings(const GuideContext& context, GuideStudy& study);

	/// Kept lines and kept crossings ranked by the priority rule; sets the winner.
	void rankCandidates(const GuideContext& context, GuideStudy& study);

	/// Every stage above.
	void studyGuides(const GuideContext& context, GuideStudy& study);

	// --- Using the study ----------------------------------------------------------

	struct SquaredPoint {
		Vec2d		point;
		std::size_t line = 0; // index into lines
	};

	/// Squaring on an existing edge from `start` to `end`: the nearest crossing, within
	/// edgeSnapRadiusMeters of the cursor, of a captured shape line with the edge, at
	/// the crossing minimum angle or more, on the edge (strictly inside it when
	/// `strictInterior`). Generation order breaks ties.
	[[nodiscard]] std::optional<SquaredPoint>
	squareOntoEdge(const GuideContext& context, const GuideStudy& study, Vec2d start, Vec2d end, bool strictInterior);

	/// The guide a result draws for `line`, from its node to `to`, with its right-angle
	/// marker.
	[[nodiscard]] SnapGuide makeGuide(const GuideContext& context, const GuideStudy& study, const GuideLine& line, Vec2d to);

} // namespace construction::snap_guides

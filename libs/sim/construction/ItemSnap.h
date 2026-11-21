#pragma once

// Snapping for placing an item (rotatable-placed-items 14.3): the turn snaps square to a nearby wall face or
// foundation edge, else to the angle step, and the position slides flush against a wall face or another item's
// reserved region. Alt drops the turn snaps and keeps the slide, as the wall tools keep their geometry snaps
// under Alt (SnapEngine.h). Pure logic over the construction world and the drawing tools' SnappingConfig, so it
// is tested without the app. Turns are whole quanta (assets::kTurnQuantumDegrees), degrees clockwise on the
// screen, the front down the screen at 0.

#include "ConstructionWorld.h"

#include <assets/ConstructionRegistry.h>

#include <math/Types.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace construction {

	/// A straight edge an item may slide flush against, with the unit normal pointing out of what it bounds.
	struct FlushFace {
		::Foundation::Vec2 start{0.0F, 0.0F};
		::Foundation::Vec2 end{0.0F, 0.0F};
		::Foundation::Vec2 outwardNormal{0.0F, 0.0F};
	};

	/// The direction, in quanta, of the wall face or foundation edge (blueprint or built) nearest `pivot` within
	/// itemAlignRangeMeters: the edge's run from its first point to its second, degrees clockwise from +X. Nothing
	/// when none is in range.
	[[nodiscard]] std::optional<uint32_t>
	itemAlignmentQuanta(const ConstructionWorld& world, ::Foundation::Vec2 pivot, const assets::SnappingConfig& snapping);

	/// A raw turn snapped: to the alignment direction or one of its quarter turns when within the angle catch
	/// window of one, else to the nearest multiple of the angle step. Under `freeform` it is the raw turn, rounded
	/// to the quantum.
	[[nodiscard]] uint32_t
	snapItemTurn(double rawDegrees, std::optional<uint32_t> alignmentQuanta, const assets::SnappingConfig& snapping, bool freeform);

	/// Q (`direction` -1, counter-clockwise) or E (+1, clockwise): the next stop past `turnQuanta`, the stops being
	/// the multiples of the angle step and the alignment's four quarter turns. Under `freeform` it is one degree.
	[[nodiscard]] uint32_t stepItemTurn(
		uint32_t					  turnQuanta,
		int							  direction,
		std::optional<uint32_t>		  alignmentQuanta,
		const assets::SnappingConfig& snapping,
		bool						  freeform
	);

	/// The raw turn, in degrees, whose front points from `pivot` toward `pointer`: atan2(-dx, dy).
	[[nodiscard]] double turnDegreesToward(::Foundation::Vec2 pivot, ::Foundation::Vec2 pointer);

	/// Both faces of every wall segment, blueprint or built, whose centreline passes within `rangeMeters` of
	/// `point`, each at its half thickness off the centreline, facing away from it.
	[[nodiscard]] std::vector<FlushFace> wallFaces(const ConstructionWorld& world, ::Foundation::Vec2 point, float rangeMeters);

	/// The edges of a ring (either winding) as faces facing out of it.
	void appendRingFaces(const std::vector<::Foundation::Vec2>& ring, std::vector<FlushFace>& faces);

	/// The flush slide: the move along a face's normal that puts the nearer end of an edge of `reservedRings` (the
	/// item's reserved region in the world, either winding) on that face, for an edge parallel to the face within
	/// guideDirectionToleranceDegrees, facing it, overlapping it along its run, and with that end within
	/// edgeSnapRadiusMeters of it. The smallest such move wins; zero when no edge qualifies.
	[[nodiscard]] ::Foundation::Vec2 flushSlide(
		const std::vector<std::vector<::Foundation::Vec2>>& reservedRings,
		const std::vector<FlushFace>&						faces,
		const assets::SnappingConfig&						snapping
	);

} // namespace construction

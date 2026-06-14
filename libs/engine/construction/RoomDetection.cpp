#include "RoomDetection.h"

#include <arrangement/Arrangement.h>
#include <arrangement/HalfEdge.h>
#include <core/Int128.h>

#include <cmath>

namespace engine::construction {

	std::vector<RoomFace> detectRooms(const ConstructionWorld& world) {
		// Gather built wall centerlines as arrangement input, tagged with the wall
		// SegmentId so extracted faces carry it back as provenance. Openings
		// (doors/windows) do NOT split a segment -- a doored wall is still one
		// centerline -- so whole segments enclose; no opening handling is needed.
		std::vector<geometry::InputSegment> inputs;
		inputs.reserve(world.segments().size());
		for (const WallSegment& seg : world.segments()) {
			if (seg.state != FoundationState::Built) {
				continue;
			}
			const Vertex* v0 = world.getVertex(seg.v0);
			const Vertex* v1 = world.getVertex(seg.v1);
			if (v0 == nullptr || v1 == nullptr) {
				continue;
			}
			// SegmentId is uint64 but InputSegment::index is int64. SegmentIds are
			// allocated from a counter starting at 1 and never approach INT64_MAX, so
			// this narrowing is value-preserving and round-trips exactly back to a
			// SegmentId via the provenance below.
			inputs.push_back({v0->pos, v1->pos, static_cast<std::int64_t>(seg.id)});
		}

		std::vector<RoomFace> rooms;
		if (inputs.empty()) {
			return rooms;
		}

		const geometry::Arrangement	 arrangement = geometry::buildArrangement(inputs);
		const geometry::HalfEdgeMesh mesh = geometry::extractFaces(arrangement);

		for (const geometry::Face& face : mesh.faces) {
			if (face.outer || !face.representativePoint.has_value()) {
				continue; // skip the unbounded face and any degenerate bounded one
			}

			RoomFace room;
			// signedAreaDoubled is 2x area in mm^2; /2 for area, /1e6 for mm^2 -> m^2.
			// Bounded faces are CCW (sign >= 0 -- that is how `outer` is defined), so
			// the exact doubled area is non-negative and serves as a deterministic
			// integer key for identity tiebreaks; the float `area` is display-only.
			room.areaDoubled = face.signedAreaDoubled;
			room.area = static_cast<float>(std::fabs(face.signedAreaDoubled.toDouble()) / 2.0e6);
			room.representativePoint = *face.representativePoint;

			room.ring.reserve(face.halfEdges.size());
			for (std::size_t he : face.halfEdges) {
				room.ring.push_back(mesh.vertices[mesh.halfEdges[he].origin]);
			}

			room.boundingSegmentIds.reserve(face.provenance.size());
			for (std::int64_t p : face.provenance) {
				room.boundingSegmentIds.push_back(static_cast<SegmentId>(p));
			}

			rooms.push_back(std::move(room));
		}

		return rooms;
	}

} // namespace engine::construction

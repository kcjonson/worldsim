#include "RoomDetection.h"

#include <arrangement/Arrangement.h>
#include <arrangement/HalfEdge.h>
#include <core/Int128.h>
#include <core/IntegerDivision.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <utility>

namespace construction {

	namespace {

		geometry::Ring faceRing(const geometry::HalfEdgeMesh& mesh, const geometry::Face& face) {
			geometry::Ring ring;
			ring.reserve(face.halfEdges.size());
			for (std::size_t he : face.halfEdges) {
				ring.push_back(mesh.vertices[mesh.halfEdges[he].origin]);
			}
			return ring;
		}

		// A point strictly inside `ring` and outside every hole, found on horizontal
		// lines halfway between consecutive distinct vertex heights (no vertex lies on
		// them, so every edge they meet is a proper crossing). Crossings pair up into
		// inside spans; the midpoint of the widest span that the exact query confirms
		// wins, the lowest line and then the leftmost span taking ties.
		std::optional<geometry::Vec2i64> pointInsideRoom(const geometry::Ring& ring, const std::vector<geometry::Ring>& holes) {
			std::vector<const geometry::Ring*> boundaries;
			boundaries.reserve(holes.size() + 1);
			boundaries.push_back(&ring);
			for (const geometry::Ring& hole : holes) {
				boundaries.push_back(&hole);
			}

			std::vector<std::int64_t> heights;
			for (const geometry::Ring* boundary : boundaries) {
				for (const geometry::Vec2i64& v : *boundary) {
					heights.push_back(v.y);
				}
			}
			std::sort(heights.begin(), heights.end());
			heights.erase(std::unique(heights.begin(), heights.end()), heights.end());

			std::optional<geometry::Vec2i64> best;
			std::int64_t					 bestWidth = 0;
			std::vector<std::int64_t>		 crossings;
			for (std::size_t i = 0; i + 1 < heights.size(); ++i) {
				if (heights[i + 1] - heights[i] < 2) {
					continue;
				}
				const std::int64_t y = heights[i] + (heights[i + 1] - heights[i]) / 2;

				crossings.clear();
				for (const geometry::Ring* boundary : boundaries) {
					const std::size_t n = boundary->size();
					for (std::size_t k = 0; k < n; ++k) {
						geometry::Vec2i64 low = (*boundary)[k];
						geometry::Vec2i64 high = (*boundary)[(k + 1) % n];
						if ((low.y < y) == (high.y < y)) {
							continue;
						}
						if (low.y > high.y) {
							std::swap(low, high);
						}
						// Both factors are bounded by one edge's extent, so the product fits.
						crossings.push_back(low.x + geometry::floorDiv((y - low.y) * (high.x - low.x), high.y - low.y));
					}
				}
				std::sort(crossings.begin(), crossings.end());

				for (std::size_t k = 0; k + 1 < crossings.size(); k += 2) {
					const std::int64_t width = crossings[k + 1] - crossings[k];
					if (width <= bestWidth) {
						continue;
					}
					const geometry::Vec2i64 candidate{crossings[k] + width / 2, y};
					if (pointInRoom(candidate, ring, holes) == geometry::PointInPolygon::Inside) {
						best = candidate;
						bestWidth = width;
					}
				}
			}
			return best;
		}

	} // namespace

	geometry::PointInPolygon pointInRoom(const geometry::Vec2i64&			point,
										 const geometry::Ring&				ring,
										 const std::vector<geometry::Ring>& holes) {
		const geometry::PointInPolygon outer = geometry::pointInPolygon(point, ring);
		if (outer != geometry::PointInPolygon::Inside) {
			return outer;
		}
		for (const geometry::Ring& hole : holes) {
			switch (geometry::pointInPolygon(point, hole)) {
				case geometry::PointInPolygon::Inside:
					return geometry::PointInPolygon::Outside;
				case geometry::PointInPolygon::OnBoundary:
					return geometry::PointInPolygon::OnBoundary;
				case geometry::PointInPolygon::Outside:
					break;
			}
		}
		return geometry::PointInPolygon::Inside;
	}

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
				continue; // skip outer boundary cycles and any degenerate bounded face
			}

			RoomFace room;
			// Bounded faces are CCW (sign >= 0 -- that is how `outer` is defined), so
			// the exact doubled area is non-negative; holes are subtracted below.
			room.areaDoubled = face.signedAreaDoubled;
			room.representativePoint = *face.representativePoint;
			room.ring = faceRing(mesh, face);

			room.boundingSegmentIds.reserve(face.provenance.size());
			for (std::int64_t p : face.provenance) {
				room.boundingSegmentIds.push_back(static_cast<SegmentId>(p));
			}

			rooms.push_back(std::move(room));
		}

		// Each wall group's outward cycle winds CW. One standing free inside a room is
		// a hole of the smallest room whose ring strictly contains it; its vertices
		// lie on its own group's rings, so only another group's room can contain one.
		// Areas are compared before any hole is subtracted.
		std::vector<std::pair<std::size_t, geometry::Ring>> holeParents;
		for (const geometry::Face& face : mesh.faces) {
			if (!face.outer) {
				continue;
			}
			geometry::Ring			hole = faceRing(mesh, face);
			const geometry::Vec2i64 probe = hole.front();
			std::size_t				parent = std::numeric_limits<std::size_t>::max();
			for (std::size_t ri = 0; ri < rooms.size(); ++ri) {
				if (geometry::pointInPolygon(probe, rooms[ri].ring) != geometry::PointInPolygon::Inside) {
					continue;
				}
				if (parent == std::numeric_limits<std::size_t>::max() || rooms[ri].areaDoubled < rooms[parent].areaDoubled) {
					parent = ri;
				}
			}
			if (parent != std::numeric_limits<std::size_t>::max()) {
				holeParents.emplace_back(parent, std::move(hole));
			}
		}
		for (auto& [parent, hole] : holeParents) {
			rooms[parent].areaDoubled = rooms[parent].areaDoubled + geometry::signedAreaDoubled(hole);
			rooms[parent].holes.push_back(std::move(hole));
		}

		for (RoomFace& room : rooms) {
			// areaDoubled is 2x area in mm^2; /2 for area, /1e6 for mm^2 -> m^2.
			room.area = static_cast<float>(std::fabs(room.areaDoubled.toDouble()) / 2.0e6);
			if (room.holes.empty() ||
				pointInRoom(room.representativePoint, room.ring, room.holes) == geometry::PointInPolygon::Inside) {
				continue;
			}
			// When the scan finds nothing the face's own point stays, unverified: it may lie in a hole.
			if (const std::optional<geometry::Vec2i64> point = pointInsideRoom(room.ring, room.holes)) {
				room.representativePoint = *point;
			}
		}

		return rooms;
	}

} // namespace construction

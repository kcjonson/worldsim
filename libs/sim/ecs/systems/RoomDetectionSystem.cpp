#include "RoomDetectionSystem.h"

#include "../World.h"
#include "../components/Room.h"
#include "../components/Structure.h"
#include "../components/Transform.h"

#include <construction/ConstructionWorld.h>

#include <core/Vec2i64.h>
#include <predicates/Predicates.h>

#include <cstddef>
#include <limits>

namespace ecs {

	namespace {

		// The same closed vertex cycle in the same direction, whatever vertex each
		// ring starts at. A ring can visit a vertex twice (two rooms touching at a
		// corner), so every alignment of the first vertex is tried.
		bool sameCycle(const geometry::Ring& a, const geometry::Ring& b) {
			const std::size_t n = a.size();
			if (n == 0 || n != b.size()) {
				return false;
			}
			for (std::size_t offset = 0; offset < n; ++offset) {
				if (b[offset] != a[0]) {
					continue;
				}
				std::size_t k = 1;
				while (k < n && a[k] == b[(offset + k) % n]) {
					++k;
				}
				if (k == n) {
					return true;
				}
			}
			return false;
		}

		// Every point of `inner`, edges included, lies strictly inside `outer`: all its
		// vertices are Inside, and no edge of one meets an edge of the other.
		bool ringStrictlyInside(const geometry::Ring& inner, const geometry::Ring& outer) {
			for (const geometry::Vec2i64& v : inner) {
				if (geometry::pointInPolygon(v, outer) != geometry::PointInPolygon::Inside) {
					return false;
				}
			}
			const std::size_t innerCount = inner.size();
			const std::size_t outerCount = outer.size();
			for (std::size_t i = 0; i < innerCount; ++i) {
				for (std::size_t k = 0; k < outerCount; ++k) {
					const geometry::SegmentIntersection meet =
						geometry::intersectSegments(inner[i], inner[(i + 1) % innerCount], outer[k], outer[(k + 1) % outerCount]);
					if (meet.relation != geometry::SegmentRelation::Disjoint) {
						return false;
					}
				}
			}
			return true;
		}

	} // namespace

	void RoomDetectionSystem::update(float /*deltaTime*/) {
		if (constructionWorld == nullptr) {
			return;
		}
		const uint64_t version = constructionWorld->version();
		if (seenVersion && version == lastVersion) {
			return; // topology unchanged since last reconcile; nothing to do
		}
		lastVersion = version;
		seenVersion = true;
		reconcile();
	}

	void RoomDetectionSystem::reconcile() {
		const std::vector<construction::RoomFace> faces = construction::detectRooms(*constructionWorld);

		// Identity matching (D6 "names survive edits"), each existing record inherited
		// by at most one face. First, a face whose outer ring is a record's stored
		// ring (the same vertex cycle) keeps that record, so building or removing a
		// room inside another never moves the outer room's id or name. Then a face
		// inherits the record whose stored representative point lies in the face
		// (inside its ring and outside its holes, exact integer tests); a face that
		// contains several old reps (a merge) inherits the dominant one (largest
		// stored area, tiebreak lowest roomId). That runs strict-interior first,
		// then as a fallback for a rep that lands exactly on a new wall -- a divider
		// drawn through the stored point reports OnBoundary, not Inside, for both
		// halves, so without the fallback both halves would reset identity. The
		// fallback gives the rep to the first face (canonical order) that still has
		// no inheritor, so exactly one side keeps the id/name.
		//
		// Neither point pass gives a record to a face whose ring lies strictly inside
		// the record's stored ring: that face is a room built inside the record's
		// room, never its successor, even when it was built round the stored point.
		// A record whose point such a face took from it goes, last, to the largest
		// unmatched face around it (one not strictly inside its ring whose own point
		// lies in the record's stored room), so the outer room keeps its id/name when
		// its ring changes in the same edit that builds the inner room.
		//
		// Limitation: if a room's shape changes enough that its old rep falls outside
		// every new face, identity resets.
		const std::size_t		 faceCount = faces.size();
		std::vector<std::size_t> inheritedRecord(faceCount, std::numeric_limits<std::size_t>::max());
		std::vector<bool>		 recordClaimed(roomRecords.size(), false);

		for (std::size_t fi = 0; fi < faceCount; ++fi) {
			for (std::size_t ri = 0; ri < roomRecords.size(); ++ri) {
				if (!recordClaimed[ri] && sameCycle(faces[fi].ring, roomRecords[ri].ring)) {
					inheritedRecord[fi] = ri;
					recordClaimed[ri] = true;
					break;
				}
			}
		}

		// Claim, for face fi, the dominant unclaimed record whose rep relates to the
		// face as `want` (Inside on the first pass, OnBoundary on the fallback).
		const auto claimBest = [&](std::size_t fi, geometry::PointInPolygon want) {
			const construction::RoomFace&		  face = faces[fi];
			std::size_t							  best = std::numeric_limits<std::size_t>::max();
			geometry::Int128					  bestArea; // exact, so the tiebreak is deterministic
			uint64_t							  bestId = 0;
			for (std::size_t ri = 0; ri < roomRecords.size(); ++ri) {
				if (recordClaimed[ri]) {
					continue;
				}
				const RoomRecord& rec = roomRecords[ri];
				if (construction::pointInRoom(rec.rep, face.ring, face.holes) != want || ringStrictlyInside(face.ring, rec.ring)) {
					continue;
				}
				if (best == std::numeric_limits<std::size_t>::max() || rec.areaDoubled > bestArea ||
					(rec.areaDoubled == bestArea && rec.roomId < bestId)) {
					best = ri;
					bestArea = rec.areaDoubled;
					bestId = rec.roomId;
				}
			}
			if (best != std::numeric_limits<std::size_t>::max()) {
				inheritedRecord[fi] = best;
				recordClaimed[best] = true;
			}
		};

		for (std::size_t fi = 0; fi < faceCount; ++fi) {
			if (inheritedRecord[fi] == std::numeric_limits<std::size_t>::max()) {
				claimBest(fi, geometry::PointInPolygon::Inside);
			}
		}
		for (std::size_t fi = 0; fi < faceCount; ++fi) {
			if (inheritedRecord[fi] == std::numeric_limits<std::size_t>::max()) {
				claimBest(fi, geometry::PointInPolygon::OnBoundary);
			}
		}

		for (std::size_t ri = 0; ri < roomRecords.size(); ++ri) {
			const RoomRecord& rec = roomRecords[ri];
			if (recordClaimed[ri]) {
				continue;
			}
			bool pointTakenByInnerRoom = false;
			for (std::size_t fi = 0; fi < faceCount && !pointTakenByInnerRoom; ++fi) {
				pointTakenByInnerRoom = geometry::pointInPolygon(rec.rep, faces[fi].ring) != geometry::PointInPolygon::Outside &&
										ringStrictlyInside(faces[fi].ring, rec.ring);
			}
			if (!pointTakenByInnerRoom) {
				continue;
			}
			std::size_t best = std::numeric_limits<std::size_t>::max();
			for (std::size_t fi = 0; fi < faceCount; ++fi) {
				if (inheritedRecord[fi] != std::numeric_limits<std::size_t>::max() ||
					(best != std::numeric_limits<std::size_t>::max() && faces[fi].areaDoubled <= faces[best].areaDoubled)) {
					continue;
				}
				if (construction::pointInRoom(faces[fi].representativePoint, rec.ring, rec.holes) == geometry::PointInPolygon::Inside &&
					!ringStrictlyInside(faces[fi].ring, rec.ring)) {
					best = fi;
				}
			}
			if (best != std::numeric_limits<std::size_t>::max()) {
				inheritedRecord[best] = ri;
				recordClaimed[ri] = true;
			}
		}

		// Build the reconciled record set in new-face order so roomId/name assignment
		// is deterministic. Inherited faces keep their record's id/name/entity and
		// refresh geometry; new faces allocate id/name and spawn an entity.
		std::vector<RoomRecord> next;
		next.reserve(faceCount);

		for (std::size_t fi = 0; fi < faceCount; ++fi) {
			const construction::RoomFace& face = faces[fi];

			if (inheritedRecord[fi] != std::numeric_limits<std::size_t>::max()) {
				RoomRecord rec = roomRecords[inheritedRecord[fi]]; // keep id/name/entity
				rec.ring = face.ring;
				rec.holes = face.holes;
				rec.rep = face.representativePoint;
				rec.area = face.area;
				rec.areaDoubled = face.areaDoubled;

				if (Room* room = world->getComponent<Room>(rec.entity)) {
					room->area = face.area;
					room->boundingSegmentIds = face.boundingSegmentIds;
				}
				if (Position* pos = world->getComponent<Position>(rec.entity)) {
					pos->value = geometry::dequantize(face.representativePoint);
				}

				next.push_back(std::move(rec));
				continue;
			}

			RoomRecord rec;
			rec.roomId = nextRoomId++;
			rec.name = "Room " + std::to_string(++nameCounter);
			rec.ring = face.ring;
			rec.holes = face.holes;
			rec.rep = face.representativePoint;
			rec.area = face.area;
			rec.areaDoubled = face.areaDoubled;
			rec.entity = world->createEntity();

			world->addComponent<Position>(rec.entity, Position{geometry::dequantize(face.representativePoint)});
			world->addComponent<Structure>(rec.entity, Structure{StructureKind::Room, rec.roomId});
			world->addComponent<Room>(rec.entity, Room{rec.roomId, face.area, face.boundingSegmentIds, rec.name});

			// Capture the entity before moving rec into the vector (avoids copying the
			// record's ring/name on every newly formed room).
			const EntityID entity = rec.entity;
			next.push_back(std::move(rec));

			if (onRoomFormed) {
				onRoomFormed(entity);
			}
		}

		// Records no face inherited were unmade (wall demolished / loop broken):
		// destroy their entities.
		for (std::size_t ri = 0; ri < roomRecords.size(); ++ri) {
			if (!recordClaimed[ri]) {
				world->destroyEntity(roomRecords[ri].entity);
			}
		}

		roomRecords = std::move(next);
	}

} // namespace ecs

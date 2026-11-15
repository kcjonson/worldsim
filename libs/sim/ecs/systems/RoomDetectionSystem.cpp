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
				if (construction::pointInRoom(roomRecords[ri].rep, face.ring, face.holes) != want) {
					continue;
				}
				const RoomRecord& rec = roomRecords[ri];
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

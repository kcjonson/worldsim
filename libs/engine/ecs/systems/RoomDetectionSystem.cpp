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

	void RoomDetectionSystem::update(float /*deltaTime*/) {
		if (m_world == nullptr) {
			return;
		}
		const uint64_t version = m_world->version();
		if (m_seenVersion && version == m_lastVersion) {
			return; // topology unchanged since last reconcile; nothing to do
		}
		m_lastVersion = version;
		m_seenVersion = true;
		reconcile();
	}

	void RoomDetectionSystem::reconcile() {
		const std::vector<engine::construction::RoomFace> faces = engine::construction::detectRooms(*m_world);

		// Identity matching (D6 "names survive edits"). A new face inherits the
		// existing record whose stored representative point lies in the new face's
		// ring (exact integer point-in-polygon); a face that contains several old
		// reps (a merge) inherits the dominant one (largest stored area, tiebreak
		// lowest roomId), and each existing record is inherited by at most one face.
		// Two passes: first strict-interior (the common case), then a fallback for a
		// rep that lands exactly on a new wall -- a divider drawn through the stored
		// point reports OnBoundary, not Inside, for both halves, so without the
		// fallback both halves would reset identity. The fallback gives the rep to
		// the first face (canonical order) that still has no inheritor, so exactly
		// one side keeps the id/name.
		//
		// Limitations (acceptable for v1; the rooms overlay that would surface them
		// ships later): if a room's shape changes enough that its old rep falls
		// outside every new face, identity resets; and a room fully nested inside
		// another with no connecting wall (a loop inside a loop) is mislabeled,
		// because extractFaces represents a face-with-a-hole by its outer cycle only
		// (the hole is a separate component), so the enclosing ring/area/rep don't
		// account for the inner room. Correct nested-room identity needs hole-aware
		// face extraction; deferred until the overlay consumes it.
		const std::size_t		 faceCount = faces.size();
		std::vector<std::size_t> inheritedRecord(faceCount, std::numeric_limits<std::size_t>::max());
		std::vector<bool>		 recordClaimed(m_rooms.size(), false);

		// Claim, for face fi, the dominant unclaimed record whose rep relates to the
		// face ring as `want` (Inside on the first pass, OnBoundary on the fallback).
		const auto claimBest = [&](std::size_t fi, geometry::PointInPolygon want) {
			const engine::construction::RoomFace& face = faces[fi];
			std::size_t							  best = std::numeric_limits<std::size_t>::max();
			geometry::Int128					  bestArea; // exact, so the tiebreak is deterministic
			uint64_t							  bestId = 0;
			for (std::size_t ri = 0; ri < m_rooms.size(); ++ri) {
				if (recordClaimed[ri]) {
					continue;
				}
				if (geometry::pointInPolygon(m_rooms[ri].rep, face.ring) != want) {
					continue;
				}
				const RoomRecord& rec = m_rooms[ri];
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
			claimBest(fi, geometry::PointInPolygon::Inside);
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
			const engine::construction::RoomFace& face = faces[fi];

			if (inheritedRecord[fi] != std::numeric_limits<std::size_t>::max()) {
				RoomRecord rec = m_rooms[inheritedRecord[fi]]; // keep id/name/entity
				rec.ring = face.ring;
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
			rec.roomId = m_nextRoomId++;
			rec.name = "Room " + std::to_string(++m_nameCounter);
			rec.ring = face.ring;
			rec.rep = face.representativePoint;
			rec.area = face.area;
			rec.areaDoubled = face.areaDoubled;
			rec.entity = world->createEntity();

			world->addComponent<Position>(rec.entity, Position{geometry::dequantize(face.representativePoint)});
			world->addComponent<Structure>(rec.entity, Structure{StructureKind::Room, rec.roomId});
			world->addComponent<Room>(rec.entity, Room{rec.roomId, face.area, face.boundingSegmentIds, rec.name});

			next.push_back(rec);

			if (m_onRoomFormed) {
				m_onRoomFormed(rec.entity);
			}
		}

		// Records no face inherited were unmade (wall demolished / loop broken):
		// destroy their entities.
		for (std::size_t ri = 0; ri < m_rooms.size(); ++ri) {
			if (!recordClaimed[ri]) {
				world->destroyEntity(m_rooms[ri].entity);
			}
		}

		m_rooms = std::move(next);
	}

} // namespace ecs

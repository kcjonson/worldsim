#pragma once

// RoomDetectionSystem - derives room entities from the built-wall graph and keeps
// their identity stable across edits (building-construction D6).
//
// Each tick it cheaply polls ConstructionWorld::version(); when topology changed
// it re-runs detectRooms() and reconciles the result against its persistent room
// records: matching new faces to existing rooms by representative-point
// containment (so names survive edits), spawning entities for genuinely new
// rooms, updating moved ones, and destroying records whose face is gone. A newly
// formed room fires the room-formed callback (the toast seam; the engine layer
// never touches UI directly).
//
// All decisions are exact-integer and operate over vectors in stable order, so
// roomId/name assignment is deterministic across platforms (multiplayer).
//
// Priority 59: ConstructionWorld's Built flip happens via the app's completion
// callback (outside any system's update), so this only needs to observe the
// version bump on a later frame; running just after ConstructionSystem (58) keeps
// it grouped with the other construction work without depending on intra-frame
// ordering.

#include "../EntityID.h"
#include "../ISystem.h"

#include <construction/RoomDetection.h>

#include <core/Vec2i64.h>
#include <polygon/Polygon.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace engine::construction {
	class ConstructionWorld;
}

namespace ecs {

	class RoomDetectionSystem : public ISystem {
	  public:
		RoomDetectionSystem() = default;

		void update(float deltaTime) override;

		[[nodiscard]] int		  priority() const override { return 59; }
		[[nodiscard]] const char* name() const override { return "RoomDetection"; }

		/// Inject the app-owned topology store. Called once from GameScene, the same
		/// place ConstructionSystem is wired.
		void setConstructionWorld(engine::construction::ConstructionWorld* world) { m_world = world; }

		/// Fired once when a room is newly formed, with its ECS entity. GameScene
		/// wires this to a "Room formed" toast (the cross-layer seam; the engine lib
		/// must not include UI).
		using RoomFormedCallback = std::function<void(EntityID room)>;
		void setRoomFormedCallback(RoomFormedCallback callback) { m_onRoomFormed = std::move(callback); }

		/// A persistent room: its identity (id/name) and the geometry that lets the
		/// next reconcile recognize it. `entity` is the ECS mirror.
		struct RoomRecord {
			uint64_t		  roomId = 0;
			std::string		  name;
			geometry::Ring	  ring;
			geometry::Vec2i64 rep;
			float			  area = 0.0f;
			EntityID		  entity = kInvalidEntity;
		};

		/// Current room records (test/inspection helper).
		[[nodiscard]] const std::vector<RoomRecord>& rooms() const { return m_rooms; }

	  private:
		// Re-detect rooms and reconcile against m_rooms: match, update, spawn, retire.
		void reconcile();

		engine::construction::ConstructionWorld* m_world = nullptr;
		RoomFormedCallback						 m_onRoomFormed = nullptr;

		std::vector<RoomRecord> m_rooms;

		uint64_t m_nextRoomId = 1;		// monotonic; reused ids would alias old entities
		uint64_t m_nameCounter = 0;		// monotonic; "Room N" names never repeat
		uint64_t m_lastVersion = 0;		// last ConstructionWorld version reconciled
		bool	 m_seenVersion = false; // false until the first reconcile runs
	};

} // namespace ecs

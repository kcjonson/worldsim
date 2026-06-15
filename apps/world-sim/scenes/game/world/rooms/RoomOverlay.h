#pragma once

// RoomOverlay - scene-owned world-space overlay that makes detected rooms
// visible. Rooms are derived by RoomDetectionSystem from the built-wall graph
// (they spawn ECS entities but render nothing on their own). When toggled on,
// this draws a translucent tint, an edge outline, and the room name for every
// record the system currently holds.
//
// Mirrors SelectionSystem/DrawingSystem: a plain class holding ecs::World*, the
// WorldCamera*, and the source system, calling Renderer::Primitives directly and
// projecting world->screen via camera->worldToScreen at kPixelsPerMeter. Off by
// default; GameScene flips it with a hotkey.
//
// Z sits above foundation fills (~50-52) and below wall bands (~60-64): tint 56,
// outline 57, label 58.

#include <ecs/systems/RoomDetectionSystem.h>
#include <world/camera/WorldCamera.h>

#include <cstdint>
#include <optional>

namespace ecs {
	class World;
}

namespace world_sim {

	class RoomOverlay {
	  public:
		struct Args {
			ecs::World*					world;
			engine::world::WorldCamera* camera;
			ecs::RoomDetectionSystem*	roomDetection;
		};

		RoomOverlay() = default;
		explicit RoomOverlay(const Args& args);

		void			   setActive(bool value) { active = value; }
		[[nodiscard]] bool isActive() const { return active; }

		/// Unproject the click to world mm and hit-test it against the live room
		/// rings (highest-id tie-break for nested rooms). Returns the hit roomId, or
		/// nullopt on a miss. Inverts the same camera projection render() uses.
		[[nodiscard]] std::optional<std::uint64_t> handleClick(float screenX, float screenY, int viewportW, int viewportH) const;

		/// The room to draw with the gold selected highlight. 0 (the monotonic-id
		/// sentinel) means none; the GameScene pushes the current selection here.
		void setSelectedRoom(std::uint64_t roomId) { selectedRoomId = roomId; }

		/// Draw every current room (tint + outline + name) in world space, with a
		/// brighter fill + gold outline on the selected room. No-op when inactive.
		void render(int viewportW, int viewportH);

	  private:
		ecs::World*					ecsWorld = nullptr;
		engine::world::WorldCamera* camera = nullptr;
		ecs::RoomDetectionSystem*	roomDetection = nullptr;

		bool		  active = false;
		std::uint64_t selectedRoomId = 0; // 0 = none; roomIds are monotonic from 1

		static constexpr float kPixelsPerMeter = 8.0F;
	};

} // namespace world_sim

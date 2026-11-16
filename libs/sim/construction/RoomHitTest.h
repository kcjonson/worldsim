#pragma once

#include <core/Vec2i64.h>
#include <polygon/Polygon.h>

#include <cstdint>
#include <optional>
#include <vector>

// Pure room hit-test (rooms-overlay R2): given a click already quantized to
// integer mm and a set of candidate rooms, return the id of the room the click
// falls in. Exact and concavity-independent (construction::pointInRoom), no GL,
// no ECS -- the overlay builds the candidate list from the live room records and
// this resolves the pick so it can be unit-tested in isolation.

namespace construction {

	// One room to test against: its id, its outer ring (CCW integer mm), and its
	// holes (each CW), as RoomDetectionSystem records them. Both are borrowed; the
	// candidate must not outlive the records it points at. Null holes means none.
	struct RoomHitCandidate {
		std::uint64_t					   roomId = 0;
		const geometry::Ring*			   ring = nullptr;
		const std::vector<geometry::Ring>* holes = nullptr;
	};

	// The room containing `clickMm`, or nullopt if the click is in no room. A room
	// is its ring minus its holes, so a click inside a room in a room lands in the
	// inner room only: the outer room has it in a hole. A click strictly inside a
	// room wins; a click on a boundary (a wall two rooms share, or an inner room's
	// ring, which is also the outer room's hole) goes to the highest roomId among
	// the rooms it touches, so the result is deterministic regardless of order.
	[[nodiscard]] std::optional<std::uint64_t> roomAtPoint(const geometry::Vec2i64& clickMm, const std::vector<RoomHitCandidate>& rooms);

} // namespace construction

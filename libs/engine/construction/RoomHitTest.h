#pragma once

#include <core/Vec2i64.h>
#include <polygon/Polygon.h>

#include <cstdint>
#include <optional>
#include <vector>

// Pure room hit-test (rooms-overlay R2): given a click already quantized to
// integer mm and a set of candidate room rings, return the id of the room the
// click falls in. Concavity-independent (exact crossing-number point-in-polygon),
// no GL, no ECS -- the overlay builds the candidate list from the live room
// records and this resolves the pick so it can be unit-tested in isolation.

namespace engine::construction {

	// One room to test against: its id and its boundary ring (CCW integer mm).
	// The ring is borrowed; the candidate must not outlive the records it points at.
	struct RoomHitCandidate {
		std::uint64_t		  roomId = 0;
		const geometry::Ring* ring = nullptr;
	};

	// The room whose ring contains `clickMm`, or nullopt if the click is outside
	// every ring. A boundary hit (on a vertex or edge) counts as a hit. On overlap
	// (nested rooms, or a shared boundary), the highest roomId wins -- the same
	// monotonic-id tie-break the wall/opening picks use, so the result is
	// deterministic regardless of candidate order.
	[[nodiscard]] std::optional<std::uint64_t> roomAtPoint(const geometry::Vec2i64& clickMm, const std::vector<RoomHitCandidate>& rooms);

} // namespace engine::construction

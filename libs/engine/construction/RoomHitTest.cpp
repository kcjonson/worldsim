#include "RoomHitTest.h"

#include <predicates/Predicates.h>

namespace engine::construction {

	std::optional<std::uint64_t> roomAtPoint(const geometry::Vec2i64& clickMm, const std::vector<RoomHitCandidate>& rooms) {
		bool		  hit = false;
		std::uint64_t best = 0;
		for (const auto& candidate : rooms) {
			if (candidate.ring == nullptr || candidate.ring->size() < 3) {
				continue;
			}
			if (geometry::pointInPolygon(clickMm, *candidate.ring) == geometry::PointInPolygon::Outside) {
				continue;
			}
			if (!hit || candidate.roomId > best) { // highest-id tie-break; ids are monotonic
				best = candidate.roomId;
				hit = true;
			}
		}
		if (!hit) {
			return std::nullopt;
		}
		return best;
	}

} // namespace engine::construction

#include "RoomHitTest.h"

#include "RoomDetection.h"

#include <predicates/Predicates.h>

namespace construction {

	std::optional<std::uint64_t> roomAtPoint(const geometry::Vec2i64& clickMm, const std::vector<RoomHitCandidate>& rooms) {
		static const std::vector<geometry::Ring> kNoHoles;
		std::optional<std::uint64_t> onBoundary;
		for (const auto& candidate : rooms) {
			if (candidate.ring == nullptr || candidate.ring->size() < 3) {
				continue;
			}
			const auto& holes = candidate.holes != nullptr ? *candidate.holes : kNoHoles;
			switch (pointInRoom(clickMm, *candidate.ring, holes)) {
				case geometry::PointInPolygon::Inside:
					return candidate.roomId; // rooms are disjoint, so at most one holds the point strictly
				case geometry::PointInPolygon::OnBoundary:
					if (!onBoundary || candidate.roomId > *onBoundary) {
						onBoundary = candidate.roomId;
					}
					break;
				case geometry::PointInPolygon::Outside:
					break;
			}
		}
		return onBoundary;
	}

} // namespace construction

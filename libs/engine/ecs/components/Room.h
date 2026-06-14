#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ecs {

	/// An enclosed region detected from a closed loop of built walls (building-
	/// construction D6). RoomDetectionSystem owns these: it spawns one per face of
	/// the wall arrangement and keeps roomId/name stable across edits. Room
	/// functions/types and the overlay UI are a later epic; this is identity + area.
	struct Room {
		uint64_t			  roomId = 0;
		float				  area = 0.0f;		  // cached m^2
		std::vector<uint64_t> boundingSegmentIds; // wall SegmentIds forming the enclosure
		std::string			  name;				  // "Room 1", "Room 2", ...
	};

} // namespace ecs

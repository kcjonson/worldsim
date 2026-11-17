#include "scenes/game/dev/CameraStateJson.h"

#include "scenes/game/dev/DevJson.h"

#include <world/camera/WorldCamera.h>

namespace world_sim {

	std::string cameraStateJson(const world::WorldCamera& camera, float pixelsPerMeter, int logicalWidth, int logicalHeight) {
		const Foundation::Rect visible = camera.getVisibleRect(logicalWidth, logicalHeight, pixelsPerMeter);
		return "{\"center\":{\"x\":" + jsonNumber(camera.position().x) + ",\"y\":" + jsonNumber(camera.position().y) +
			   "},\"zoom\":" + jsonNumber(camera.zoom()) + ",\"pixelsPerMeter\":" + jsonNumber(pixelsPerMeter * camera.zoom()) +
			   ",\"window\":{\"width\":" + std::to_string(logicalWidth) + ",\"height\":" + std::to_string(logicalHeight) +
			   "},\"visible\":{\"minX\":" + jsonNumber(visible.x) + ",\"minY\":" + jsonNumber(visible.y) +
			   ",\"maxX\":" + jsonNumber(visible.x + visible.width) + ",\"maxY\":" + jsonNumber(visible.y + visible.height) + "}}";
	}

} // namespace world_sim

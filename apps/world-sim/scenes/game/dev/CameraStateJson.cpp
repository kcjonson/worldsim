#include "scenes/game/dev/CameraStateJson.h"

#include "scenes/game/dev/DevJson.h"

#include <world/camera/WorldCamera.h>

namespace world_sim {

	std::string cameraStateJson(const world::WorldCamera& camera, float pixelsPerMeter, int logicalWidth, int logicalHeight) {
		const auto number = [](double value) { return jsonNumber(value); };
		const Foundation::Rect visible = camera.getVisibleRect(logicalWidth, logicalHeight, pixelsPerMeter);
		return "{\"center\":{\"x\":" + number(camera.position().x) + ",\"y\":" + number(camera.position().y) + "},\"zoom\":" + number(camera.zoom()) +
			   ",\"pixelsPerMeter\":" + number(pixelsPerMeter * camera.zoom()) + ",\"window\":{\"width\":" + std::to_string(logicalWidth) +
			   ",\"height\":" + std::to_string(logicalHeight) + "},\"visible\":{\"minX\":" + number(visible.x) + ",\"minY\":" + number(visible.y) +
			   ",\"maxX\":" + number(visible.x + visible.width) + ",\"maxY\":" + number(visible.y + visible.height) + "}}";
	}

} // namespace world_sim

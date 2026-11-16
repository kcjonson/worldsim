#include "DrawingFeedback.h"

#include "DrawingSystem.h"

#include <assets/ConstructionRegistry.h>
#include <primitives/Primitives.h>
#include <world/camera/WorldCamera.h>

#include <array>

namespace world_sim {

	namespace {

		// Each tool's guides keep their own draw ids and layer key: the wall preview
		// draws its band and centerlines on keys above the foundation preview's.
		struct GuideLayer {
			std::array<const char*, construction::kMaxSnapGuides> ids;
			std::int32_t										  key;
		};

		GuideLayer guideLayer(ToolKind tool) {
			if (tool == ToolKind::Wall) {
				return {{"wall_guide", "wall_guide_alt"}, 905};
			}
			return {{"drawing_guide", "drawing_guide_alt"}, 899};
		}

	} // namespace

	void drawSnapFeedback(const DrawingFeedbackInput& input) {
		const GuideLayer		layer = guideLayer(input.tool);
		const Foundation::Color color{input.style.guideColor.r, input.style.guideColor.g, input.style.guideColor.b, input.style.guideColor.a};
		auto toScreen = [&](Foundation::Vec2 world) {
			return input.camera.worldToScreen(world.x, world.y, input.viewportWidth, input.viewportHeight, input.pixelsPerMeter);
		};

		for (std::size_t i = 0; i < input.snap.guideCount; ++i) {
			const construction::SnapGuide& guide = input.snap.guides[i];
			if (guide.from.x == guide.to.x && guide.from.y == guide.to.y) {
				continue;
			}
			Renderer::Primitives::drawLine({
				.start = toScreen(guide.from),
				.end = toScreen(guide.to),
				.style = {.color = color, .width = input.style.guideWidth},
				.id = layer.ids[i],
				.key = layer.key,
			});
		}
	}

} // namespace world_sim

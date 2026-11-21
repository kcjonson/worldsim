#include "BlockedMarkers.h"

#include <ecs/systems/PlacedItemSystem.h>
#include <primitives/Primitives.h>

#include <string>

namespace world_sim {

	namespace {
		// Over the entities and the visibility cover, under the selection indicator and the ui.
		constexpr int32_t kMarkerKey = 96;
		constexpr float	  kRadiusUiPx = 9.0F;
		// The warning colour the toasts use for the same news.
		const Foundation::Color kFill(0.93F, 0.55F, 0.13F, 0.95F);
		const Foundation::Color kRim(0.2F, 0.12F, 0.05F, 0.9F);
	} // namespace

	void BlockedMarkers::render(ecs::World& world, const world::WorldCamera& camera, int viewportW, int viewportH, float pixelsPerMeter, float uiScale) {
		const ecs::PlacedItemSystem* items = world.tryGetSystem<ecs::PlacedItemSystem>();
		if (items == nullptr) {
			return;
		}
		const float radius = kRadiusUiPx * uiScale;
		items->items().forEach([&](const ecs::PlacedItemIndex::Entry& entry) {
			if (entry.pending || entry.use != ecs::UseState::Blocked) {
				return;
			}
			// Raised over the item's pivot, where its body stands up on the screen.
			Foundation::Vec2 screen = camera.worldToScreen(entry.position.x, entry.position.y, viewportW, viewportH, pixelsPerMeter);
			screen.y -= radius * 2.5F;
			if (screen.x < -radius || screen.y < -radius || screen.x > static_cast<float>(viewportW) + radius ||
				screen.y > static_cast<float>(viewportH) + radius) {
				return;
			}
			const std::string id = "blocked-marker-" + std::to_string(entry.entity);
			Renderer::Primitives::drawCircle(Renderer::Primitives::CircleArgs{
				.center = Foundation::Vec2{screen.x, screen.y},
				.radius = radius,
				.style = Foundation::CircleStyle{.fill = kFill, .border = Foundation::BorderStyle{.color = kRim, .width = 1.5F * uiScale}},
				.id = id.c_str(),
				.key = kMarkerKey,
			});
			Renderer::Primitives::drawText(Renderer::Primitives::TextArgs{
				.text = "!",
				.position = Foundation::Vec2{screen.x - radius, screen.y - radius},
				.scale = uiScale,
				.color = Foundation::Color(1.0F, 1.0F, 1.0F, 1.0F),
				.hAlign = Foundation::HorizontalAlign::Center,
				.vAlign = Foundation::VerticalAlign::Middle,
				.boxWidth = radius * 2.0F,
				.boxHeight = radius * 2.0F,
				.id = id.c_str(),
				.key = kMarkerKey + 1,
			});
		});
	}

} // namespace world_sim

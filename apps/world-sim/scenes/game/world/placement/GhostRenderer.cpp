#include "GhostRenderer.h"

#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/DetailLevels.h>
#include <assets/ItemView.h>
#include <assets/ViewConfig.h>
#include <primitives/Primitives.h>
#include <utils/Log.h>
#include <world/rendering/ItemViewDrawing.h>

namespace world_sim {

namespace {
constexpr float	  kGhostAlpha = 0.5F;	  // Semi-transparent
constexpr float	  kInvalidAlpha = 0.3F;	  // More transparent when invalid
constexpr float	  kPixelsPerMeter = 8.0F; // Match GameScene constant
constexpr int32_t kGhostKey = 1000;		  // Draw above entities
} // namespace

void GhostRenderer::renderRefusal(
	std::string_view label, Foundation::Vec2 worldPos, const world::WorldCamera& camera, int viewportWidth, int viewportHeight, float uiScale
) {
	// As the drawing tools' reason beside the cursor; its look is the overlays' (rendering.xml, <ItemOverlays>).
	const assets::ItemOverlayStyle& look = assets::ConstructionRegistry::Get().rendering().itemOverlays;
	const Foundation::Vec2			pivot = camera.worldToScreen(worldPos.x, worldPos.y, viewportWidth, viewportHeight, kPixelsPerMeter);
	const float						shadow = look.refusalShadowPx * uiScale;
	Foundation::Color				color = worldview::colorOf(look.refusedTint);
	color.a = 1.0F;
	Renderer::Primitives::drawText({
		.text = std::string(label),
		.position = {pivot.x + look.refusalOffsetXPx * uiScale, pivot.y + look.refusalOffsetYPx * uiScale},
		.scale = look.refusalTextScale * uiScale,
		.color = color,
		.shadow = Foundation::BoxShadow{.color = {0.0F, 0.0F, 0.0F, 0.8F}, .blur = 0.0F, .offset = {shadow, shadow}},
		.id = "placement_refusal",
		.key = kGhostKey + 1,
	});
}

void GhostRenderer::beginFrame() {
	// A reload changes the defs the views were built from.
	if (const uint64_t generation = assets::AssetRegistry::Get().generation(); generation != viewsGeneration) {
		views.clear();
		viewsGeneration = generation;
	}
	for (auto& [key, held] : views) {
		held.drawn = false;
	}
}

void GhostRenderer::endFrame() {
	std::erase_if(views, [](const auto& entry) { return !entry.second.drawn; });
}

void GhostRenderer::render(const std::string& defName,
						   Foundation::Vec2 worldPos,
						   uint32_t turnQuanta,
						   const world::WorldCamera& camera,
						   int viewportWidth,
						   int viewportHeight,
						   bool isValid,
						   float uiScale) {
	if (defName.empty()) {
		return;
	}

	// Meshes are about the pivot: a template upright at scale 1 (PlacementSystem::spawnEntity), a view turned already.
	const worldview::ItemScreenFrame frame{
		.pivot = camera.worldToScreen(worldPos.x, worldPos.y, viewportWidth, viewportHeight, kPixelsPerMeter),
		.pixelsPerMeter = camera.worldDistanceToScreen(1.0F, kPixelsPerMeter),
	};
	const assets::ItemOverlayStyle& look = assets::ConstructionRegistry::Get().rendering().itemOverlays;
	const worldview::ItemDrawStyle	ghost{.opacity = isValid ? kGhostAlpha : kInvalidAlpha, .refused = !isValid, .key = kGhostKey};

	auto&		registry = assets::AssetRegistry::Get();
	const auto* def = registry.getDefinition(defName);
	if (def != nullptr && def->parts != nullptr) {
		// The view at the ghost's turn, turned already, at the level the camera's density draws
		// (rotatable-placed-items 14.4).
		const uint8_t level = assets::detailLevelFor(def->detailLevels, kPixelsPerMeter * camera.zoom());
		const auto	  key = std::make_tuple(defName, turnQuanta, level);
		auto		  held = views.find(key);
		if (held == views.end()) {
			assets::ItemView view = assets::buildItemMesh(*def, turnQuanta, level, assets::GameViewConfig::Get().config());
			if (!view.error.empty()) {
				LOG_ERROR(Game, "No ghost view of %s at %u quanta: %s", defName.c_str(), turnQuanta, view.error.c_str());
				return;
			}
			held = views
					   .emplace(key, HeldView{.mesh = std::move(view.mesh), .overlays = assets::itemOverlays(*def, turnQuanta, look.hatchSpacingMeters)})
					   .first;
		}
		held->second.drawn = true;
		// Under the ghost, on the ground; the collider is 14.4's footprint line, the parts' own footprints the asset
		// manager's.
		worldview::drawItemOverlays(
			held->second.overlays,
			frame,
			{.useAreas = true, .clearance = true, .footprints = false, .collider = true},
			look,
			{.pixel = uiScale, .opacity = isValid ? 1.0F : kInvalidAlpha / kGhostAlpha, .refused = !isValid, .key = kGhostKey}
		);
		worldview::drawItemView(held->second.mesh, frame, look, ghost);
		return;
	}

	// The def's default template: the preview stands for any instance, so it has no traits.
	const auto* mesh = registry.getTemplate(defName, assets::TreeTraits{});
	if (mesh == nullptr) {
		return;
	}
	worldview::drawItemView(*mesh, frame, look, ghost);
}

} // namespace world_sim

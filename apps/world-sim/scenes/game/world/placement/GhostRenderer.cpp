#include "GhostRenderer.h"

#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/DetailLevels.h>
#include <assets/ItemView.h>
#include <assets/ViewConfig.h>
#include <font/FontRenderer.h>
#include <primitives/Primitives.h>
#include <utils/Log.h>
#include <world/rendering/ItemViewDrawing.h>

namespace world_sim {

namespace {
constexpr float	  kGhostAlpha = 0.5F;	  // Semi-transparent
constexpr float	  kInvalidAlpha = 0.3F;	  // More transparent when invalid
constexpr float	  kPixelsPerMeter = 8.0F; // Match GameScene constant
// Above entities, in layers: every pending install's overlays, then their bodies, then the placing ghost's overlays,
// its body, and its refusal label, so nothing drawn for a pending install covers the ghost being placed.
constexpr int32_t kPendingOverlayKey = 1000;
constexpr int32_t kPendingBodyKey = 1001;
constexpr int32_t kPlacingOverlayKey = 1002;
constexpr int32_t kPlacingBodyKey = 1003;
constexpr int32_t kRefusalKey = 1004;
constexpr float	  kRefusalPaddingPx = 3.0F;
constexpr float	  kFontBaseSize = 16.0F;
} // namespace

void GhostRenderer::renderRefusal(
	std::string_view label, Foundation::Vec2 worldPos, const world::WorldCamera& camera, int viewportWidth, int viewportHeight, float uiScale
) {
	// As the drawing tools' reason beside the cursor; its look is the overlays' (rendering.xml, <ItemOverlays>).
	const assets::ItemOverlayStyle& look = assets::ConstructionRegistry::Get().rendering().itemOverlays;
	const Foundation::Vec2			pivot = camera.worldToScreen(worldPos.x, worldPos.y, viewportWidth, viewportHeight, kPixelsPerMeter);
	const Foundation::Vec2			at{pivot.x + look.refusalOffsetXPx * uiScale, pivot.y + look.refusalOffsetYPx * uiScale};
	const float						scale = look.refusalTextScale * uiScale;
	const std::string				text(label);
	Foundation::Color				color = worldview::colorOf(look.refusedTint);
	color.a = 1.0F;
	// The plate the area names have, so the red reads over the red-tinted ghost and anything under it.
	if (const Renderer::FontRenderer* fonts = Renderer::Primitives::getFontRenderer()) {
		const glm::vec2 size = fonts->MeasureText(text, scale);
		const float		pad = kRefusalPaddingPx * uiScale;
		Renderer::Primitives::drawRect({
			.bounds = {at.x - pad, at.y - pad, size.x + 2.0F * pad, std::max(size.y, scale * kFontBaseSize) + 2.0F * pad},
			.style = {.fill = worldview::colorOf(look.labelBacking)},
			.id = "placement_refusal",
			.key = kRefusalKey,
		});
	}
	Renderer::Primitives::drawText({
		.text = text,
		.position = at,
		.scale = scale,
		.color = color,
		.id = "placement_refusal",
		.key = kRefusalKey,
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
						   bool placing,
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
	const worldview::ItemDrawStyle	ghost{
		.opacity = isValid ? kGhostAlpha : kInvalidAlpha, .refused = !isValid, .key = placing ? kPlacingBodyKey : kPendingBodyKey
	};

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
			{.pixel = uiScale,
			 .opacity = isValid ? 1.0F : kInvalidAlpha / kGhostAlpha,
			 .refused = !isValid,
			 .key = placing ? kPlacingOverlayKey : kPendingOverlayKey}
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

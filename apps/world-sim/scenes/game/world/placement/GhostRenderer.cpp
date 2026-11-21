#include "GhostRenderer.h"

#include <assets/AssetRegistry.h>
#include <assets/DetailLevels.h>
#include <assets/ItemView.h>
#include <assets/ViewConfig.h>
#include <assets/placement/InstanceTransform.h>
#include <primitives/Primitives.h>
#include <utils/Log.h>

namespace world_sim {

namespace {
constexpr float kGhostAlpha = 0.5F;	   // Semi-transparent
constexpr float kInvalidAlpha = 0.3F;  // More transparent when invalid
constexpr float kInvalidTintR = 1.0F;  // Red tint for invalid placement
constexpr float kInvalidTintG = 0.3F;
constexpr float kInvalidTintB = 0.3F;
constexpr float kPixelsPerMeter = 8.0F; // Match GameScene constant
// The refusal label, as the drawing tools' reason beside the cursor: text scale and offset from the pivot in
// logical px, both times the interface scale.
constexpr float			   kRefusalTextScale = 0.8F;
constexpr Foundation::Vec2 kRefusalOffset{14.0F, -22.0F};
constexpr float			   kRefusalShadowPx = 1.0F;
} // namespace

void GhostRenderer::renderRefusal(
	std::string_view label, Foundation::Vec2 worldPos, const world::WorldCamera& camera, int viewportWidth, int viewportHeight, float uiScale
) {
	const Foundation::Vec2 pivot = camera.worldToScreen(worldPos.x, worldPos.y, viewportWidth, viewportHeight, kPixelsPerMeter);
	Renderer::Primitives::drawText({
		.text = std::string(label),
		.position = {pivot.x + kRefusalOffset.x * uiScale, pivot.y + kRefusalOffset.y * uiScale},
		.scale = kRefusalTextScale * uiScale,
		.color = {kInvalidTintR, kInvalidTintG, kInvalidTintB, 1.0F},
		.shadow = Foundation::BoxShadow{.color = {0.0F, 0.0F, 0.0F, 0.8F}, .blur = 0.0F, .offset = {kRefusalShadowPx, kRefusalShadowPx}},
		.id = "placement_refusal",
		.key = 1001,
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
						   bool isValid) {
	if (defName.empty()) {
		return;
	}

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
			held = views.emplace(key, HeldView{.mesh = std::move(view.mesh)}).first;
		}
		held->second.drawn = true;
		draw(held->second.mesh, worldPos, camera, viewportWidth, viewportHeight, isValid);
		return;
	}

	// The def's default template: the preview stands for any instance, so it has no traits.
	const auto* mesh = registry.getTemplate(defName, assets::TreeTraits{});
	if (mesh == nullptr) {
		return;
	}
	draw(*mesh, worldPos, camera, viewportWidth, viewportHeight, isValid);
}

void GhostRenderer::draw(
	const renderer::TessellatedMesh& mesh, Foundation::Vec2 offset, const world::WorldCamera& camera, int viewportWidth, int viewportHeight, bool isValid
) {
	if (mesh.vertices.empty()) {
		return;
	}
	// The mesh is about the pivot: a template upright at scale 1 (PlacementSystem::spawnEntity), a view turned already.
	const assets::InstanceTransform placed = assets::uprightInstance({offset.x, offset.y});
	m_transformedVertices.resize(mesh.vertices.size());
	for (size_t i = 0; i < mesh.vertices.size(); ++i) {
		const glm::vec2 world	 = placed.toWorld({mesh.vertices[i].x, mesh.vertices[i].y});
		m_transformedVertices[i] = camera.worldToScreen(world.x, world.y, viewportWidth, viewportHeight, kPixelsPerMeter);
	}

	// Create ghost colors with transparency
	float alpha = isValid ? kGhostAlpha : kInvalidAlpha;
	m_ghostColors.resize(mesh.vertices.size());

	if (mesh.hasColors()) {
		// Apply alpha to existing colors, with optional red tint for invalid
		for (size_t i = 0; i < mesh.colors.size(); ++i) {
			const auto& c = mesh.colors[i];
			if (isValid) {
				m_ghostColors[i] = {c.r, c.g, c.b, c.a * alpha};
			} else {
				// Red-tinted for invalid placement
				m_ghostColors[i] = {
					c.r * kInvalidTintR,
					c.g * kInvalidTintG,
					c.b * kInvalidTintB,
					c.a * alpha
				};
			}
		}
	} else {
		// Use white with alpha if no colors
		Foundation::Color ghostColor = isValid
			? Foundation::Color{1.0F, 1.0F, 1.0F, alpha}
			: Foundation::Color{kInvalidTintR, kInvalidTintG, kInvalidTintB, alpha};

		for (auto& c : m_ghostColors) {
			c = ghostColor;
		}
	}

	// Draw the ghost using primitives API
	Renderer::Primitives::drawTriangles({
		.vertices = m_transformedVertices.data(),
		.indices = mesh.indices.data(),
		.vertexCount = m_transformedVertices.size(),
		.indexCount = mesh.indices.size(),
		.colors = m_ghostColors.data(),
		.id = "placement_ghost",
		.key = 1000  // Draw above entities
	});
}

} // namespace world_sim

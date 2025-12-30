#include "GhostRenderer.h"

#include <assets/AssetRegistry.h>
#include <primitives/Primitives.h>
#include <vector/Types.h>

#include <limits>

namespace world_sim {

namespace {
constexpr float kGhostAlpha = 0.5F;	   // Semi-transparent
constexpr float kInvalidAlpha = 0.3F;  // More transparent when invalid
constexpr float kInvalidTintR = 1.0F;  // Red tint for invalid placement
constexpr float kInvalidTintG = 0.3F;
constexpr float kInvalidTintB = 0.3F;
constexpr float kPixelsPerMeter = 8.0F; // Match GameScene constant
} // namespace

void GhostRenderer::render(const std::string& defName,
						   Foundation::Vec2 worldPos,
						   const engine::world::WorldCamera& camera,
						   int viewportWidth,
						   int viewportHeight,
						   bool isValid) {
	if (defName.empty()) {
		return;
	}

	// Get the tessellated mesh template from the asset registry
	auto& registry = engine::assets::AssetRegistry::Get();
	const auto* mesh = registry.getTemplate(defName);
	if (mesh == nullptr || mesh->vertices.empty()) {
		return;
	}

	// Calculate mesh bounds to find center offset
	// Mesh vertices are in local space but NOT centered on origin
	float minX = std::numeric_limits<float>::max();
	float maxX = std::numeric_limits<float>::lowest();
	float minY = std::numeric_limits<float>::max();
	float maxY = std::numeric_limits<float>::lowest();
	for (const auto& v : mesh->vertices) {
		minX = std::min(minX, v.x);
		maxX = std::max(maxX, v.x);
		minY = std::min(minY, v.y);
		maxY = std::max(maxY, v.y);
	}
	// Center offset: shift so mesh center aligns with worldPos
	float centerOffsetX = (minX + maxX) * 0.5F;
	float centerOffsetY = (minY + maxY) * 0.5F;

	// Calculate screen position from world position
	auto screenPos = camera.worldToScreen(worldPos.x, worldPos.y, viewportWidth, viewportHeight, kPixelsPerMeter);

	// Get the scale factor (pixels per meter * zoom)
	float scale = kPixelsPerMeter * camera.zoom();

	// Transform vertices from local space to screen space, centered on cursor
	m_transformedVertices.resize(mesh->vertices.size());
	for (size_t i = 0; i < mesh->vertices.size(); ++i) {
		const auto& v = mesh->vertices[i];
		m_transformedVertices[i] = {
			screenPos.x + (v.x - centerOffsetX) * scale,
			screenPos.y + (v.y - centerOffsetY) * scale
		};
	}

	// Create ghost colors with transparency
	float alpha = isValid ? kGhostAlpha : kInvalidAlpha;
	m_ghostColors.resize(mesh->vertices.size());

	if (mesh->hasColors()) {
		// Apply alpha to existing colors, with optional red tint for invalid
		for (size_t i = 0; i < mesh->colors.size(); ++i) {
			const auto& c = mesh->colors[i];
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
		.indices = mesh->indices.data(),
		.vertexCount = m_transformedVertices.size(),
		.indexCount = mesh->indices.size(),
		.colors = m_ghostColors.data(),
		.id = "placement_ghost",
		.zIndex = 1000  // Draw above entities
	});
}

} // namespace world_sim

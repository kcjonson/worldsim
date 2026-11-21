#include "world/rendering/ItemViewDrawing.h"

#include <primitives/Primitives.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace worldview {

	namespace {

		constexpr float kUseAreaFillAlpha = 0.28F;
		constexpr float kHatchAlpha = 0.6F;
		constexpr float kFootprintAlpha = 0.85F;
		// In logical px.
		constexpr float kEdgeWidth = 1.5F;
		constexpr float kHatchWidth = 1.0F;
		constexpr float kFootprintWidth = 1.0F;
		constexpr float kColliderWidth = 2.0F;
		constexpr float kDash = 6.0F;
		constexpr float kDashGap = 4.0F;
		constexpr float kLabelSize = 12.0F;
		constexpr float kLabelBoxWidth = 240.0F;
		constexpr float kAnchorRadius = 4.0F;
		constexpr float kFontBaseSize = 16.0F;

		Foundation::Color styled(Foundation::Color color, float alpha, const ItemOverlayStyle& style) {
			Foundation::Color result = style.refused ? kRefusedColor : color;
			result.a = alpha * style.opacity;
			return result;
		}

		std::vector<Foundation::Vec2>& ringOnScreen(const std::vector<glm::vec2>& ring, const ItemScreenFrame& frame) {
			thread_local std::vector<Foundation::Vec2> points;
			points.clear();
			for (const glm::vec2& point : ring) {
				points.push_back(frame.toScreen(point));
			}
			return points;
		}

		void drawRing(
			const std::vector<glm::vec2>& ring,
			const ItemScreenFrame&		  frame,
			float						  width,
			Foundation::Color			  color,
			std::optional<Foundation::DashPattern> dash,
			int32_t						  key
		) {
			if (ring.size() < 2) {
				return;
			}
			Renderer::Primitives::drawPolyline(
				{.points = ringOnScreen(ring, frame), .width = width, .color = color, .closed = true, .dash = dash, .id = "item_overlay", .key = key}
			);
		}

		void drawArea(
			const assets::OverlayArea&				  area,
			const ItemScreenFrame&					  frame,
			float									  width,
			Foundation::Color						  color,
			std::optional<Foundation::DashPattern>	  dash,
			int32_t									  key
		) {
			for (const assets::AreaPolygon& polygon : area.polygons) {
				drawRing(polygon.outer, frame, width, color, dash, key);
				for (const std::vector<glm::vec2>& hole : polygon.holes) {
					drawRing(hole, frame, width, color, dash, key);
				}
			}
		}

	} // namespace

	void drawItemView(const renderer::TessellatedMesh& mesh, const ItemScreenFrame& frame, int32_t key) {
		if (mesh.vertices.empty() || mesh.indices.empty()) {
			return;
		}
		thread_local std::vector<Foundation::Vec2> placed;
		placed.clear();
		for (const Foundation::Vec2& vertex : mesh.vertices) {
			placed.push_back(frame.toScreen({vertex.x, vertex.y}));
		}
		Renderer::Primitives::drawTriangles({
			.vertices = placed.data(),
			.indices = mesh.indices.data(),
			.vertexCount = placed.size(),
			.indexCount = mesh.indices.size(),
			.colors = mesh.hasColors() ? mesh.colors.data() : nullptr,
			.id = "item_view",
			.key = key,
		});
	}

	void drawItemOverlays(
		const assets::ItemOverlays& overlays, const ItemScreenFrame& frame, const ItemOverlayShown& shown, const ItemOverlayStyle& style
	) {
		const float pixel = style.pixel;
		if (shown.useAreas) {
			for (const assets::OverlayArea& area : overlays.useAreas) {
				const renderer::TessellatedMesh& fill = area.fill;
				if (!fill.vertices.empty() && !fill.indices.empty()) {
					std::vector<Foundation::Vec2> placed;
					placed.reserve(fill.vertices.size());
					for (const Foundation::Vec2& vertex : fill.vertices) {
						placed.push_back(frame.toScreen({vertex.x, vertex.y}));
					}
					Renderer::Primitives::drawTriangles({
						.vertices = placed.data(),
						.indices = fill.indices.data(),
						.vertexCount = placed.size(),
						.indexCount = fill.indices.size(),
						.color = styled(kUseAreaColor, kUseAreaFillAlpha, style),
						.id = "item_overlay_use",
						.key = style.key,
					});
				}
				drawArea(area, frame, kEdgeWidth * pixel, styled(kUseAreaColor, 1.0F, style), std::nullopt, style.key);
			}
		}
		if (shown.clearance) {
			const Foundation::Color hatch = styled(kClearanceColor, kHatchAlpha, style);
			for (const std::array<glm::vec2, 2>& line : overlays.clearanceHatch) {
				Renderer::Primitives::drawLine(
					{.start = frame.toScreen(line[0]),
					 .end = frame.toScreen(line[1]),
					 .style = {.color = hatch, .width = kHatchWidth * pixel},
					 .id = "item_overlay_clearance",
					 .key = style.key}
				);
			}
			drawArea(
				overlays.clearance,
				frame,
				kEdgeWidth * pixel,
				styled(kClearanceColor, 1.0F, style),
				Foundation::DashPattern{.dash = kDash * pixel, .gap = kDashGap * pixel},
				style.key
			);
		}
		if (shown.footprints) {
			for (const std::vector<glm::vec2>& ring : overlays.footprints) {
				drawRing(ring, frame, kFootprintWidth * pixel, styled(kFootprintColor, kFootprintAlpha, style), std::nullopt, style.key);
			}
		}
		if (shown.collider) {
			for (const std::vector<glm::vec2>& ring : overlays.collider) {
				drawRing(ring, frame, kColliderWidth * pixel, styled(kColliderColor, 1.0F, style), std::nullopt, style.key);
			}
		}
		if (shown.useAreas && style.labels) {
			for (const assets::OverlayArea& area : overlays.useAreas) {
				if (area.name.empty() || area.polygons.empty()) {
					continue;
				}
				const Foundation::Vec2 at = frame.toScreen(area.labelPoint);
				Renderer::Primitives::drawText({
					.text = area.name,
					.position = {at.x - kLabelBoxWidth * pixel * 0.5F, at.y - kLabelSize * pixel * 0.5F},
					.scale = kLabelSize * pixel / kFontBaseSize,
					.color = styled(kUseAreaColor, 1.0F, style),
					.hAlign = Foundation::HorizontalAlign::Center,
					.vAlign = Foundation::VerticalAlign::Middle,
					.boxWidth = kLabelBoxWidth * pixel,
					.boxHeight = kLabelSize * pixel,
					.id = "item_overlay_label",
					.key = style.key,
				});
			}
		}
	}

	void drawItemStrips(const assets::ItemView& view, const ItemScreenFrame& frame, float reachMeters, const ItemOverlayStyle& style) {
		for (const assets::ViewStrip& strip : view.strips) {
			// A first or last band reaches to infinity: drawn out to the item's reach.
			const float left = std::max(strip.minX, -reachMeters);
			const float right = std::min(strip.maxX, reachMeters);
			drawRing(
				{{left, strip.minY}, {right, strip.minY}, {right, strip.maxY}, {left, strip.maxY}},
				frame,
				kColliderWidth * style.pixel,
				styled(kStripColor, 1.0F, style),
				std::nullopt,
				style.key
			);
			if (strip.flat) {
				continue;
			}
			const Foundation::Vec2 anchorLeft = frame.toScreen({left, strip.anchorLocalY});
			const Foundation::Vec2 anchorRight = frame.toScreen({right, strip.anchorLocalY});
			const Foundation::Color anchor = styled(kAnchorColor, 1.0F, style);
			Renderer::Primitives::drawLine(
				{.start = anchorLeft, .end = anchorRight, .style = {.color = anchor, .width = kColliderWidth * style.pixel}, .id = "item_anchor", .key = style.key}
			);
			Renderer::Primitives::drawCircle(
				{.center = {(anchorLeft.x + anchorRight.x) * 0.5F, anchorLeft.y},
				 .radius = kAnchorRadius * style.pixel,
				 .style = {.fill = anchor},
				 .id = "item_anchor",
				 .key = style.key}
			);
		}
	}

} // namespace worldview

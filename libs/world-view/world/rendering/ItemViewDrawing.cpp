#include "world/rendering/ItemViewDrawing.h"

#include <primitives/Primitives.h>

#include <algorithm>
#include <cmath>
#include <array>
#include <optional>
#include <span>
#include <vector>

namespace worldview {

	namespace {

		constexpr float kLabelBoxWidth = 240.0F; // logical px; the label is centred in it
		constexpr float kLabelHaloSpread = 3.0F;
		constexpr float kLabelHaloBlur = 2.0F;
		constexpr float kAnchorRadius = 4.0F;
		constexpr float kFontBaseSize = 16.0F;

		Foundation::Color styled(const assets::StyleColor& color, float alpha, const assets::ItemOverlayStyle& look, const ItemDrawStyle& style) {
			Foundation::Color result = colorOf(style.refused ? look.refusedTint : color);
			result.a = alpha * style.opacity;
			return result;
		}

		/// Pivot-relative points on the screen, in a buffer kept from call to call.
		const std::vector<Foundation::Vec2>& onScreen(std::span<const glm::vec2> points, const ItemScreenFrame& frame) {
			thread_local std::vector<Foundation::Vec2> placed;
			placed.clear();
			for (const glm::vec2& point : points) {
				placed.push_back(frame.toScreen(point));
			}
			return placed;
		}

		void drawRing(
			std::span<const glm::vec2>			   ring,
			const ItemScreenFrame&				   frame,
			float								   width,
			Foundation::Color					   color,
			std::optional<Foundation::DashPattern> dash,
			int32_t								   key
		) {
			if (ring.size() < 2) {
				return;
			}
			Renderer::Primitives::drawPolyline(
				{.points = onScreen(ring, frame), .width = width, .color = color, .closed = true, .dash = dash, .id = "item_overlay", .key = key}
			);
		}

		void drawArea(
			const assets::OverlayArea&			   area,
			const ItemScreenFrame&				   frame,
			float								   width,
			Foundation::Color					   color,
			std::optional<Foundation::DashPattern> dash,
			int32_t								   key
		) {
			for (const assets::AreaPolygon& polygon : area.polygons) {
				drawRing(polygon.outer, frame, width, color, dash, key);
				for (const std::vector<glm::vec2>& hole : polygon.holes) {
					drawRing(hole, frame, width, color, dash, key);
				}
			}
		}

	} // namespace

	Foundation::Color colorOf(const assets::StyleColor& color) {
		return {color.r, color.g, color.b, color.a};
	}

	void drawItemView(
		const renderer::TessellatedMesh& mesh, const ItemScreenFrame& frame, const assets::ItemOverlayStyle& look, const ItemDrawStyle& style
	) {
		if (mesh.vertices.empty() || mesh.indices.empty()) {
			return;
		}
		const std::vector<Foundation::Vec2>& placed = onScreen(mesh.vertices, frame);
		const Foundation::Color				 tint = style.refused ? colorOf(look.refusedTint) : Foundation::Color(1.0F, 1.0F, 1.0F, 1.0F);
		const bool							 plain = !style.refused && style.opacity >= 1.0F;
		thread_local std::vector<Foundation::Color> colors;
		if (!plain && mesh.hasColors()) {
			colors.resize(mesh.colors.size());
			for (size_t index = 0; index < mesh.colors.size(); ++index) {
				const Foundation::Color& color = mesh.colors[index];
				colors[index] = {color.r * tint.r, color.g * tint.g, color.b * tint.b, color.a * style.opacity};
			}
		}
		Renderer::Primitives::drawTriangles({
			.vertices = placed.data(),
			.indices = mesh.indices.data(),
			.vertexCount = placed.size(),
			.indexCount = mesh.indices.size(),
			.color = {tint.r, tint.g, tint.b, style.opacity},
			.colors = !mesh.hasColors() ? nullptr : plain ? mesh.colors.data() : colors.data(),
			.id = "item_view",
			.key = style.key,
		});
	}

	void drawItemOverlays(
		const assets::ItemOverlays& overlays, const ItemScreenFrame& frame, const ItemOverlayShown& shown, const assets::ItemOverlayStyle& look,
		const ItemDrawStyle& style
	) {
		const float pixel = style.pixel;
		if (shown.useAreas) {
			for (const assets::OverlayArea& area : overlays.useAreas) {
				const renderer::TessellatedMesh& fill = area.fill;
				if (!fill.vertices.empty() && !fill.indices.empty()) {
					const std::vector<Foundation::Vec2>& placed = onScreen(fill.vertices, frame);
					Renderer::Primitives::drawTriangles({
						.vertices = placed.data(),
						.indices = fill.indices.data(),
						.vertexCount = placed.size(),
						.indexCount = fill.indices.size(),
						.color = styled(look.useAreaColor, look.useAreaFillAlpha, look, style),
						.id = "item_overlay_use",
						.key = style.key,
					});
				}
				drawArea(area, frame, look.edgeWidth * pixel, styled(look.useAreaColor, 1.0F, look, style), std::nullopt, style.key);
			}
		}
		if (shown.clearance) {
			// Thinned to every Nth line where they would crowd closer than hatchMinGapPx; each piece keeps its middle one.
			const float gap = overlays.hatchSpacingMeters * frame.pixelsPerMeter;
			const int64_t stride = gap > 0.0F ? std::max<int64_t>(1, static_cast<int64_t>(std::ceil(look.hatchMinGapPx * pixel / gap))) : 1;
			const Foundation::Color hatch = styled(look.clearanceColor, look.hatchAlpha, look, style);
			for (const assets::HatchLine& line : overlays.clearanceHatch) {
				if (!line.middle && line.index % stride != 0) {
					continue;
				}
				Renderer::Primitives::drawLine(
					{.start = frame.toScreen(line.from),
					 .end = frame.toScreen(line.to),
					 .style = {.color = hatch, .width = look.hatchWidth * pixel},
					 .id = "item_overlay_clearance",
					 .key = style.key}
				);
			}
			drawArea(
				overlays.clearance,
				frame,
				look.edgeWidth * pixel,
				styled(look.clearanceColor, 1.0F, look, style),
				Foundation::DashPattern{.dash = look.dashPx * pixel, .gap = look.gapPx * pixel},
				style.key
			);
		}
		if (shown.footprints) {
			for (const std::vector<glm::vec2>& ring : overlays.footprints) {
				drawRing(ring, frame, look.footprintWidth * pixel, styled(look.footprintColor, look.footprintAlpha, look, style), std::nullopt, style.key);
			}
		}
		if (shown.collider) {
			for (const std::vector<glm::vec2>& ring : overlays.collider) {
				drawRing(ring, frame, look.colliderWidth * pixel, styled(look.colliderColor, 1.0F, look, style), std::nullopt, style.key);
			}
		}
		if (shown.useAreas && style.labels) {
			Foundation::Color backing = colorOf(look.labelBacking);
			backing.a *= style.opacity;
			for (const assets::OverlayArea& area : overlays.useAreas) {
				if (area.name.empty() || area.polygons.empty()) {
					continue;
				}
				const Foundation::Vec2 at = frame.toScreen(area.labelPoint);
				Renderer::Primitives::drawText({
					.text = area.name,
					.position = {at.x - kLabelBoxWidth * pixel * 0.5F, at.y - look.labelPx * pixel * 0.5F},
					.scale = look.labelPx * pixel / kFontBaseSize,
					.color = styled(look.useAreaColor, 1.0F, look, style),
					.shadow = Foundation::BoxShadow{.color = backing, .blur = kLabelHaloBlur * pixel, .spread = kLabelHaloSpread * pixel},
					.hAlign = Foundation::HorizontalAlign::Center,
					.vAlign = Foundation::VerticalAlign::Middle,
					.boxWidth = kLabelBoxWidth * pixel,
					.boxHeight = look.labelPx * pixel,
					.id = "item_overlay_label",
					.key = style.key,
				});
			}
		}
	}

	void drawItemStrips(
		const assets::ItemView& view, const ItemScreenFrame& frame, float reachMeters, const assets::ItemOverlayStyle& look, const ItemDrawStyle& style
	) {
		for (const assets::ViewStrip& strip : view.strips) {
			// A first or last band reaches to infinity: drawn out to the item's reach.
			const float						left = std::max(strip.minX, -reachMeters);
			const float						right = std::min(strip.maxX, reachMeters);
			const std::array<glm::vec2, 4>	box{glm::vec2{left, strip.minY}, glm::vec2{right, strip.minY}, glm::vec2{right, strip.maxY}, glm::vec2{left, strip.maxY}};
			drawRing(box, frame, look.colliderWidth * style.pixel, styled(look.stripColor, 1.0F, look, style), std::nullopt, style.key);
			if (strip.flat) {
				continue;
			}
			const Foundation::Vec2	anchorLeft = frame.toScreen({left, strip.anchorLocalY});
			const Foundation::Vec2	anchorRight = frame.toScreen({right, strip.anchorLocalY});
			const Foundation::Color anchor = styled(look.anchorColor, 1.0F, look, style);
			Renderer::Primitives::drawLine(
				{.start = anchorLeft,
				 .end = anchorRight,
				 .style = {.color = anchor, .width = look.colliderWidth * style.pixel},
				 .id = "item_anchor",
				 .key = style.key}
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

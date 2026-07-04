#include "RegionMinimapPanel.h"

#include <components/panel/Panel.h>
#include <font/FontRenderer.h>
#include <graphics/ClipTypes.h>
#include <graphics/PrimitiveStyles.h>
#include <primitives/Primitives.h>
#include <theme/Tokens.h>
#include <theme/Variants.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace world_sim {

namespace {
	constexpr float kDotRadius = 2.0F;
	constexpr float kMarkerHalf = 4.0F;	 // crash-site diamond half-diagonal
	constexpr float kChevronTip = 4.5F;	 // chevron tip distance from its anchor
	constexpr float kChevronBase = 3.0F; // chevron base half-width
	constexpr float kGridStepMeters = 32.0F;
	constexpr float kDegreeRadius = 1.5F;
	constexpr float kCoordPad = 6.0F;

	float textScale(float px) { return px / 16.0F; }

	UI::Panel makePanel(Foundation::Vec2 position, Foundation::Vec2 size) {
		return UI::Panel(UI::Panel::Args{
			.position = position,
			.size = size,
			.title = "Region",
			.variant = UI::PanelVariant::Panel,
			.accent = UI::PanelAccent::Data,
			.corners = true,
			.compact = true,
			.flush = true});
	}

	std::string formatDegrees(double deg) {
		char buf[16];
		std::snprintf(buf, sizeof(buf), "%.1f", std::abs(deg));
		return buf;
	}
} // namespace

RegionMinimapPanel::RegionMinimapPanel(const Args& args)
	: panelWidth(args.width), id(args.id) {
	// Flush body => bodyBounds().y - position.y is exactly the header band.
	headerHeight = makePanel({0.0F, 0.0F}, {panelWidth, kBodyHeight}).bodyBounds().y;
	size = {panelWidth, headerHeight + kBodyHeight};
}

void RegionMinimapPanel::setAnchorPosition(float x, float y) {
	position = {x - panelWidth, y};
}

void RegionMinimapPanel::setContext(Foundation::Vec2 site, double latDeg, double lonDeg) {
	crashSite = site;
	coordLat = formatDegrees(latDeg);
	coordLatHemi = latDeg >= 0.0 ? "N" : "S";
	coordLon = formatDegrees(lonDeg);
	coordLonHemi = lonDeg >= 0.0 ? "E" : "W";
	hasContext = true;
}

void RegionMinimapPanel::updateData(const Foundation::Rect& visibleRect, const std::vector<adapters::ColonistData>& colonists) {
	viewRect = visibleRect;
	colonistDots.clear();
	colonistDots.reserve(colonists.size());
	for (const auto& colonist : colonists) {
		colonistDots.push_back(colonist.position);
	}
}

Foundation::Rect RegionMinimapPanel::bodyRect() const {
	return {position.x, position.y + headerHeight, panelWidth, kBodyHeight};
}

Foundation::Rect RegionMinimapPanel::windowRect() const {
	// Window width is fixed; height follows the body aspect so the projection
	// is uniform with no dead margins.
	const float windowH = kWindowMeters * (kBodyHeight / panelWidth);
	return {crashSite.x - kWindowMeters * 0.5F, crashSite.y - windowH * 0.5F, kWindowMeters, windowH};
}

Foundation::Vec2 RegionMinimapPanel::worldToMap(Foundation::Vec2 world) const {
	const Foundation::Rect window = windowRect();
	const Foundation::Rect body = bodyRect();
	const float scale = body.width / window.width;
	return {body.x + (world.x - window.x) * scale, body.y + (world.y - window.y) * scale};
}

bool RegionMinimapPanel::containsPoint(Foundation::Vec2 point) const {
	return point.x >= position.x && point.x <= position.x + size.x && point.y >= position.y && point.y <= position.y + size.y;
}

bool RegionMinimapPanel::handleEvent(UI::InputEvent& event) {
	// Not interactive yet (click-to-navigate is the Minimap epic), but clicks
	// over the panel must not fall through and select world entities beneath.
	if (event.type != UI::InputEvent::Type::MouseDown && event.type != UI::InputEvent::Type::MouseUp) {
		return false;
	}
	if (!containsPoint(event.position)) {
		return false;
	}
	event.consume();
	return true;
}

Foundation::Rect RegionMinimapPanel::getBounds() const {
	return {position.x, position.y, size.x, size.y};
}

void RegionMinimapPanel::render() {
	using Renderer::Primitives::drawRect;

	makePanel(position, size).render();

	const Foundation::Rect body = bodyRect();

	// Flat inset-tinted map surface (terrain rendering is out of scope).
	drawRect(Renderer::Primitives::RectArgs{
		.bounds = body,
		.style = {.fill = UI::bg_inset},
		.id = "minimap_surface"});

	if (!hasContext) {
		return;
	}

	renderGrid(body);

	// Clip the world-projected marks to the map surface.
	Renderer::Primitives::pushClip(Foundation::ClipSettings{.shape = Foundation::ClipRect{.bounds = body}});
	renderViewportRect();
	renderCrashMarker();
	renderColonists();
	Renderer::Primitives::popClip();

	renderCoordLabel(body);

	// Bearing chevrons sit on the inset perimeter, above the clipped marks.
	const Foundation::Rect window = windowRect();
	for (const Foundation::Vec2& dot : colonistDots) {
		const bool inside = dot.x >= window.x && dot.x <= window.x + window.width && dot.y >= window.y && dot.y <= window.y + window.height;
		if (!inside) {
			renderBearingChevron(body, dot);
		}
	}
}

void RegionMinimapPanel::renderGrid(const Foundation::Rect& body) const {
	using Renderer::Primitives::drawLine;

	const Foundation::Rect window = windowRect();
	const float scale = body.width / window.width;
	const float stepPx = kGridStepMeters * scale;

	for (float x = body.x + stepPx; x < body.x + body.width - 0.5F; x += stepPx) {
		drawLine(Renderer::Primitives::LineArgs{
			.start = {x, body.y},
			.end = {x, body.y + body.height},
			.style = {.color = UI::line_hairline, .width = 1.0F}});
	}
	for (float y = body.y + stepPx; y < body.y + body.height - 0.5F; y += stepPx) {
		drawLine(Renderer::Primitives::LineArgs{
			.start = {body.x, y},
			.end = {body.x + body.width, y},
			.style = {.color = UI::line_hairline, .width = 1.0F}});
	}
}

void RegionMinimapPanel::renderViewportRect() const {
	const Foundation::Vec2 topLeft = worldToMap({viewRect.x, viewRect.y});
	const Foundation::Vec2 bottomRight = worldToMap({viewRect.x + viewRect.width, viewRect.y + viewRect.height});
	const Foundation::Rect mapped{topLeft.x, topLeft.y, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y};
	if (mapped.width <= 0.0F || mapped.height <= 0.0F) {
		return;
	}
	Renderer::Primitives::drawRect(Renderer::Primitives::RectArgs{
		.bounds = mapped,
		.style = {.fill = {0.0F, 0.0F, 0.0F, 0.0F},
				  .border = Foundation::BorderStyle{.color = UI::accent, .width = UI::bw, .position = Foundation::BorderPosition::Inside}},
		.id = "minimap_viewport"});
}

void RegionMinimapPanel::renderCrashMarker() const {
	const Foundation::Vec2 c = worldToMap(crashSite);
	const Foundation::Vec2 verts[4] = {
		{c.x, c.y - kMarkerHalf}, {c.x + kMarkerHalf, c.y}, {c.x, c.y + kMarkerHalf}, {c.x - kMarkerHalf, c.y}};
	const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
	Renderer::Primitives::drawTriangles(Renderer::Primitives::TrianglesArgs{
		.vertices = verts,
		.indices = indices,
		.vertexCount = 4,
		.indexCount = 6,
		.color = UI::accent,
		.id = "minimap_crash_marker"});
}

void RegionMinimapPanel::renderColonists() const {
	const Foundation::Rect window = windowRect();
	for (const Foundation::Vec2& dot : colonistDots) {
		const bool inside = dot.x >= window.x && dot.x <= window.x + window.width && dot.y >= window.y && dot.y <= window.y + window.height;
		if (!inside) {
			continue;
		}
		Renderer::Primitives::drawCircle(Renderer::Primitives::CircleArgs{
			.center = worldToMap(dot),
			.radius = kDotRadius,
			.style = {.fill = UI::text_bright},
			.id = "minimap_colonist"});
	}
}

void RegionMinimapPanel::renderCoordLabel(const Foundation::Rect& body) const {
	using Renderer::Primitives::drawCircle;
	using Renderer::Primitives::drawText;

	const ui::FontRenderer* fr = Renderer::Primitives::getFontRenderer();
	auto measure = [&](const std::string& s) -> float {
		if (fr != nullptr) {
			return fr->MeasureText(s, textScale(UI::fs_2xs), UI::fontMono, 0.0F).x;
		}
		return static_cast<float>(s.size()) * UI::fs_2xs * 0.55F;
	};

	// "14.2°N · 9.8°W" with the degree rings and middot drawn as vectors
	// (symbols are iconography, not font glyphs -- same treatment as TopBar).
	const float textY = body.y + body.height - UI::fs_2xs - kCoordPad + 2.0F;
	float x = body.x + kCoordPad;

	auto drawRun = [&](const std::string& s) {
		drawText(Renderer::Primitives::TextArgs{
			.text = s,
			.position = {x, textY},
			.scale = textScale(UI::fs_2xs),
			.color = UI::data_bright,
			.font = UI::fontMono,
			.hAlign = Foundation::HorizontalAlign::Left,
			.vAlign = Foundation::VerticalAlign::Top});
		x += measure(s);
	};
	auto drawDegree = [&]() {
		drawCircle(Renderer::Primitives::CircleArgs{
			.center = {x + kDegreeRadius + 1.0F, textY + 2.0F},
			.radius = kDegreeRadius,
			.style = {.fill = {0.0F, 0.0F, 0.0F, 0.0F},
					  .border = Foundation::BorderStyle{.color = UI::data_bright, .width = 1.0F}}});
		x += kDegreeRadius * 2.0F + 2.0F;
	};

	drawRun(coordLat);
	drawDegree();
	drawRun(coordLatHemi);
	x += UI::space_1_5;
	drawCircle(Renderer::Primitives::CircleArgs{
		.center = {x, textY + UI::fs_2xs * 0.5F},
		.radius = 1.4F,
		.style = {.fill = UI::data_bright}});
	x += UI::space_1_5;
	drawRun(coordLon);
	drawDegree();
	drawRun(coordLonHemi);
}

void RegionMinimapPanel::renderBearingChevron(const Foundation::Rect& body, Foundation::Vec2 colonist) const {
	const float dx = colonist.x - crashSite.x;
	const float dy = colonist.y - crashSite.y;
	const float len = std::sqrt(dx * dx + dy * dy);
	if (len <= 0.0F) {
		return;
	}
	const float nx = dx / len;
	const float ny = dy / len;

	// Project the bearing onto the inset perimeter (prototype edgePoint()).
	const float half = 0.5F - kEdgeInset;
	const float denom = std::max(std::abs(nx), std::abs(ny));
	const float s = half / denom;
	const Foundation::Vec2 anchor{
		body.x + (0.5F + nx * s) * body.width,
		body.y + (0.5F + ny * s) * body.height};

	// Small triangle pointing along the bearing.
	const Foundation::Vec2 tip{anchor.x + nx * kChevronTip, anchor.y + ny * kChevronTip};
	const Foundation::Vec2 baseA{anchor.x - nx * kChevronBase - ny * kChevronBase, anchor.y - ny * kChevronBase + nx * kChevronBase};
	const Foundation::Vec2 baseB{anchor.x - nx * kChevronBase + ny * kChevronBase, anchor.y - ny * kChevronBase - nx * kChevronBase};
	const Foundation::Vec2 verts[3] = {tip, baseA, baseB};
	const uint16_t indices[3] = {0, 1, 2};
	Renderer::Primitives::drawTriangles(Renderer::Primitives::TrianglesArgs{
		.vertices = verts,
		.indices = indices,
		.vertexCount = 3,
		.indexCount = 3,
		.color = UI::accent_bright,
		.id = "minimap_bearing"});
}

}  // namespace world_sim

#include "RegionMinimapPanel.h"

#include "scenes/shared/SalalUi.h"

#include <primitives/Primitives.h>
#include <ui/Context.h>
#include <ui/shapes/Circle.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>

namespace world_sim {

	namespace {

		// The map window's width in world metres. The default 100% view is about 128 m wide, so 256 m
		// shows it at half scale with working room around the clearing; the window's height follows the
		// map's aspect (about 163 m).
		constexpr float kWindowMeters = 256.0F;
		constexpr float kBodyHeight = 148.0F; // 232 x 148, the prototype's 100:64 canvas
		constexpr float kEdgeInset = 0.12F;	  // the chevrons' perimeter, inset by this fraction of the map
		constexpr float kDotRadius = 2.0F;
		constexpr float kMarkerHalf = 4.0F;	 // the crash-site diamond's half-diagonal
		constexpr float kChevronTip = 4.5F;	 // a chevron's tip past its anchor
		constexpr float kChevronBase = 3.0F; // a chevron's base half-width
		constexpr float kGridStepMeters = 32.0F;
		constexpr float kDegree = 3.0F; // the drawn degree ring
		constexpr float kMiddot = 2.8F;
		constexpr float kCoordPad = 6.0F;

		Foundation::Color faded(Foundation::Color color, float alpha) {
			color.a *= alpha;
			return color;
		}

		std::string degrees(double value) {
			return std::format("{:.1f}", std::abs(value));
		}

	} // namespace

	// The map itself: the inset surface, and over it what has no component, the grid, the viewport, the
	// marker, the dots and the chevrons, drawn at its key. The coordinate label is its children.
	class RegionMinimapPanel::Map : public ui::Rectangle {
	  public:
		void setContext(Foundation::Vec2 site, double latDeg, double lonDeg) {
			crashSite = site;
			hasContext = true;
			lat.get()->setText(degrees(latDeg));
			latHemi.get()->setText(latDeg >= 0.0 ? "N" : "S");
			lon.get()->setText(degrees(lonDeg));
			lonHemi.get()->setText(lonDeg >= 0.0 ? "E" : "W");
			coords.get()->setVisible(true);
		}

		void setData(const Foundation::Rect& visibleRect, const std::vector<adapters::ColonistData>& colonists) {
			viewRect = visibleRect;
			dots.clear();
			dots.reserve(colonists.size());
			for (const adapters::ColonistData& colonist : colonists) {
				dots.push_back(colonist.position);
			}
		}

		[[nodiscard]] const char* typeName() const override { return "RegionMap"; }

	  protected:
		void onMount() override {
			const ui::Theme& theme = getContext()->getTheme();
			setStyle(ui::Style{.backgroundColor = theme.get(tokens::color::bgInset)}.extend(getDeclaredStyle()));
			// "14.2°N · 9.8°W", the rings and the middot drawn: the font's symbols aren't the theme's
			// iconography.
			coords = addChild<ui::Component>({
				.id = "minimap_coords",
				.visible = false,
				.layout = {
					.display = ui::Display::Flex,
					.alignItems = ui::AlignItems::Start,
					.position = ui::Position::Absolute,
					.inset = {.bottom = kCoordPad - 2.0F, .left = kCoordPad}
				},
			});
			const ui::Style run = world_sim::text(
				theme, {.family = ui::tokens::font::family::mono, .size = tokens::fontSize::fs2xs, .color = tokens::color::dataBright}
			);
			const Foundation::Color ink = theme.get(tokens::color::dataBright);
			const auto				ring = [&]() {
				 coords.addChild<ui::Circle>({
								 .size = {kDegree, kDegree},
								 .margin = {0.5F, 1.0F, 0.0F, 1.0F},
								 .style = {.backgroundColor = Foundation::Color::transparent(), .borderColor = ink, .borderWidth = 1.0F},
				 });
			};
			lat = coords.addChild<ui::Text>({.style = run});
			ring();
			latHemi = coords.addChild<ui::Text>({.style = run});
			const float apart = theme.get(tokens::spacing::space15) - (kMiddot * 0.5F);
			coords.addChild<ui::Circle>({
				.size = {kMiddot, kMiddot},
				.margin = {0.0F, apart},
				.layout = {.alignSelf = ui::AlignItems::Center},
				.style = {.backgroundColor = ink},
			});
			lon = coords.addChild<ui::Text>({.style = run});
			ring();
			lonHemi = coords.addChild<ui::Text>({.style = run});
		}

		void render() override {
			Rectangle::render();
			if (!hasContext) {
				return;
			}
			const ui::Theme&	   theme = getContext()->getTheme();
			const float			   alpha = opacity();
			const std::int32_t	   key = drawKey();
			const Foundation::Rect window = windowRect();
			const float			   scale = size.x / window.width;

			const Foundation::Color hairline = faded(theme.get(tokens::color::lineHairline), alpha);
			const float				step = kGridStepMeters * scale;
			for (float x = step; x < size.x - 0.5F; x += step) {
				Renderer::Primitives::drawLine(
					{.start = {x, 0.0F}, .end = {x, size.y}, .style = {.color = hairline, .width = 1.0F}, .key = key}
				);
			}
			for (float y = step; y < size.y - 0.5F; y += step) {
				Renderer::Primitives::drawLine(
					{.start = {0.0F, y}, .end = {size.x, y}, .style = {.color = hairline, .width = 1.0F}, .key = key}
				);
			}

			// The world-projected marks stop at the map's edge.
			Renderer::Primitives::pushClip({0.0F, 0.0F, size.x, size.y});
			const Foundation::Color accent = faded(theme.get(tokens::color::accent), alpha);
			const Foundation::Vec2	topLeft = toMap({viewRect.x, viewRect.y});
			const Foundation::Vec2	bottomRight = toMap({viewRect.x + viewRect.width, viewRect.y + viewRect.height});
			if (bottomRight.x > topLeft.x && bottomRight.y > topLeft.y) {
				Renderer::Primitives::drawRect({
					.bounds = {topLeft.x, topLeft.y, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y},
					.style =
						{.fill = Foundation::Color::transparent(),
						 .border =
							 Foundation::BorderStyle{
								 .color = accent, .width = theme.get(tokens::border::bw), .position = Foundation::BorderPosition::Inside
							 }},
					.id = "minimap_viewport",
					.key = key,
				});
			}
			const Foundation::Vec2 site = toMap(crashSite);
			const Foundation::Vec2 marker[4] = {
				{site.x, site.y - kMarkerHalf},
				{site.x + kMarkerHalf, site.y},
				{site.x, site.y + kMarkerHalf},
				{site.x - kMarkerHalf, site.y}
			};
			const std::uint16_t markerIndices[6] = {0, 1, 2, 0, 2, 3};
			Renderer::Primitives::drawTriangles({
				.vertices = marker,
				.indices = markerIndices,
				.vertexCount = 4,
				.indexCount = 6,
				.color = accent,
				.id = "minimap_crash_marker",
				.key = key,
			});
			const Foundation::Color dot = faded(theme.get(tokens::color::textBright), alpha);
			for (const Foundation::Vec2& colonist : dots) {
				if (inWindow(window, colonist)) {
					Renderer::Primitives::drawCircle(
						{.center = toMap(colonist), .radius = kDotRadius, .style = {.fill = dot}, .id = "minimap_colonist", .key = key}
					);
				}
			}
			Renderer::Primitives::popClip();

			// A colonist outside the window: a chevron on the inset perimeter along its bearing.
			const Foundation::Color bright = faded(theme.get(tokens::color::accentBright), alpha);
			for (const Foundation::Vec2& colonist : dots) {
				if (!inWindow(window, colonist)) {
					drawBearing(colonist, bright, key);
				}
			}
		}

	  private:
		static bool inWindow(const Foundation::Rect& window, Foundation::Vec2 point) {
			return point.x >= window.x && point.x <= window.x + window.width && point.y >= window.y && point.y <= window.y + window.height;
		}

		// Its width fixed, its height the map's aspect, so the projection is uniform with no dead margins.
		[[nodiscard]] Foundation::Rect windowRect() const {
			const float height = kWindowMeters * (size.y / std::max(size.x, 1.0F));
			return {crashSite.x - (kWindowMeters * 0.5F), crashSite.y - (height * 0.5F), kWindowMeters, height};
		}

		[[nodiscard]] Foundation::Vec2 toMap(Foundation::Vec2 world) const {
			const Foundation::Rect window = windowRect();
			const float			   scale = size.x / window.width;
			return {(world.x - window.x) * scale, (world.y - window.y) * scale};
		}

		// The bearing projected onto the inset perimeter (the prototype's edgePoint()).
		void drawBearing(Foundation::Vec2 colonist, Foundation::Color color, std::int32_t key) const {
			const float dx = colonist.x - crashSite.x;
			const float dy = colonist.y - crashSite.y;
			const float length = std::sqrt((dx * dx) + (dy * dy));
			if (length <= 0.0F) {
				return;
			}
			const float			   nx = dx / length;
			const float			   ny = dy / length;
			const float			   reach = (0.5F - kEdgeInset) / std::max(std::abs(nx), std::abs(ny));
			const Foundation::Vec2 anchor{(0.5F + nx * reach) * size.x, (0.5F + ny * reach) * size.y};
			const Foundation::Vec2 points[3] = {
				{anchor.x + nx * kChevronTip, anchor.y + ny * kChevronTip},
				{anchor.x - nx * kChevronBase - ny * kChevronBase, anchor.y - ny * kChevronBase + nx * kChevronBase},
				{anchor.x - nx * kChevronBase + ny * kChevronBase, anchor.y - ny * kChevronBase - nx * kChevronBase},
			};
			const std::uint16_t indices[3] = {0, 1, 2};
			Renderer::Primitives::drawTriangles({
				.vertices = points,
				.indices = indices,
				.vertexCount = 3,
				.indexCount = 3,
				.color = color,
				.id = "minimap_bearing",
				.key = key,
			});
		}

		Foundation::Vec2			  crashSite{0.0F, 0.0F};
		Foundation::Rect			  viewRect{};
		std::vector<Foundation::Vec2> dots;
		bool						  hasContext = false;
		ui::Ref<ui::Component>		  coords;
		ui::Ref<ui::Text>			  lat;
		ui::Ref<ui::Text>			  latHemi;
		ui::Ref<ui::Text>			  lon;
		ui::Ref<ui::Text>			  lonHemi;
	};

	RegionMinimapPanel::RegionMinimapPanel()
		: theme::Panel(
			  {.title = "Region",
			   .variant = theme::PanelVariant::Panel,
			   .accent = theme::PanelAccent::Data,
			   .corners = true,
			   .compact = true,
			   .flush = true}
		  ) {}

	void RegionMinimapPanel::onMount() {
		if (getSizeSpec().width.isAuto()) {
			setSize({kWidth, getSizeSpec().height});
		}
		theme::Panel::onMount();
		map = body().addChild<Map>({.id = "minimap_surface", .size = {ui::percent(100.0F), kBodyHeight}});
	}

	void RegionMinimapPanel::setContext(Foundation::Vec2 crashSite, double latDeg, double lonDeg) {
		if (Map* shown = map.get()) {
			shown->setContext(crashSite, latDeg, lonDeg);
		}
	}

	void RegionMinimapPanel::updateData(const Foundation::Rect& visibleRect, const std::vector<adapters::ColonistData>& colonists) {
		if (Map* shown = map.get()) {
			shown->setData(visibleRect, colonists);
		}
	}

} // namespace world_sim

#include "VisionOverlay.h"

#include <ecs/World.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Transform.h>
#include <nav/NavCoords.h>

#include <visibility/ForestShape.h>
#include <visibility/SightClip.h>
#include <visibility/SightPolygon.h>

#include <primitives/Primitives.h>

#include <cmath>
#include <string>
#include <vector>

namespace world_sim {

	namespace {
		// The sight shape's fill: a very faint cyan fan from the build, which it is star-shaped round.
		constexpr Foundation::Color kShapeFill{0.3F, 0.9F, 0.9F, 0.08F};
		// The forest shape of his sight where he stands.
		constexpr Foundation::Color kForestOutline{0.45F, 1.0F, 0.35F, 0.8F};
		// The hard polygon the opaque bodies leave, cut to his sight where he stands.
		constexpr Foundation::Color kHardOutline{0.4F, 1.0F, 1.0F, 0.65F};
		// Bodies by kind.
		constexpr Foundation::Color kOpaqueBody{1.0F, 0.2F, 0.2F, 0.9F};
		constexpr Foundation::Color kSoftBody{1.0F, 0.85F, 0.2F, 0.9F};
		// The soft ray's width, a circle that wide round the build eye.
		constexpr Foundation::Color kSoftRay{1.0F, 0.4F, 1.0F, 0.8F};

		// Post-entity world flush domain: these z values only order this overlay
		// against other post-entity world draws; committed construction flushes
		// earlier, so the toggled overlay always paints over it.
		constexpr int kZShapeFill = 62;
		constexpr int kZOutline	  = 64;
		constexpr int kZBody	  = 65;

		constexpr int kSoftRaySegments = 32;

		Foundation::Vec2 toScreen(const world::WorldCamera& camera, const geometry::Vec2i64& point, int viewportW, int viewportH, float pixelsPerMeter) {
			const glm::vec2 meters = nav::toMeters(point);
			const auto		screen = camera.worldToScreen(meters.x, meters.y, viewportW, viewportH, pixelsPerMeter);
			return Foundation::Vec2{screen.x, screen.y};
		}

		void drawOutline(const std::vector<Foundation::Vec2>& ring, const Foundation::Color& color, int key, const std::string& idBase) {
			for (std::size_t index = 0; index < ring.size(); ++index) {
				const std::string edgeId = idBase + std::to_string(index);
				Renderer::Primitives::drawLine(
					Renderer::Primitives::LineArgs{
						.start = ring[index],
						.end   = ring[(index + 1) % ring.size()],
						.style = Foundation::LineStyle{.color = color, .width = 1.0F},
						.id	   = edgeId.c_str(),
						.key   = key,
					}
				);
			}
		}
	} // namespace

	VisionOverlay::VisionOverlay(const Args& args)
		: ecsWorld(args.world),
		  camera(args.camera),
		  vision(args.vision) {}

	void VisionOverlay::render(int viewportW, int viewportH) {
		if (!active || ecsWorld == nullptr || camera == nullptr || vision == nullptr) {
			return;
		}
		const auto screenRing = [&](const geometry::Ring& ring) {
			std::vector<Foundation::Vec2> out;
			out.reserve(ring.size());
			for (const geometry::Vec2i64& vertex : ring) {
				out.push_back(toScreen(*camera, vertex, viewportW, viewportH, kPixelsPerMeter));
			}
			return out;
		};
		const std::int64_t softRayWidthMm = vision->sightTunables().softShadowRayWidthMm;

		for (auto [entity, pos, memory] : ecsWorld->view<ecs::Position, ecs::Memory>()) {
			(void)memory; // view filters by Memory; value not read here
			const auto shape = vision->sightShape(entity);
			if (!shape.has_value()) {
				continue;
			}
			const std::string		idBase	 = "vis_" + std::to_string(static_cast<unsigned long long>(entity));
			const geometry::Vec2i64 standing = nav::toMm(pos.value);
			const geometry::Vec2i64 build	 = nav::toMm(shape->center);
			const std::int64_t		radiusMm = std::llround(static_cast<double>(shape->radius) * 1000.0);
			if (radiusMm <= 0) {
				continue;
			}
			const geometry::Ring	inSight	 = geometry::sightPolygon(standing, radiusMm);
			const geometry::Ring	forest	 = shape->forest != nullptr ? geometry::forestShape(standing, *shape->ranges) : inSight;

			// The shape he sees by: the forest shape where he stands, or on the polygon path the polygon
			// round its build cut to it.
			const geometry::Ring sight = shape->polygon != nullptr ? geometry::starClippedToConvex(*shape->polygon, build, forest) : forest;
			const std::vector<Foundation::Vec2> sightScreen = screenRing(sight);
			if (sightScreen.size() >= 3) {
				std::vector<Foundation::Vec2> vertices;
				vertices.reserve(sightScreen.size() + 1);
				vertices.push_back(toScreen(*camera, shape->polygon != nullptr ? build : standing, viewportW, viewportH, kPixelsPerMeter));
				vertices.insert(vertices.end(), sightScreen.begin(), sightScreen.end());
				std::vector<uint16_t> indices;
				indices.reserve(sightScreen.size() * 3);
				for (std::size_t index = 0; index < sightScreen.size(); ++index) {
					indices.push_back(0);
					indices.push_back(static_cast<uint16_t>(index + 1));
					indices.push_back(static_cast<uint16_t>((index + 1) % sightScreen.size() + 1));
				}
				const std::string fanId = idBase + "_fan";
				Renderer::Primitives::drawTriangles(
					Renderer::Primitives::TrianglesArgs{
						.vertices	 = vertices.data(),
						.indices	 = indices.data(),
						.vertexCount = vertices.size(),
						.indexCount	 = indices.size(),
						.color		 = kShapeFill,
						.id			 = fanId.c_str(),
						.key		 = kZShapeFill,
					}
				);
			}

			const std::optional<ecs::VisionSystem::SightBodies> bodies = vision->sightBodies(entity);
			if (!bodies.has_value()) {
				continue;
			}
			drawOutline(screenRing(forest), kForestOutline, kZOutline, idBase + "_forest_");
			if (shape->polygon != nullptr) {
				const geometry::Ring hard = geometry::starClippedToConvex(vision->hardPolygon(build, bodies->reachMm), build, inSight);
				drawOutline(screenRing(hard), kHardOutline, kZOutline, idBase + "_hard_");
			}
			for (std::size_t body = 0; body < bodies->bodies->size(); ++body) {
				const vision::SightBody& sightBody = (*bodies->bodies)[body];
				const Foundation::Color& color	   = sightBody.kind == vision::BodyKind::Opaque ? kOpaqueBody : kSoftBody;
				for (std::size_t edge = 0; edge < sightBody.edges.size(); ++edge) {
					const std::string edgeId = idBase + "_body_" + std::to_string(body) + "_" + std::to_string(edge);
					Renderer::Primitives::drawLine(
						Renderer::Primitives::LineArgs{
							.start = toScreen(*camera, sightBody.edges[edge].a, viewportW, viewportH, kPixelsPerMeter),
							.end   = toScreen(*camera, sightBody.edges[edge].b, viewportW, viewportH, kPixelsPerMeter),
							.style = Foundation::LineStyle{.color = color, .width = 1.5F},
							.id	   = edgeId.c_str(),
							.key   = kZBody,
						}
					);
				}
			}

			std::vector<Foundation::Vec2> softRay;
			softRay.reserve(kSoftRaySegments);
			const double halfWidthMm = static_cast<double>(softRayWidthMm) / 2.0;
			for (int segment = 0; segment < kSoftRaySegments; ++segment) {
				const double angle = 2.0 * 3.14159265358979323846 * static_cast<double>(segment) / static_cast<double>(kSoftRaySegments);
				const geometry::Vec2i64 point{
					bodies->eyeCentre.x + std::llround(halfWidthMm * std::cos(angle)), bodies->eyeCentre.y + std::llround(halfWidthMm * std::sin(angle))
				};
				softRay.push_back(toScreen(*camera, point, viewportW, viewportH, kPixelsPerMeter));
			}
			drawOutline(softRay, kSoftRay, kZBody, idBase + "_ray_");
		}
	}

} // namespace world_sim

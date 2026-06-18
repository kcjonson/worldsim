#include "VisionOverlay.h"

#include <ecs/World.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Transform.h>

#include <primitives/Primitives.h>

#include <string>
#include <vector>

namespace world_sim {

	namespace {
		// Polygon fill: very faint cyan fan (star-shaped, so a triangle fan from the
		// observer works exactly -- the polygon is star-shaped by construction).
		constexpr Foundation::Color kPolyFill{0.3F, 0.9F, 0.9F, 0.08F};
		// Polygon outline: brighter cyan so it reads over the fill.
		constexpr Foundation::Color kPolyOutline{0.4F, 1.0F, 1.0F, 0.65F};
		// Occluder segments: red so they stand out against world geometry.
		constexpr Foundation::Color kOccluder{1.0F, 0.2F, 0.2F, 0.85F};

		constexpr int kZPolyFill = 62;
		constexpr int kZPolyOutline = 64;
		constexpr int kZOccluder = 65;

		// All geometry is stored in integer millimeters; render space is meters.
		constexpr float kMmToMeters = 0.001F;
	} // namespace

	VisionOverlay::VisionOverlay(const Args& args)
		: ecsWorld(args.world),
		  camera(args.camera),
		  vision(args.vision) {}

	void VisionOverlay::render(int viewportW, int viewportH) {
		if (!active || ecsWorld == nullptr || camera == nullptr || vision == nullptr) {
			return;
		}

		// --- Occluder segments (drawn once for the whole index, not per-entity) ---
		{
			const auto& occluders = vision->geometry().occluders();
			for (std::size_t i = 0; i < occluders.size(); ++i) {
				const auto& rec = occluders[i];
				const float ax = static_cast<float>(rec.seg.a.x) * kMmToMeters;
				const float ay = static_cast<float>(rec.seg.a.y) * kMmToMeters;
				const float bx = static_cast<float>(rec.seg.b.x) * kMmToMeters;
				const float by = static_cast<float>(rec.seg.b.y) * kMmToMeters;

				const auto sa = camera->worldToScreen(ax, ay, viewportW, viewportH, kPixelsPerMeter);
				const auto sb = camera->worldToScreen(bx, by, viewportW, viewportH, kPixelsPerMeter);

				const std::string occId = "vis_occ_" + std::to_string(i);
				Renderer::Primitives::drawLine(
					Renderer::Primitives::LineArgs{
						.start = Foundation::Vec2{sa.x, sa.y},
						.end = Foundation::Vec2{sb.x, sb.y},
						.style = Foundation::LineStyle{.color = kOccluder, .width = 1.5F},
						.id = occId.c_str(),
						.zIndex = kZOccluder,
					}
				);
			}
		}

		// --- Per-observer visibility polygons ---
		for (auto [entity, pos, memory] : ecsWorld->view<ecs::Position, ecs::Memory>()) {
			(void)memory; // view filters by Memory; value not read here
			const geometry::Ring* ring = vision->visibilityPolygon(entity);
			if (ring == nullptr || ring->size() < 3) {
				continue;
			}

			const std::size_t n = ring->size();
			const std::string idBase = "vis_poly_" + std::to_string(static_cast<unsigned long long>(entity));

			// Observer screen position (meters).
			const float ox = pos.value.x;
			const float oy = pos.value.y;
			const auto	os = camera->worldToScreen(ox, oy, viewportW, viewportH, kPixelsPerMeter);

			// Project the whole ring once; reuse for both fan and outline.
			std::vector<Foundation::Vec2> screenRing;
			screenRing.reserve(n);
			for (const auto& v : *ring) {
				const float wx = static_cast<float>(v.x) * kMmToMeters;
				const float wy = static_cast<float>(v.y) * kMmToMeters;
				const auto	s = camera->worldToScreen(wx, wy, viewportW, viewportH, kPixelsPerMeter);
				screenRing.push_back(Foundation::Vec2{s.x, s.y});
			}

			// Triangle fan fill: observer at center, each consecutive edge on the ring.
			// n triangles, 3n indices, n+1 vertices (observer + ring verts).
			{
				std::vector<Foundation::Vec2> verts;
				verts.reserve(n + 1);
				verts.push_back(Foundation::Vec2{os.x, os.y}); // index 0 = observer
				for (const auto& sv : screenRing) {
					verts.push_back(sv);
				}

				std::vector<uint16_t> idx;
				idx.reserve(n * 3);
				for (std::size_t i = 0; i < n; ++i) {
					idx.push_back(0);								  // observer
					idx.push_back(static_cast<uint16_t>(i + 1));	  // ring[i]
					idx.push_back(static_cast<uint16_t>((i + 1) % n + 1)); // ring[(i+1)%n]
				}

				const std::string fanId = idBase + "_fan";
				Renderer::Primitives::drawTriangles(
					Renderer::Primitives::TrianglesArgs{
						.vertices = verts.data(),
						.indices = idx.data(),
						.vertexCount = verts.size(),
						.indexCount = idx.size(),
						.color = kPolyFill,
						.id = fanId.c_str(),
						.zIndex = kZPolyFill,
					}
				);
			}

			// Outline: draw each edge of the closed ring.
			for (std::size_t i = 0; i < n; ++i) {
				const std::string edgeId = idBase + "_e" + std::to_string(i);
				Renderer::Primitives::drawLine(
					Renderer::Primitives::LineArgs{
						.start = screenRing[i],
						.end = screenRing[(i + 1) % n],
						.style = Foundation::LineStyle{.color = kPolyOutline, .width = 1.0F},
						.id = edgeId.c_str(),
						.zIndex = kZPolyOutline,
					}
				);
			}
		}
	}

} // namespace world_sim

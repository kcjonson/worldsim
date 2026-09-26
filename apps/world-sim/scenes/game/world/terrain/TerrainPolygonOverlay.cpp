#include "TerrainPolygonOverlay.h"

#include <primitives/Primitives.h>
#include <world/chunk/Chunk.h>
#include <world/chunk/ChunkCoordinate.h>
#include <world/chunk/TerrainPolygons.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>

namespace world_sim {

	namespace {
		constexpr Foundation::Color kRingEdge{0.2F, 0.9F, 0.95F, 0.85F};	  // cyan
		constexpr Foundation::Color kSyntheticEdge{0.95F, 0.2F, 0.9F, 0.85F}; // magenta: apron closure, not a real shore (D4)
		constexpr Foundation::Color kNavRingEdge{1.0F, 0.9F, 0.15F, 0.8F};	  // yellow
		constexpr Foundation::Color kChunkOutline{1.0F, 1.0F, 1.0F, 0.18F};	  // faint white

		constexpr int kZChunkOutline = 40;
		constexpr int kZRingEdge = 42;
		constexpr int kZNavRingEdge = 44;
		constexpr int kZVertexDot = 46;

		// Ring vertices are integer mm, world-absolute (TerrainPolygons.h), unlike
		// the nav mesh's region-local mm: no chunk-offset math is needed here.
		constexpr float kMmToMeters = 0.001F;

		Foundation::Vec2 toMeters(const geometry::Vec2i64& v) {
			return Foundation::Vec2{static_cast<float>(v.x) * kMmToMeters, static_cast<float>(v.y) * kMmToMeters};
		}

		// Cheap per-ring AABB in world meters, so a ring far from the camera (in
		// another corner of a large chunk's apron) is skipped before projecting
		// every vertex to screen space.
		Foundation::Rect ringBoundsMeters(const geometry::Ring& ring) {
			float minX = std::numeric_limits<float>::max();
			float minY = std::numeric_limits<float>::max();
			float maxX = std::numeric_limits<float>::lowest();
			float maxY = std::numeric_limits<float>::lowest();
			for (const auto& v : ring) {
				const Foundation::Vec2 p = toMeters(v);
				minX = std::min(minX, p.x);
				minY = std::min(minY, p.y);
				maxX = std::max(maxX, p.x);
				maxY = std::max(maxY, p.y);
			}
			return Foundation::Rect{minX, minY, maxX - minX, maxY - minY};
		}
	} // namespace

	TerrainPolygonOverlay::TerrainPolygonOverlay(const Args& args)
		: camera(args.camera),
		  chunks(args.chunks) {}

	void TerrainPolygonOverlay::render(int viewportW, int viewportH) {
		if (!active || camera == nullptr || chunks == nullptr) {
			return;
		}

		const Foundation::Rect viewRect = camera->getVisibleRect(viewportW, viewportH, kPixelsPerMeter);
		const bool			   drawVertexDots = camera->worldDistanceToScreen(1.0F, kPixelsPerMeter) >= kVertexDotMinPixelsPerMeter;

		const auto									   corners = camera->getVisibleCorners(viewportW, viewportH, kPixelsPerMeter);
		const std::vector<const engine::world::Chunk*> visible = chunks->getVisibleChunks(corners.first, corners.second);

		// Draw one ring set (rings or navRings): every edge, colored by kind, with
		// small vertex dots once zoomed in enough to make them useful rather than
		// clutter. Synthetic apron-closure edges (D4) get their own color so they
		// are never mistaken for real shore.
		auto drawRings = [&](const std::vector<engine::world::TerrainRing>& rings,
							 const std::string&								idBase,
							 const char*									tag,
							 Foundation::Color								edgeColor,
							 float											lineWidth,
							 int											zIndex,
							 bool											flagSynthetic) {
			for (std::size_t ri = 0; ri < rings.size(); ++ri) {
				const engine::world::TerrainRing& terrainRing = rings[ri];
				const geometry::Ring&			  ring = terrainRing.ring;
				if (ring.size() < 2 || !ringBoundsMeters(ring).intersects(viewRect)) {
					continue;
				}

				const std::string ringIdBase = idBase + "_" + tag + "_" + std::to_string(ri);
				const std::size_t n = ring.size();
				for (std::size_t i = 0; i < n; ++i) {
					const std::size_t	   next = (i + 1) % n;
					const Foundation::Vec2 startWorld = toMeters(ring[i]);
					const Foundation::Vec2 endWorld = toMeters(ring[next]);
					const Foundation::Vec2 start = camera->worldToScreen(startWorld.x, startWorld.y, viewportW, viewportH, kPixelsPerMeter);
					const Foundation::Vec2 end = camera->worldToScreen(endWorld.x, endWorld.y, viewportW, viewportH, kPixelsPerMeter);

					const bool synthetic = flagSynthetic && i < terrainRing.profiles.size() &&
										   (terrainRing.profiles[i].flags & engine::world::ShoreProfile::kFlagSynthetic) != 0;

					const std::string edgeId = ringIdBase + "_e" + std::to_string(i);
					Renderer::Primitives::drawLine(
						Renderer::Primitives::LineArgs{
							.start = start,
							.end = end,
							.style =
								Foundation::LineStyle{
									.color = synthetic ? kSyntheticEdge : edgeColor,
									.width = lineWidth,
								},
							.id = edgeId.c_str(),
							.zIndex = zIndex,
						}
					);

					if (drawVertexDots) {
						const std::string dotId = ringIdBase + "_v" + std::to_string(i);
						Renderer::Primitives::drawCircle(
							Renderer::Primitives::CircleArgs{
								.center = start,
								.radius = 2.5F,
								.style = Foundation::CircleStyle{.fill = edgeColor},
								.id = dotId.c_str(),
								.zIndex = kZVertexDot,
							}
						);
					}
				}
			}
		};

		for (const engine::world::Chunk* chunk : visible) {
			if (chunk == nullptr || !chunk->isReady()) {
				continue;
			}

			const engine::world::ChunkCoordinate coord = chunk->coordinate();
			const engine::world::WorldPosition	 origin = coord.origin();
			const std::string					 idBase = "terrainpoly_" + std::to_string(coord.x) + "_" + std::to_string(coord.y);

			// Chunk square outline, thin and faint, so seams between chunks are
			// easy to spot.
			const std::array<Foundation::Vec2, 4> squareCorners = {
				camera->worldToScreen(origin.x, origin.y, viewportW, viewportH, kPixelsPerMeter),
				camera->worldToScreen(origin.x + engine::world::kChunkWorldSize, origin.y, viewportW, viewportH, kPixelsPerMeter),
				camera->worldToScreen(
					origin.x + engine::world::kChunkWorldSize,
					origin.y + engine::world::kChunkWorldSize,
					viewportW,
					viewportH,
					kPixelsPerMeter
				),
				camera->worldToScreen(origin.x, origin.y + engine::world::kChunkWorldSize, viewportW, viewportH, kPixelsPerMeter),
			};
			for (int i = 0; i < 4; ++i) {
				const std::string edgeId = idBase + "_sq" + std::to_string(i);
				Renderer::Primitives::drawLine(
					Renderer::Primitives::LineArgs{
						.start = squareCorners[i],
						.end = squareCorners[(i + 1) % 4],
						.style =
							Foundation::LineStyle{
								.color = kChunkOutline,
								.width = 1.0F,
							},
						.id = edgeId.c_str(),
						.zIndex = kZChunkOutline,
					}
				);
			}

			const engine::world::ChunkTerrainPolygons& polys = chunk->terrainPolygons();
			drawRings(polys.rings, idBase, "ring", kRingEdge, 1.5F, kZRingEdge, /*flagSynthetic=*/true);
			drawRings(polys.navRings, idBase, "nav", kNavRingEdge, 1.0F, kZNavRingEdge, /*flagSynthetic=*/false);
		}
	}

} // namespace world_sim

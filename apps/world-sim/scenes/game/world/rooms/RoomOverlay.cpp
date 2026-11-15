#include "RoomOverlay.h"

#include <construction/RoomHitTest.h>
#include <core/Vec2i64.h>
#include <primitives/Primitives.h>
#include <vector/Tessellator.h>

#include <cstdint>
#include <string>
#include <vector>

namespace world_sim {

	namespace {
		// Tint applied to a room's interior; deliberately faint so foundation/floor
		// detail reads through.
		constexpr Foundation::Color kTint{0.35F, 0.75F, 1.0F, 0.2F};
		constexpr Foundation::Color kOutline{0.5F, 0.85F, 1.0F, 0.8F};
		constexpr Foundation::Color kLabel{1.0F, 1.0F, 1.0F, 0.95F};

		// Selected room: brighter fill + a gold outline (matches the gold the
		// SelectionSystem indicators use for foundations/walls/openings).
		constexpr Foundation::Color kSelectedTint{0.35F, 0.75F, 1.0F, 0.38F};
		constexpr Foundation::Color kSelectedOutline{1.0F, 0.85F, 0.0F, 0.95F};

		// Overlays draw in the post-entity world pass, a separate flush domain
		// from committed construction (which flushes before the entity pass), so
		// these z values only order overlay primitives against OTHER post-entity
		// world draws — a toggled overlay always paints over construction.
		constexpr int kZFill = 56;
		constexpr int kZOutline = 57;
		constexpr int kZLabel = 58;
		constexpr int kZSelected = 100; // gold highlight, matches SelectionSystem indicators
	} // namespace

	RoomOverlay::RoomOverlay(const Args& args)
		: ecsWorld(args.world),
		  camera(args.camera),
		  roomDetection(args.roomDetection) {}

	std::optional<std::uint64_t> RoomOverlay::handleClick(float screenX, float screenY, int viewportW, int viewportH) const {
		if (camera == nullptr || roomDetection == nullptr) {
			return std::nullopt;
		}

		// Same inversion the SelectionSystem uses: screen -> world meters, then
		// quantize to the integer-mm grid the rings live on.
		const auto world = camera->screenToWorld(screenX, screenY, viewportW, viewportH, kPixelsPerMeter);
		const auto clickMm = geometry::quantize(Foundation::Vec2{world.x, world.y});

		std::vector<construction::RoomHitCandidate> candidates;
		candidates.reserve(roomDetection->rooms().size());
		for (const auto& room : roomDetection->rooms()) {
			candidates.push_back(construction::RoomHitCandidate{room.roomId, &room.ring, &room.holes});
		}
		return construction::roomAtPoint(clickMm, candidates);
	}

	void RoomOverlay::syncFills() {
		const auto& rooms = roomDetection->rooms();
		fills.resize(rooms.size());
		renderer::Tessellator tessellator;
		for (std::size_t i = 0; i < rooms.size(); ++i) {
			const auto& room = rooms[i];
			RoomFill&	fill = fills[i];
			if (fill.roomId == room.roomId && fill.ring == room.ring && fill.holes == room.holes) {
				continue;
			}
			fill.roomId = room.roomId;
			fill.ring = room.ring;
			fill.holes = room.holes;
			fill.mesh.clear();
			if (room.ring.size() < 3) {
				continue;
			}

			// Relative to the ring's first vertex so float precision holds far from the
			// world origin. The ring is CCW and every hole CW, so the nonzero rule cuts
			// the holes out; the camera projection is affine, so the triangles stay valid
			// on screen.
			fill.origin = room.ring.front();
			const auto toPath = [&fill](const geometry::Ring& loop) {
				renderer::VectorPath path;
				path.vertices.reserve(loop.size());
				for (const auto& vertex : loop) {
					path.addVertex(geometry::dequantize(vertex - fill.origin));
				}
				return path;
			};
			std::vector<renderer::VectorPath> contours;
			contours.reserve(1 + room.holes.size());
			contours.push_back(toPath(room.ring));
			for (const auto& hole : room.holes) {
				contours.push_back(toPath(hole));
			}
			if (!tessellator.Tessellate(contours, fill.mesh)) {
				fill.mesh.clear();
			}
		}
	}

	void RoomOverlay::render(int viewportW, int viewportH) {
		if (!active || camera == nullptr || roomDetection == nullptr) {
			return;
		}

		syncFills();

		const auto project = [&](const geometry::Vec2i64& point) {
			const auto world = geometry::dequantize(point);
			return camera->worldToScreen(world.x, world.y, viewportW, viewportH, kPixelsPerMeter);
		};

		const auto& rooms = roomDetection->rooms();
		for (std::size_t roomIndex = 0; roomIndex < rooms.size(); ++roomIndex) {
			const auto&		room = rooms[roomIndex];
			const RoomFill& fill = fills[roomIndex];
			if (room.ring.size() < 3) {
				continue;
			}

			// Stable per-room primitive ids: the Primitives API takes a const char*, so
			// build the id strings here and keep them alive for the duration of the call.
			// roomId is monotonic, so this never aliases a retired room.
			const bool		  selected = (selectedRoomId != 0 && room.roomId == selectedRoomId);
			const std::string idSuffix = std::to_string(room.roomId);
			const std::string fillId = "room_fill_" + idSuffix;
			const std::string edgeId = "room_edge_" + idSuffix;
			const std::string labelId = "room_label_" + idSuffix;

			// The selected room gets a brighter tint at a higher z so it reads as picked.
			if (fill.mesh.indices.size() >= 3) {
				const auto originWorld = geometry::dequantize(fill.origin);
				screenPoints.clear();
				screenPoints.reserve(fill.mesh.vertices.size());
				for (const auto& vertex : fill.mesh.vertices) {
					screenPoints.push_back(
						camera->worldToScreen(originWorld.x + vertex.x, originWorld.y + vertex.y, viewportW, viewportH, kPixelsPerMeter)
					);
				}
				Renderer::Primitives::drawTriangles(
					Renderer::Primitives::TrianglesArgs{
						.vertices = screenPoints.data(),
						.indices = fill.mesh.indices.data(),
						.vertexCount = screenPoints.size(),
						.indexCount = fill.mesh.indices.size(),
						.color = selected ? kSelectedTint : kTint,
						.id = fillId.c_str(),
						.key = selected ? kZSelected : kZFill,
					}
				);
			}

			// Outline: the outer ring and every hole, per edge. Selected room uses the
			// gold ring at z 100, mirroring the SelectionSystem indicators.
			const Foundation::Color edgeColor = selected ? kSelectedOutline : kOutline;
			const float				edgeWidth = selected ? 3.0F : 2.0F;
			const int				edgeZ = selected ? kZSelected : kZOutline;
			const auto				outline = [&](const geometry::Ring& loop) {
				const std::size_t count = loop.size();
				for (std::size_t i = 0; i < count; ++i) {
					Renderer::Primitives::drawLine(
						Renderer::Primitives::LineArgs{
							.start = project(loop[i]),
							.end = project(loop[(i + 1) % count]),
							.style =
								Foundation::LineStyle{
									.color = edgeColor,
									.width = edgeWidth,
								},
							.id = edgeId.c_str(),
							.key = edgeZ,
						}
					);
				}
			};
			outline(room.ring);
			for (const auto& hole : room.holes) {
				outline(hole);
			}

			// Name label at the room's representative point, which lies outside every
			// hole, so it never lands in a room inside this one.
			if (!room.name.empty()) {
				Renderer::Primitives::drawText(
					Renderer::Primitives::TextArgs{
						.text = room.name,
						.position = project(room.rep),
						.color = kLabel,
						.id = labelId.c_str(),
						.key = kZLabel,
					}
				);
			}
		}
	}

} // namespace world_sim

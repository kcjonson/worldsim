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

		constexpr int kZFill = 56;	  // above foundation fills (~50-52)
		constexpr int kZOutline = 57; // below wall bands (~60-64)
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

		std::vector<engine::construction::RoomHitCandidate> candidates;
		candidates.reserve(roomDetection->rooms().size());
		for (const auto& room : roomDetection->rooms()) {
			candidates.push_back(engine::construction::RoomHitCandidate{room.roomId, &room.ring});
		}
		return engine::construction::roomAtPoint(clickMm, candidates);
	}

	void RoomOverlay::render(int viewportW, int viewportH) {
		if (!active || camera == nullptr || roomDetection == nullptr) {
			return;
		}

		// Stable per-room primitive ids: the Primitives API takes a const char*, so
		// build the id strings here and keep them alive for the duration of the call.
		// roomId is monotonic, so this never aliases a retired room.
		renderer::Tessellator tessellator;

		for (const auto& room : roomDetection->rooms()) {
			const std::size_t n = room.ring.size();
			if (n < 3) {
				continue;
			}

			// Project the room ring to screen space once; both the fill tessellation
			// and the edge outline draw from these points.
			std::vector<Foundation::Vec2> screen;
			screen.reserve(n);
			for (const auto& v : room.ring) {
				const auto w = geometry::dequantize(v);
				const auto s = camera->worldToScreen(w.x, w.y, viewportW, viewportH, kPixelsPerMeter);
				screen.push_back(Foundation::Vec2{s.x, s.y});
			}

			const bool		  selected = (selectedRoomId != 0 && room.roomId == selectedRoomId);
			const std::string idSuffix = std::to_string(room.roomId);
			const std::string fillId = "room_fill_" + idSuffix;
			const std::string edgeId = "room_edge_" + idSuffix;
			const std::string labelId = "room_label_" + idSuffix;

			// Fill: rooms can be concave (L-shaped), so a vertex-0 fan is wrong. Run
			// the screen-space ring through the renderer tessellator, which handles
			// arbitrary simple polygons (and self-corrects winding). The selected room
			// gets a brighter tint at a higher z so it reads as picked.
			renderer::TessellatedMesh mesh;
			if (tessellator.Tessellate(renderer::VectorPath{screen, true}, mesh) && mesh.indices.size() >= 3) {
				Renderer::Primitives::drawTriangles(
					Renderer::Primitives::TrianglesArgs{
						.vertices = mesh.vertices.data(),
						.indices = mesh.indices.data(),
						.vertexCount = mesh.vertices.size(),
						.indexCount = mesh.indices.size(),
						.color = selected ? kSelectedTint : kTint,
						.id = fillId.c_str(),
						.zIndex = selected ? kZSelected : kZFill,
					}
				);
			}

			// Outline: per-edge draw, concavity-independent. Selected room uses the
			// gold ring at z 100, mirroring the SelectionSystem indicators.
			const Foundation::Color edgeColor = selected ? kSelectedOutline : kOutline;
			const float				edgeWidth = selected ? 3.0F : 2.0F;
			const int				edgeZ = selected ? kZSelected : kZOutline;
			for (std::size_t i = 0; i < n; ++i) {
				Renderer::Primitives::drawLine(
					Renderer::Primitives::LineArgs{
						.start = screen[i],
						.end = screen[(i + 1) % n],
						.style =
							Foundation::LineStyle{
								.color = edgeColor,
								.width = edgeWidth,
							},
						.id = edgeId.c_str(),
						.zIndex = edgeZ,
					}
				);
			}

			// Name label anchored at the room's representative interior point.
			if (!room.name.empty()) {
				const auto repWorld = geometry::dequantize(room.rep);
				const auto repScreen = camera->worldToScreen(repWorld.x, repWorld.y, viewportW, viewportH, kPixelsPerMeter);
				Renderer::Primitives::drawText(
					Renderer::Primitives::TextArgs{
						.text = room.name,
						.position = Foundation::Vec2{repScreen.x, repScreen.y},
						.color = kLabel,
						.id = labelId.c_str(),
						.zIndex = static_cast<float>(kZLabel),
					}
				);
			}
		}
	}

} // namespace world_sim

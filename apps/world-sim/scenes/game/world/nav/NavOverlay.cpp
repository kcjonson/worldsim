#include "NavOverlay.h"

#include <ecs/World.h>
#include <ecs/components/NavPath.h>

#include <primitives/Primitives.h>

#include <array>
#include <string>

namespace world_sim {

	namespace {
		// Mesh wireframe color-coded by walkability: green = walkable ground, red = blocked
		// (water / tree / wall). A glance confirms whether the navmesh classifies land vs water
		// correctly. Bright magenta routes read over both.
		constexpr Foundation::Color kWalkableEdge{1.0F, 0.95F, 0.15F, 0.75F}; // bright yellow: reads on grass AND water
		constexpr Foundation::Color kBlockedEdge{0.95F, 0.2F, 0.2F, 0.6F};	  // red
		constexpr Foundation::Color kPathLine{1.0F, 0.2F, 0.8F, 0.95F};
		constexpr Foundation::Color kWaypointMarker{1.0F, 0.9F, 0.2F, 1.0F};

		constexpr int kZMeshEdge = 66; // above wall bands (~60-64)
		constexpr int kZPathLine = 70;
		constexpr int kZMarker = 71;

		// Mesh vertices are region-local integer millimeters; render space is meters.
		constexpr float kMmToMeters = 0.001F;
	} // namespace

	NavOverlay::NavOverlay(const Args& args)
		: ecsWorld(args.world),
		  camera(args.camera),
		  navigation(args.navigation) {}

	void NavOverlay::render(int viewportW, int viewportH) {
		if (!active || camera == nullptr || navigation == nullptr || !navigation->hasMesh()) {
			return;
		}

		// Mesh wireframe: draw each triangle's three edges, for every built region. Edges
		// are shared between neighbors, so this overdraws once per interior edge; fine for a
		// debug layer. Ids are prefixed with the region index so two regions' triangles
		// don't collide on the same primitive id.
		const std::vector<ecs::NavigationSystem::RegionView> built = navigation->builtRegions();
		for (std::size_t ri = 0; ri < built.size(); ++ri) {
			const geometry::nav::NavMesh& mesh = *built[ri].mesh;
			for (std::size_t t = 0; t < mesh.triangles.size(); ++t) {
				const auto& tri = mesh.triangles[t];
				const Foundation::Color edgeColor =
					geometry::nav::terrainTraversable(tri) ? kWalkableEdge : kBlockedEdge;

				std::array<Foundation::Vec2, 3> screen;
				for (int i = 0; i < 3; ++i) {
					const auto& v = mesh.vertices[tri.v[i]];
					const float wx = static_cast<float>(v.x) * kMmToMeters;
					const float wy = static_cast<float>(v.y) * kMmToMeters;
					const auto	s = camera->worldToScreen(wx, wy, viewportW, viewportH, kPixelsPerMeter);
					screen[i] = Foundation::Vec2{s.x, s.y};
				}

				const std::string idBase = "nav_tri_" + std::to_string(ri) + "_" + std::to_string(t);
				for (int i = 0; i < 3; ++i) {
					const std::string edgeId = idBase + "_" + std::to_string(i);
					Renderer::Primitives::drawLine(
						Renderer::Primitives::LineArgs{
							.start = screen[i],
							.end = screen[(i + 1) % 3],
							.style =
								Foundation::LineStyle{
									.color = edgeColor,
									.width = 1.0F,
								},
							.id = edgeId.c_str(),
							.zIndex = kZMeshEdge,
						}
					);
				}
			}
		}

		// Per-agent routes: the remaining waypoint polyline plus a marker at the
		// waypoint the colonist is currently steering toward.
		for (auto [entity, navPath] : ecsWorld->view<ecs::NavPath>()) {
			if (!navPath.valid || navPath.waypoints.size() < 2) {
				continue;
			}

			std::vector<Foundation::Vec2> screen;
			screen.reserve(navPath.waypoints.size());
			for (const auto& w : navPath.waypoints) {
				const auto s = camera->worldToScreen(w.x, w.y, viewportW, viewportH, kPixelsPerMeter);
				screen.push_back(Foundation::Vec2{s.x, s.y});
			}

			const std::string idBase = "nav_path_" + std::to_string(static_cast<unsigned long long>(entity));
			// Draw only the route the colonist still has to walk: start at the waypoint
			// it is steering toward (navPath.current), not the already-traversed ones. If
			// current has advanced to/past the end, nothing is drawn.
			for (std::size_t i = navPath.current; i + 1 < screen.size(); ++i) {
				const std::string segId = idBase + "_" + std::to_string(i);
				Renderer::Primitives::drawLine(
					Renderer::Primitives::LineArgs{
						.start = screen[i],
						.end = screen[i + 1],
						.style =
							Foundation::LineStyle{
								.color = kPathLine,
								.width = 2.0F,
							},
						.id = segId.c_str(),
						.zIndex = kZPathLine,
					}
				);
			}

			// Marker at the current target waypoint.
			if (navPath.current < screen.size()) {
				const std::string markerId = idBase + "_cur";
				Renderer::Primitives::drawCircle(
					Renderer::Primitives::CircleArgs{
						.center = screen[navPath.current],
						.radius = 4.0F,
						.style = Foundation::CircleStyle{.fill = kWaypointMarker},
						.id = markerId.c_str(),
						.zIndex = kZMarker,
					}
				);
			}
		}
	}

} // namespace world_sim

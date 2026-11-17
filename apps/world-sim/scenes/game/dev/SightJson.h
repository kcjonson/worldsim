#pragma once

// The JSON of the sight views (sight-softening.md 8). `/api/state?what=sightBodies` lists each
// colonist's sight bodies as vision handed them out at its last pass, with kind, diameter in
// millimetres, centre in metres, and a placed body's def name, so a scenario can find a BigRock.
// `/api/state?what=sightTunables` and `/api/dev/sight` give the sight tunables with each one's range,
// for the Dev Tools tab's sliders. Measures print to three decimals.

#include <vision/SightBody.h>
#include <vision/SightTunables.h>

#include <core/Vec2i64.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace world_sim {

	/// One colonist's bodies, as VisionSystem::sightBodies gives them.
	struct ColonistSightBodies {
		std::uint64_t						   id = 0;
		std::string							   name;
		geometry::Vec2i64					   eyeCentre{0, 0};
		std::int64_t						   leadMm  = 0;
		std::int64_t						   reachMm = 0;
		const std::vector<vision::SightBody>*  bodies	   = nullptr;
		const std::vector<vision::ShadowEdge>* shadowEdges = nullptr;
	};

	std::string sightBodiesJson(std::span<const ColonistSightBodies> colonists);

	/// Every tunable's value, its fixed bounds, and its start value.
	std::string sightTunablesJson(const vision::SightTunables& tunables);

	/// `tunables` with the field named `name` set to `value`, unclamped; false when no field has that name.
	bool setSightTunable(vision::SightTunables& tunables, const std::string& name, std::int64_t value);

} // namespace world_sim

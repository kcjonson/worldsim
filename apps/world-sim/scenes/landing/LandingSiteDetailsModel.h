#pragma once

// LandingSiteDetailsModel - ViewModel for the landing-site details pane.
//
// Computes the human-readable content for a selected tile: location, water
// signal (the survival-critical bit), terrain, climate, and a habitability
// rating. The View (LandingSiteDetailsPanel) only lays out and draws what this
// produces, so the wording and the difficulty heuristic live in one place.

#include <worldgen/sampling/LandingSite.h>

#include <graphics/Color.h>

#include <string>
#include <vector>

namespace worldgen {
struct GeneratedWorld;
}

namespace world_sim {

// One label:value line in the pane. The accent colors the value text so the
// water verdict and difficulty read at a glance.
struct DetailRow {
	std::string			label;
	std::string			value;
	Foundation::Color	accent;
};

// A titled group of rows (Water / Terrain / Climate).
struct DetailSection {
	std::string				header;
	std::vector<DetailRow>	rows;
};

struct LandingSiteDetails {
	std::string					location;   // "12.3 N, 45.6 W"
	std::string					verdict;    // one-line water verdict (the headline)
	Foundation::Color			verdictColor;
	worldgen::Habitability		habitability{worldgen::Habitability::Moderate};
	std::string					habitabilityText;
	Foundation::Color			habitabilityColor;
	std::vector<DetailSection>	sections;
};

// Build the pane content for the tile under (latDeg, lonDeg). The world must
// have a grid; fields are read defensively so a partially-generated world still
// produces a sensible (if sparser) pane.
LandingSiteDetails buildLandingSiteDetails(
	const worldgen::GeneratedWorld& world, double latDeg, double lonDeg);

} // namespace world_sim

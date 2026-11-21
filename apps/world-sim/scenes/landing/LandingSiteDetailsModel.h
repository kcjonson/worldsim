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

// A titled group of rows (Water / Provisions / Terrain / Climate).
struct DetailSection {
	std::string				header;
	std::vector<DetailRow>	rows;
};

struct LandingSiteDetails {
	std::string					location;   // "12.3 N, 45.6 W"
	std::string					coords;     // "14.7 N  79.2 W" (site-analysis readout)
	std::string					biomeName;  // display-spaced biome ("Temperate Deciduous Forest")
	std::string					tempRange;  // "9 to 22 C" (mean +/- seasonal half-swing)
	std::string					rainfall;   // "620 mm/yr"
	int							difficulty{0}; // 1..5 skulls, from habitability
	bool						recommended{false};
	std::string					verdict;    // one-line water verdict (the headline)
	Foundation::Color			verdictColor;
	worldgen::Habitability		habitability{worldgen::Habitability::Moderate};
	std::string					habitabilityText;
	Foundation::Color			habitabilityColor;
	std::vector<DetailSection>	sections;
};

// The pane's coordinate readout for a pick: "14.7 N  79.2 W".
std::string landingCoordinates(double pickLatDeg, double pickLonDeg);

// Build the pane content for a resolved landing. The coordinates are the pick's (the player never
// sees the resolution); everything else describes the landing tile, which can be a neighbour of the
// picked one. Fields are read defensively so a partially-generated world still produces a sensible
// (if sparser) pane.
LandingSiteDetails buildLandingSiteDetails(
	const worldgen::GeneratedWorld& world, const worldgen::LandingSite& site, double pickLatDeg, double pickLonDeg);

} // namespace world_sim

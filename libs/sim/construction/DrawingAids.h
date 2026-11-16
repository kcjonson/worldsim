#pragma once

// DrawingAids
//
// The player's drawing helpers as the snap engine reads them (building-construction
// D12, D14): which helpers are on, and the rectangle tool's grid spacing. Plain data;
// the app fills it from the player's Drawing Settings and tests fill their own, so the
// engine never reads a setting itself. Display-only settings (guide distances, the
// angle readout, the key guide) stay with the app.

namespace construction {

	struct DrawingAids {
		bool  angleSteps = false;
		bool  guidesFromShape = false;
		bool  alignToExistingBuildings = false;
		bool  guidesFromWorldAxes = false;
		bool  guideCrossings = false;
		bool  commonLengths = false;
		bool  roundLengths = false;
		float rectangleGridSpacingMeters = 1.0F;

		bool operator==(const DrawingAids&) const = default;
	};

} // namespace construction

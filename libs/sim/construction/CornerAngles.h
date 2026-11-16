#pragma once

// CornerAngles
//
// The angle readout's data (building-construction D12): the interior angle at the
// corner being formed at the previous point, and the closing corner at the first
// point when the shape closes or the cursor is within the closing reach. Display only;
// it returns numbers and flags, no text and no drawing.

#include <math/Types.h>

#include <array>
#include <cstddef>
#include <span>

namespace construction {

	enum class AngleReadoutTool {
		Foundation, // reads as a polygon once the path has three vertices
		Wall,		// an open chain reads the angle between its two segments
	};

	enum class CornerRole {
		Previous, // the corner at the last placed point
		Closing,  // the corner at the first point
	};

	struct CornerAngle {
		CornerRole		   role = CornerRole::Previous;
		std::size_t		   vertexIndex = 0; // into the path: the placed points, then the cursor
		::Foundation::Vec2 vertex{0.0F, 0.0F};
		float			   degrees = 0.0F;
		bool			   square = false;
		float			   startRadians = 0.0F;			// direction from the vertex to the previous path point
		float			   sweepRadians = 0.0F;			// signed sweep from that direction to the next path point
		::Foundation::Vec2 toPrevious{0.0F, 0.0F};		// unit vectors from the vertex
		::Foundation::Vec2 toNext{0.0F, 0.0F};
	};

	struct CornerAngleInput {
		std::span<const ::Foundation::Vec2>	   points;		   // placed so far
		::Foundation::Vec2					   cursor{0.0F, 0.0F};
		bool								   closes = false; // the click closes the shape, so the cursor is not in the path
		AngleReadoutTool					   tool = AngleReadoutTool::Foundation;
		float								   closingReachMeters = 0.0F;
		float								   squareToleranceDegrees = 0.05F;
	};

	struct CornerAngles {
		std::array<CornerAngle, 2> corners{};
		std::size_t				   count = 0;
	};

	/// Nothing with fewer than two placed points. A polygon corner is the interior angle;
	/// an open wall chain's is the unsigned angle between its segments. A corner with an
	/// edge under 1 mm is skipped.
	[[nodiscard]] CornerAngles cornerAngles(const CornerAngleInput& input);

} // namespace construction

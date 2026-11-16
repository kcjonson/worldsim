#include "world/chunk/NoiseLattice.h"

#include <core/IntegerDivision.h>

namespace world {

	NoiseOctaveGrid::NoiseOctaveGrid(const Args& args) {
		const int64_t firstX = geometry::floorDiv(args.originX, args.wavelengthM);
		const int64_t firstY = geometry::floorDiv(args.originY, args.wavelengthM);
		columns = fillAxis(args.originX, firstX, args);
		rows = fillAxis(args.originY, firstY, args);
		cornersWide = columns.back().cell + 2;
		const int32_t cornersHigh = rows.back().cell + 2;
		gradients.resize(static_cast<size_t>(cornersWide) * static_cast<size_t>(cornersHigh) * 2);
		for (int32_t cy = 0; cy < cornersHigh; ++cy) {
			for (int32_t cx = 0; cx < cornersWide; ++cx) {
				float		   gz = 0.0F;
				const size_t   k = (static_cast<size_t>(cy) * static_cast<size_t>(cornersWide) + static_cast<size_t>(cx)) * 2;
				const uint32_t h = foundation::hash3(static_cast<int32_t>(firstX + cx), static_cast<int32_t>(firstY + cy), 0, args.seed);
				foundation::detail::gradient(h, gradients[k], gradients[k + 1], gz);
			}
		}
	}

	std::vector<NoiseOctaveGrid::Axis> NoiseOctaveGrid::fillAxis(int64_t origin, int64_t firstCell, const Args& args) {
		std::vector<Axis> axis(static_cast<size_t>(args.side));
		const float		  inverse = 1.0F / static_cast<float>(args.wavelengthM);
		for (int32_t t = 0; t < args.side; ++t) {
			const int64_t meters = origin + static_cast<int64_t>(t) * args.stepM;
			const int64_t cell = geometry::floorDiv(meters, args.wavelengthM);
			const float	  f = (static_cast<float>(meters - cell * args.wavelengthM) + args.phaseM) * inverse;
			axis[static_cast<size_t>(t)] = {
				.cell = static_cast<int32_t>(cell - firstCell),
				.f = f,
				.u = foundation::detail::quintic(f),
			};
		}
		return axis;
	}

} // namespace world

#pragma once

// One octave of salal's gradient noise (HashNoise's gradientNoise2 field) sampled on a square grid of
// whole world meters, with each lattice corner's gradient hashed once for the grid instead of four
// times a sample. A sample depends only on its world position, the phase, the wavelength and the seed,
// never on the grid's origin, so two grids that share a point give it the same value bit for bit.
// Positions are whole meters plus the phase, kept apart, so precision doesn't depend on the distance
// from the origin.

#include <random/HashNoise.h>

#include <cstdint>
#include <vector>

namespace world {

	class NoiseOctaveGrid {
	  public:
		struct Args {
			int64_t	 originX = 0; ///< world meters of sample (0, 0)
			int64_t	 originY = 0;
			int32_t	 side = 0;		  ///< samples a row, and rows
			int32_t	 stepM = 1;		  ///< meters between samples
			float	 phaseM = 0.5F;	  ///< added to every position: gradient noise is zero on its lattice, which sits on whole meters
			int32_t	 wavelengthM = 0; ///< the lattice spacing
			uint32_t seed = 0;
		};

		explicit NoiseOctaveGrid(const Args& args);

		/// The octave at sample (i, j), about -1 to 1.
		[[nodiscard]] float at(int32_t i, int32_t j) const {
			const Axis&	 c = columns[static_cast<size_t>(i)];
			const Axis&	 r = rows[static_cast<size_t>(j)];
			const float* g00 =
				&gradients[(static_cast<size_t>(r.cell) * static_cast<size_t>(cornersWide) + static_cast<size_t>(c.cell)) * 2];
			const float* g01 = g00 + static_cast<size_t>(cornersWide) * 2;
			const float	 d00 = g00[0] * c.f + g00[1] * r.f;
			const float	 d10 = g00[2] * (c.f - 1.0F) + g00[3] * r.f;
			const float	 d01 = g01[0] * c.f + g01[1] * (r.f - 1.0F);
			const float	 d11 = g01[2] * (c.f - 1.0F) + g01[3] * (r.f - 1.0F);
			const float	 x0 = d00 + c.u * (d10 - d00);
			const float	 x1 = d01 + c.u * (d11 - d01);
			return x0 + r.u * (x1 - x0);
		}

	  private:
		struct Axis {
			int32_t cell = 0; ///< lattice cell, counted from the grid's first
			float	f = 0.0F; ///< offset inside it, 0..1
			float	u = 0.0F; ///< its quintic fade
		};

		static std::vector<Axis> fillAxis(int64_t origin, int64_t firstCell, const Args& args);

		std::vector<Axis>  columns;
		std::vector<Axis>  rows;
		int32_t			   cornersWide = 0;
		std::vector<float> gradients; ///< x, y per corner, rows of cornersWide
	};

} // namespace world

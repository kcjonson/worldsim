#include "WarpField.h"

#include "ContourDetail.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <vector>

namespace geometry {

	namespace {

		using contour_detail::floorDiv;

		// `coarse` addressed by global lattice index: index k sits at world
		// phase + k * cellMm, where phase = originMm mod cellMm. Every field on the
		// same lattice agrees on these indices regardless of its origin.
		class GlobalLattice {
		  public:
			GlobalLattice(const ScalarField& newField, float newOutside)
				: field(newField),
				  outside(newOutside),
				  phaseX(newField.originMm.x - floorDiv(newField.originMm.x, newField.cellMm) * newField.cellMm),
				  phaseY(newField.originMm.y - floorDiv(newField.originMm.y, newField.cellMm) * newField.cellMm),
				  startX(floorDiv(newField.originMm.x, newField.cellMm)),
				  startY(floorDiv(newField.originMm.y, newField.cellMm)) {}

			float sample(std::int64_t gx, std::int64_t gy) const {
				const std::int64_t lx = gx - startX;
				const std::int64_t ly = gy - startY;
				if (lx < 0 || ly < 0 || lx >= field.width || ly >= field.height) {
					return outside;
				}
				return field.at(static_cast<int>(lx), static_cast<int>(ly));
			}

			// Global index of the cell containing world position w (exact).
			std::int64_t cellX(std::int64_t wx) const { return floorDiv(wx - phaseX, field.cellMm); }
			std::int64_t cellY(std::int64_t wy) const { return floorDiv(wy - phaseY, field.cellMm); }

			// One axis of a bilinear read: the global cell and the fraction across it.
			struct AxisRead {
				std::int64_t cell = 0;
				double		 frac = 0.0;
			};

			// Along x (or y), the read at world coordinate w displaced by offset.
			AxisRead readX(std::int64_t wx, double offset) const { return axisRead(wx - phaseX, offset); }
			AxisRead readY(std::int64_t wy, double offset) const { return axisRead(wy - phaseY, offset); }

			// Bilinear read at world w + offset. The lerps stay within the range of
			// their four samples in floating point, which the skip relies on.
			float bilinear(Vec2i64 w, WarpOffsetMm offset) const {
				return bilinear(readX(w.x, offset.x), readY(w.y, offset.y));
			}

			float bilinear(AxisRead x, AxisRead y) const {
				const double v00	= static_cast<double>(sample(x.cell, y.cell));
				const double v10	= static_cast<double>(sample(x.cell + 1, y.cell));
				const double v01	= static_cast<double>(sample(x.cell, y.cell + 1));
				const double v11	= static_cast<double>(sample(x.cell + 1, y.cell + 1));
				const double bottom = v00 + (v10 - v00) * x.frac;
				const double top	= v01 + (v11 - v01) * x.frac;
				return static_cast<float>(bottom + (top - bottom) * y.frac);
			}

		  private:
			AxisRead axisRead(std::int64_t fromPhaseMm, double offset) const {
				const double u = (static_cast<double>(fromPhaseMm) + offset) / static_cast<double>(field.cellMm);
				const double k = std::floor(u);
				return {static_cast<std::int64_t>(k), u - k};
			}

			const ScalarField& field;
			float			   outside;
			std::int64_t	   phaseX;
			std::int64_t	   phaseY;
			std::int64_t	   startX;
			std::int64_t	   startY;
		};

		// Per global coarse cell in [cellMin, cellMax], whether every coarse sample
		// within `reach` cells of it (indices k - reach .. k + 1 + reach) lies on one
		// side of iso. Separable dilated min/max.
		class UniformCells {
		  public:
			UniformCells(const GlobalLattice& lattice, Vec2i64 cellMin, Vec2i64 cellMax, std::int64_t reach, float iso)
				: minX(cellMin.x),
				  minY(cellMin.y),
				  countX(static_cast<std::size_t>(cellMax.x - cellMin.x + 1)),
				  countY(static_cast<std::size_t>(cellMax.y - cellMin.y + 1)),
				  uniform(countX * countY, 0) {
				const std::int64_t window = 2 * reach + 2;
				const std::int64_t rowLo  = cellMin.y - reach;
				const std::size_t  rows	  = countY + static_cast<std::size_t>(window - 1);

				// Row pass: for each sample row, min/max over the x window of each cell.
				std::vector<float> rowMin(rows * countX);
				std::vector<float> rowMax(rows * countX);
				for (std::size_t r = 0; r < rows; ++r) {
					const std::int64_t gy = rowLo + static_cast<std::int64_t>(r);
					for (std::size_t c = 0; c < countX; ++c) {
						const std::int64_t gx0 = cellMin.x + static_cast<std::int64_t>(c) - reach;
						float			   lo  = lattice.sample(gx0, gy);
						float			   hi  = lo;
						for (std::int64_t i = 1; i < window; ++i) {
							const float v = lattice.sample(gx0 + i, gy);
							lo			  = std::min(lo, v);
							hi			  = std::max(hi, v);
						}
						rowMin[r * countX + c] = lo;
						rowMax[r * countX + c] = hi;
					}
				}

				// Column pass over the row results.
				for (std::size_t cy = 0; cy < countY; ++cy) {
					for (std::size_t c = 0; c < countX; ++c) {
						float lo = rowMin[cy * countX + c];
						float hi = rowMax[cy * countX + c];
						for (std::size_t j = 1; j < static_cast<std::size_t>(window); ++j) {
							lo = std::min(lo, rowMin[(cy + j) * countX + c]);
							hi = std::max(hi, rowMax[(cy + j) * countX + c]);
						}
						uniform[cy * countX + c] = (lo >= iso || hi < iso) ? 1 : 0;
					}
				}
			}

			bool isUniform(std::int64_t gx, std::int64_t gy) const {
				return uniform[static_cast<std::size_t>(gy - minY) * countX + static_cast<std::size_t>(gx - minX)] != 0;
			}

		  private:
			std::int64_t			  minX;
			std::int64_t			  minY;
			std::size_t				  countX;
			std::size_t				  countY;
			// Bytes, not vector<bool>: every vector<bool> element access builds an
			// iterator, which in MSVC debug builds takes a process-wide lock, and
			// concurrent chunk workers read this millions of times each.
			std::vector<std::uint8_t> uniform;
		};

	} // namespace

	ScalarField warpField(
		const ScalarField&		coarse,
		Vec2i64					fineOriginMm,
		std::int64_t			fineCellMm,
		int						fineWidth,
		int						fineHeight,
		const WarpFunction&		offsetMm,
		float					outsideValue,
		std::optional<WarpSkip> skip
	) {
		ScalarField fine(fineOriginMm, fineCellMm, fineWidth, fineHeight);
		if (fineWidth <= 0 || fineHeight <= 0) {
			return fine;
		}
		const GlobalLattice lattice(coarse, outsideValue);

		std::optional<UniformCells> uniformCells;
		if (skip) {
			assert(skip->maxOffsetMm >= 0);
			// A fine neighbor of a skipped sample reads within maxOffsetMm +
			// fineCellMm of it, and a bilinear read touches one more coarse sample
			// beyond its cell; the extra cell absorbs floating-point rounding at
			// exact cell boundaries.
			const std::int64_t reach = (skip->maxOffsetMm + fineCellMm + coarse.cellMm - 1) / coarse.cellMm + 1;
			const Vec2i64	   fineMax{
				  fineOriginMm.x + static_cast<std::int64_t>(fineWidth - 1) * fineCellMm,
				  fineOriginMm.y + static_cast<std::int64_t>(fineHeight - 1) * fineCellMm
			  };
			uniformCells.emplace(
				lattice,
				Vec2i64{lattice.cellX(fineOriginMm.x), lattice.cellY(fineOriginMm.y)},
				Vec2i64{lattice.cellX(fineMax.x), lattice.cellY(fineMax.y)},
				reach,
				skip->iso
			);
		}

		// Per fine column and per fine row: the skip test's cell and the unwarped
		// read, each a function of that one coordinate, so the skipped samples (all
		// but a band around the isoline) cost a lookup and a lerp.
		using AxisRead = GlobalLattice::AxisRead;
		std::vector<std::int64_t> colCell(static_cast<std::size_t>(fineWidth));
		std::vector<std::int64_t> rowCell(static_cast<std::size_t>(fineHeight));
		std::vector<AxisRead>	  colRead(static_cast<std::size_t>(fineWidth));
		std::vector<AxisRead>	  rowRead(static_cast<std::size_t>(fineHeight));
		for (int i = 0; i < fineWidth; ++i) {
			const std::int64_t wx				  = fineOriginMm.x + static_cast<std::int64_t>(i) * fineCellMm;
			colCell[static_cast<std::size_t>(i)] = lattice.cellX(wx);
			colRead[static_cast<std::size_t>(i)] = lattice.readX(wx, 0.0);
		}
		for (int j = 0; j < fineHeight; ++j) {
			const std::int64_t wy				  = fineOriginMm.y + static_cast<std::int64_t>(j) * fineCellMm;
			rowCell[static_cast<std::size_t>(j)] = lattice.cellY(wy);
			rowRead[static_cast<std::size_t>(j)] = lattice.readY(wy, 0.0);
		}

		for (int j = 0; j < fineHeight; ++j) {
			const auto row = static_cast<std::size_t>(j);
			for (int i = 0; i < fineWidth; ++i) {
				const auto col = static_cast<std::size_t>(i);
				if (uniformCells && uniformCells->isUniform(colCell[col], rowCell[row])) {
					fine.at(i, j) = lattice.bilinear(colRead[col], rowRead[row]);
					continue;
				}
				const Vec2i64 w = fine.samplePositionMm(i, j);
				const WarpOffsetMm offset = offsetMm(w);
				assert(
					!skip || (std::abs(offset.x) <= static_cast<double>(skip->maxOffsetMm) &&
							  std::abs(offset.y) <= static_cast<double>(skip->maxOffsetMm))
				);
				fine.at(i, j) = lattice.bilinear(w, offset);
			}
		}
		return fine;
	}

} // namespace geometry

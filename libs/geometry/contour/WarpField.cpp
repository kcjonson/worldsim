#include "WarpField.h"

#include "ContourDetail.h"
#include "../core/IntegerDivision.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace geometry {

	namespace {

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

			// Along x (or y), the read at world coordinate w displaced by offset.
			LatticeAxisRead readX(std::int64_t wx, double offset) const { return latticeAxisRead(wx - phaseX, offset, field.cellMm); }
			LatticeAxisRead readY(std::int64_t wy, double offset) const { return latticeAxisRead(wy - phaseY, offset, field.cellMm); }

			// Bilinear read at world w + offset.
			float bilinear(Vec2i64 w, WarpOffsetMm offset) const {
				return warpedBilinear(*this, {phaseX, phaseY}, field.cellMm, w, offset);
			}

			float bilinear(LatticeAxisRead x, LatticeAxisRead y) const { return latticeBilinear(*this, x, y); }

			float operator()(std::int64_t gx, std::int64_t gy) const { return sample(gx, gy); }

		  private:
			const ScalarField& field;
			float			   outside;
			std::int64_t	   phaseX;
			std::int64_t	   phaseY;
			std::int64_t	   startX;
			std::int64_t	   startY;
		};

		// Per global coarse cell in [cellMin, cellMax], whether every coarse sample
		// within `reach` cells of it (indices k - reach .. k + 1 + reach) lies on one
		// side of iso, counted over a summed-area table of which side each sample is
		// on, and its value where its own four samples are equal.
		class UniformCells {
		  public:
			UniformCells(const GlobalLattice& lattice, Vec2i64 cellMin, Vec2i64 cellMax, std::int64_t reach, float iso)
				: minX(cellMin.x),
				  minY(cellMin.y),
				  countX(static_cast<std::size_t>(cellMax.x - cellMin.x + 1)),
				  countY(static_cast<std::size_t>(cellMax.y - cellMin.y + 1)),
				  uniform(countX * countY, 0),
				  flat(countX * countY, std::numeric_limits<float>::quiet_NaN()) {
				const auto		  window = static_cast<std::size_t>(2 * reach + 2);
				const std::size_t sideX	 = countX + window - 1;
				const std::size_t sideY	 = countY + window - 1;
				const std::size_t stride = sideX + 1;
				// above[y * stride + x]: samples at or above iso in the first y rows and
				// x columns of the window area, which starts `reach` before cellMin.
				std::vector<std::uint32_t> above(stride * (sideY + 1), 0);
				for (std::size_t y = 0; y < sideY; ++y) {
					const std::int64_t gy	  = cellMin.y - reach + static_cast<std::int64_t>(y);
					std::uint32_t	   rowSum = 0;
					for (std::size_t x = 0; x < sideX; ++x) {
						rowSum += lattice.sample(cellMin.x - reach + static_cast<std::int64_t>(x), gy) >= iso ? 1U : 0U;
						above[(y + 1) * stride + x + 1] = above[y * stride + x + 1] + rowSum;
					}
				}

				const auto area = static_cast<std::uint32_t>(window * window);
				for (std::size_t cy = 0; cy < countY; ++cy) {
					const std::int64_t gy = cellMin.y + static_cast<std::int64_t>(cy);
					for (std::size_t c = 0; c < countX; ++c) {
						const std::uint32_t count = above[(cy + window) * stride + c + window] - above[cy * stride + c + window] -
													above[(cy + window) * stride + c] + above[cy * stride + c];
						uniform[cy * countX + c]  = (count == 0 || count == area) ? 1 : 0;

						const std::int64_t gx = cellMin.x + static_cast<std::int64_t>(c);
						const float		   v  = lattice.sample(gx, gy);
						if (lattice.sample(gx + 1, gy) == v && lattice.sample(gx, gy + 1) == v &&
							lattice.sample(gx + 1, gy + 1) == v) {
							flat[cy * countX + c] = v;
						}
					}
				}
			}

			bool isUniform(std::int64_t gx, std::int64_t gy) const { return uniform[index(gx, gy)] != 0; }

			// A bilinear read anywhere in a cell whose four samples are equal returns
			// exactly that value; NaN when they differ.
			float flatValue(std::int64_t gx, std::int64_t gy) const { return flat[index(gx, gy)]; }

		  private:
			std::size_t index(std::int64_t gx, std::int64_t gy) const {
				return static_cast<std::size_t>(gy - minY) * countX + static_cast<std::size_t>(gx - minX);
			}

			std::int64_t			  minX;
			std::int64_t			  minY;
			std::size_t				  countX;
			std::size_t				  countY;
			// Bytes, not vector<bool>: every vector<bool> element access builds an
			// iterator, which in MSVC debug builds takes a process-wide lock, and
			// concurrent chunk workers read this millions of times each.
			std::vector<std::uint8_t> uniform;
			std::vector<float>		  flat;
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
		using AxisRead = LatticeAxisRead;
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

		// Runs of fine columns in one coarse cell.
		std::vector<int> runEnds;
		for (int i = 1; i <= fineWidth; ++i) {
			if (i == fineWidth || colCell[static_cast<std::size_t>(i)] != colCell[static_cast<std::size_t>(i - 1)]) {
				runEnds.push_back(i);
			}
		}

		// A row's spans: its runs, each warped, read unwarped, or filled with its
		// cell's flat value, neighbors of one kind (and one flat value) merged. Every
		// fine row in a coarse row has the same ones.
		struct Span {
			int	  start;
			int	  end;
			bool  warp;
			float flat; // NaN: read unwarped
		};
		std::vector<Span> spans;
		std::int64_t	  spansCellRow = 0;
		for (int j = 0; j < fineHeight; ++j) {
			const auto		   row	   = static_cast<std::size_t>(j);
			const std::int64_t cellRow = rowCell[row];
			if (j == 0 || cellRow != spansCellRow) {
				spans.clear();
				int start = 0;
				for (const int end : runEnds) {
					const std::int64_t cell = colCell[static_cast<std::size_t>(start)];
					const bool		   warp = !uniformCells || !uniformCells->isUniform(cell, cellRow);
					const float		   flat = warp ? std::numeric_limits<float>::quiet_NaN() : uniformCells->flatValue(cell, cellRow);
					const bool		   same = !spans.empty() && spans.back().warp == warp && (warp || spans.back().flat == flat);
					if (same) {
						spans.back().end = end;
					} else {
						spans.push_back({start, end, warp, flat});
					}
					start = end;
				}
				spansCellRow = cellRow;
			}
			for (const Span& span : spans) {
				if (!span.warp && !std::isnan(span.flat)) {
					std::fill(&fine.at(span.start, j), &fine.at(span.start, j) + (span.end - span.start), span.flat);
					continue;
				}
				for (int i = span.start; i < span.end; ++i) {
					if (!span.warp) {
						fine.at(i, j) = lattice.bilinear(colRead[static_cast<std::size_t>(i)], rowRead[row]);
						continue;
					}
					const Vec2i64	   w	  = fine.samplePositionMm(i, j);
					const WarpOffsetMm offset = offsetMm(w);
					assert(
						!skip || (std::abs(offset.x) <= static_cast<double>(skip->maxOffsetMm) &&
								  std::abs(offset.y) <= static_cast<double>(skip->maxOffsetMm))
					);
					fine.at(i, j) = lattice.bilinear(w, offset);
				}
			}
		}
		return fine;
	}

} // namespace geometry

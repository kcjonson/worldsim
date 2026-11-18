// The seen area's feather (sight-softening.md 7.2) on the null backend: the record's cells, and the
// cut near a pending shape, whose pieces lie on the union's boundary alone, so no seam is drawn where
// the record's edge runs inside the pending shape.

#include "world/rendering/SeenFeather.h"

#include "BaseShader.h"
#include "backend/NullBackend.h"
#include "primitives/Primitives.h"

#include <vision/SeenArea.h>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <vector>

namespace {

	using geometry::Vec2i64;
	using worldview::ValuePolygon;

	constexpr std::int64_t kFeatherMm = 1500;

	double valueOrZero(const std::vector<ValuePolygon>& pieces, glm::dvec2 point) {
		double greatest = 0.0;
		for (const ValuePolygon& piece : pieces) {
			for (std::size_t corner = 1; corner + 1 < piece.points.size(); ++corner) {
				const glm::dvec2 a	  = piece.points[0];
				const glm::dvec2 b	  = piece.points[corner];
				const glm::dvec2 c	  = piece.points[corner + 1];
				const double	 area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
				if (area == 0.0) {
					continue;
				}
				const double first	= ((b.x - point.x) * (c.y - point.y) - (b.y - point.y) * (c.x - point.x)) / area;
				const double second = ((c.x - point.x) * (a.y - point.y) - (c.y - point.y) * (a.x - point.x)) / area;
				const double third	= 1.0 - first - second;
				if (first >= -1e-9 && second >= -1e-9 && third >= -1e-9) {
					greatest = std::max(greatest, first * piece.values[0] + second * piece.values[corner] + third * piece.values[corner + 1]);
				}
			}
		}
		return greatest;
	}

	std::vector<Vec2i64> square(double x, double y, double half) {
		const auto mm = [](double metres) { return std::llround(metres * 1000.0); };
		return {{mm(x - half), mm(y - half)}, {mm(x + half), mm(y - half)}, {mm(x + half), mm(y + half)}, {mm(x - half), mm(y + half)}};
	}

	class SeenFeatherTest : public ::testing::Test {
	  protected:
		void SetUp() override { Renderer::Primitives::init(std::make_unique<Renderer::backend::NullBackend>(), base_shader::source()); }
		void TearDown() override {
			feather.reset();
			Renderer::Primitives::shutdown();
		}

		void frame() {
			std::vector<vision::SeenArea::Box> changed;
			seen.takeChangedBoxes(changed);
			feather->update(seen, changed, {-60000, -60000}, {60000, 60000}, kFeatherMm);
		}

		std::vector<ValuePolygon> cellPieces() const {
			std::vector<ValuePolygon> pieces;
			for (const worldview::SeenFeather::Cell* cell : feather->cellsInView()) {
				pieces.insert(pieces.end(), cell->pieces.begin(), cell->pieces.end());
			}
			return pieces;
		}

		vision::SeenArea						 seen;
		std::unique_ptr<worldview::SeenFeather> feather = std::make_unique<worldview::SeenFeather>();
	};

} // namespace

// The record's cells feather its edge; a cell is rebuilt only when a ring registers near it.
TEST_F(SeenFeatherTest, TheRecordsCellsFeatherItsEdge) {
	seen.commitNow(square(0.0, 0.0, 10.0), {0, 0});
	frame();
	EXPECT_GT(feather->counts().cellsRebuilt, 0U);
	EXPECT_NEAR(valueOrZero(cellPieces(), {-9250.0, 0.0}), 0.5, 1e-6);
	EXPECT_NEAR(valueOrZero(cellPieces(), {3000.0, 9700.0}), 0.8, 1e-6);
	EXPECT_EQ(valueOrZero(cellPieces(), {0.0, 0.0}), 0.0);

	frame();
	EXPECT_EQ(feather->counts().cellsRebuilt, 0U) << "nothing changed";
	seen.commitNow(square(40.0, 40.0, 3.0), {40000, 40000});
	frame();
	EXPECT_GT(feather->counts().cellsRebuilt, 0U);
	EXPECT_LT(feather->counts().cellsRebuilt, feather->counts().cellsDrawn) << "only the cells near the new ring";
}

// A pending square overlapping the record: the record's edge inside it draws nothing, the pending
// square's sides inside the record draw nothing, and the rest fade from the union's boundary.
TEST_F(SeenFeatherTest, ThePendingCutDrawsTheUnionsBoundaryAlone) {
	seen.commitNow(square(0.0, 0.0, 10.0), {0, 0});
	seen.queue(square(12.0, 0.0, 5.0), {12000, 0});
	frame();
	EXPECT_EQ(feather->counts().pendingShapes, 1U);
	EXPECT_EQ(feather->counts().clustersCut, 1U);
	std::vector<ValuePolygon> pending;
	feather->appendPendingPieces(pending);
	EXPECT_EQ(valueOrZero(pending, {10000.0, 0.0}), 0.0) << "no seam on the record's edge inside the pending square";
	EXPECT_EQ(valueOrZero(pending, {7500.0, 0.0}), 0.0) << "no seam on the pending side inside the record";
	EXPECT_NEAR(valueOrZero(pending, {10000.0, 4250.0}), 0.5, 1e-6) << "the pending square's top side";
	EXPECT_NEAR(valueOrZero(pending, {16500.0, 0.0}), 1.0 - 500.0 / 1500.0, 1e-6) << "its far side";
	EXPECT_NEAR(valueOrZero(pending, {9500.0, 7000.0}), 1.0 - 500.0 / 1500.0, 1e-6) << "the record's edge outside it";
	EXPECT_NEAR(valueOrZero(pending, {9700.0, 5300.0}), 0.8, 1e-6) << "beside the concave corner";

	// The cells give way to the cut inside the pending square grown by the feather.
	ASSERT_EQ(feather->pendingGrown().size(), 1U) << "a convex square grows as one polygon";
	frame();
	EXPECT_EQ(feather->counts().clustersCut, 0U) << "kept while nothing changes";
}

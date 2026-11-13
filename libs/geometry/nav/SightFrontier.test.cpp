#include "nav/SightFrontier.h"

#include "nav/PatchableMesh.h"
#include "nav/SightCover.h"
#include "polygon/Polygon.h"
#include "predicates/Predicates.h"
#include "visibility/Buildings.test.h"
#include "visibility/SightPolygon.h"
#include "visibility/SightSkirt.h"
#include "visibility/Visibility.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

using geometry::Vec2i64;
using namespace geometry::nav;

namespace {

	constexpr std::int64_t kSightMm = 30000;

	void add(PatchableMesh& mesh, const std::vector<Vec2i64>& ring) {
		PatchEdits edits;
		edits.adds.push_back(NavInputPolygon{ring, false, -3});
		mesh.applyPatch(edits);
	}

	double distanceToOutline(const Vec2i64& point, const std::vector<Vec2i64>& shape) {
		double nearest = 1e300;
		for (std::size_t i = 0; i < shape.size(); ++i) {
			nearest = std::min(nearest, geometry::distanceToSegment(point, shape[i], shape[(i + 1) % shape.size()]));
		}
		return nearest;
	}

} // namespace

// The record's own path, polygon by polygon from observers round random buildings: the true shape
// is tested, an uncovered one grown by the skirt and cut into its frontier, and the pieces patched
// in. Every piece is a simple ring inside the skirted shape, and every point of the true shape
// deeper than the cover tolerance is then on the record.
TEST(SightFrontier, PiecesAndTheRecordHoldTheShape) {
	std::mt19937					  rng(3301);
	SightCoverScratch				  coverScratch;
	SightFrontierScratch			  frontierScratch;
	std::vector<std::vector<Vec2i64>> pieces;
	int								  cut	  = 0;
	int								  samples = 0;
	for (int scene = 0; scene < 2; ++scene) {
		const geometry::buildings::Building building  = geometry::buildings::randomBuilding(rng);
		const std::vector<Vec2i64>			observers = geometry::buildings::observersFor(building, rng, 14);
		PatchableMesh						record;
		add(record, geometry::computeVisibilityPolygon(observers.front(), kSightMm, building.walls));
		for (const Vec2i64& observer : observers) {
			const std::vector<Vec2i64> shape = geometry::computeVisibilityPolygon(observer, kSightMm, building.walls);
			if (!geometry::simpleAround(shape, observer) || sightShapeCovered(record, shape, observer, coverScratch)) {
				continue;
			}
			const std::vector<Vec2i64> skirted = geometry::skirtedSightShape(shape, observer, kSightMm, geometry::kSeenSkirtMm);
			if (skirted.empty()) {
				continue;
			}
			if (sightShapeFrontier(record, skirted, observer, frontierScratch, pieces) == SightFrontier::Pieces) {
				++cut;
			}
			for (const std::vector<Vec2i64>& piece : pieces) {
				EXPECT_TRUE(geometry::isSimple(piece).pass) << "a piece from (" << observer.x << ", " << observer.y << ") is not simple";
				for (const Vec2i64& vertex : piece) {
					EXPECT_TRUE(geometry::pointInPolygon(vertex, skirted) != geometry::PointInPolygon::Outside || distanceToOutline(vertex, skirted) <= 1.0);
				}
				add(record, piece);
			}
			Vec2i64 low	 = shape.front();
			Vec2i64 high = low;
			for (const Vec2i64& vertex : shape) {
				low	 = {std::min(low.x, vertex.x), std::min(low.y, vertex.y)};
				high = {std::max(high.x, vertex.x), std::max(high.y, vertex.y)};
			}
			for (int attempt = 0; attempt < 150; ++attempt) {
				const std::int64_t x = low.x + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(high.x - low.x + 1));
				const std::int64_t y = low.y + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(high.y - low.y + 1));
				const Vec2i64	   point{x, y};
				if (geometry::pointInPolygon(point, shape) != geometry::PointInPolygon::Inside ||
					distanceToOutline(point, shape) <= static_cast<double>(kSeenCoverToleranceMm) + 1.0) {
					continue;
				}
				++samples;
				EXPECT_GE(record.locate(point), 0) << "(" << point.x << ", " << point.y << ") of the shape from (" << observer.x << ", " << observer.y
												   << ") is off the record";
			}
		}
		EXPECT_EQ(record.validate(), "");
	}
	std::printf("[sight frontier] %d shapes cut into pieces; %d deep samples\n", cut, samples);
	EXPECT_GT(cut, 3);
	EXPECT_GT(samples, 500);
}

TEST(SightFrontier, CentreOffTheRecordGivesTheWholeShape) {
	PatchableMesh						record;
	SightFrontierScratch				scratch;
	std::vector<std::vector<Vec2i64>>	pieces;
	const std::vector<Vec2i64>			shape = geometry::sightPolygon({500000, 0}, kSightMm);
	EXPECT_EQ(sightShapeFrontier(record, shape, {500000, 0}, scratch, pieces), SightFrontier::Whole);
	ASSERT_EQ(pieces.size(), 1U);
	EXPECT_EQ(pieces.front(), shape);
	add(record, geometry::sightPolygon({0, 0}, kSightMm));
	EXPECT_EQ(sightShapeFrontier(record, shape, {500000, 0}, scratch, pieces), SightFrontier::Whole);
}

TEST(SightFrontier, HeldShapeIsCoveredAndAPartlyHeldOneGivesPieces) {
	PatchableMesh					  record;
	SightFrontierScratch			  scratch;
	std::vector<std::vector<Vec2i64>> pieces;
	add(record, geometry::sightPolygon({0, 0}, kSightMm));
	EXPECT_EQ(sightShapeFrontier(record, geometry::sightPolygon({1000, 0}, 20000), {1000, 0}, scratch, pieces), SightFrontier::Covered);
	EXPECT_TRUE(pieces.empty());
	const std::vector<Vec2i64> beyond = geometry::sightPolygon({20000, 0}, kSightMm);
	ASSERT_EQ(sightShapeFrontier(record, beyond, {20000, 0}, scratch, pieces), SightFrontier::Pieces);
	for (const std::vector<Vec2i64>& piece : pieces) {
		EXPECT_TRUE(geometry::isSimple(piece).pass);
		add(record, piece);
	}
	EXPECT_TRUE(record.groundCovers(geometry::sightPolygon({20000, 0}, kSightMm - 10)));
}

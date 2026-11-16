#include "nav/SightCover.h"

#include "nav/PatchableMesh.h"
#include "predicates/Predicates.h"
#include "visibility/Buildings.test.h"
#include "visibility/SightPolygon.h"
#include "visibility/Visibility.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

using geometry::Vec2i64;
using namespace geometry::nav;

namespace {

	constexpr std::int64_t kSeenProvenance = -3;

	void add(PatchableMesh& mesh, const std::vector<Vec2i64>& shape) {
		PatchEdits edits;
		edits.adds.push_back(NavInputPolygon{shape, false, kSeenProvenance});
		mesh.applyPatch(edits);
	}

	// Every live triangle edge with nothing across it, the slow way.
	std::vector<std::pair<Vec2i64, Vec2i64>> walkMeshEdges(const PatchableMesh& mesh) {
		std::vector<std::pair<Vec2i64, Vec2i64>> out;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (!mesh.triangleLive(t)) {
				continue;
			}
			const NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
			for (std::size_t e = 0; e < 3; ++e) {
				if (tri.neighbor[e] < 0) {
					out.emplace_back(mesh.vertices()[tri.v[e]], mesh.vertices()[tri.v[(e + 1) % 3]]);
				}
			}
		}
		std::sort(out.begin(), out.end());
		return out;
	}

	bool boxesMeet(const Vec2i64& boxLow, const Vec2i64& boxHigh, const std::pair<Vec2i64, Vec2i64>& edge) {
		return std::max(edge.first.x, edge.second.x) >= boxLow.x && std::min(edge.first.x, edge.second.x) <= boxHigh.x && std::max(edge.first.y, edge.second.y) >= boxLow.y &&
			   std::min(edge.first.y, edge.second.y) <= boxHigh.y;
	}

	double distanceToOutline(const Vec2i64& point, const std::vector<Vec2i64>& shape) {
		double nearest = 1e300;
		for (std::size_t i = 0; i < shape.size(); ++i) {
			nearest = std::min(nearest, geometry::distanceToSegment(point, shape[i], shape[(i + 1) % shape.size()]));
		}
		return nearest;
	}

	struct Tally {
		int shapes	   = 0;
		int covered	   = 0;
		int exact	   = 0;
		int bandFree   = 0;
		int samples	   = 0;
		int nonStar	   = 0;
	};

	// The covered test on one shape against the record, checked three ways: a shape the ground
	// holds exactly is covered; where no mesh edge comes within the tolerance band of the shape's
	// outline it agrees with groundCovers; and a covered shape has every sampled point deeper
	// than the tolerance inside it on the ground, the samples taken at random and just outside
	// each nearby mesh edge.
	void check(PatchableMesh& mesh, const std::vector<Vec2i64>& shape, const Vec2i64& centre, std::mt19937& rng, SightCoverScratch& scratch, Tally& tally) {
		if (!geometry::simpleAround(shape, centre)) {
			++tally.nonStar;
			return;
		}
		++tally.shapes;
		const bool covered = sightShapeCovered(mesh, shape, centre, scratch);
		const bool exact   = mesh.groundCovers(shape);
		tally.covered += covered ? 1 : 0;
		tally.exact += exact ? 1 : 0;
		EXPECT_TRUE(covered || !exact) << "the ground holds the shape from (" << centre.x << ", " << centre.y << ") but it is not called covered";

		Vec2i64 boxLow = shape.front();
		Vec2i64 boxHigh = boxLow;
		for (const Vec2i64& vertex : shape) {
			boxLow = {std::min(boxLow.x, vertex.x), std::min(boxLow.y, vertex.y)};
			boxHigh = {std::max(boxHigh.x, vertex.x), std::max(boxHigh.y, vertex.y)};
		}
		const std::vector<std::pair<Vec2i64, Vec2i64>> allEdges = walkMeshEdges(mesh);
		std::vector<std::int32_t>						edgeIds;
		std::vector<std::pair<Vec2i64, Vec2i64>>		 near;
		mesh.meshEdgesNear(boxLow, boxHigh, edgeIds, near);
		std::sort(near.begin(), near.end());
		bool bandFree = true;
		for (const auto& edge : allEdges) {
			if (!boxesMeet(boxLow, boxHigh, edge)) {
				continue;
			}
			EXPECT_TRUE(std::binary_search(near.begin(), near.end(), edge)) << "meshEdgesNear missed an edge meeting the box";
			for (std::size_t i = 0; i < shape.size() && bandFree; ++i) {
				const Vec2i64& from = shape[i];
				const Vec2i64& to	= shape[(i + 1) % shape.size()];
				bandFree			= !(geometry::withinDistanceOfSegment(edge.first, from, to, kSeenCoverToleranceMm + 1) ||
							 geometry::withinDistanceOfSegment(edge.second, from, to, kSeenCoverToleranceMm + 1) ||
							 geometry::withinDistanceOfSegment(from, edge.first, edge.second, kSeenCoverToleranceMm + 1) ||
							 geometry::intersectSegments(from, to, edge.first, edge.second).relation != geometry::SegmentRelation::Disjoint);
			}
		}
		if (bandFree) {
			++tally.bandFree;
			EXPECT_EQ(covered, exact) << "no mesh edge near the outline of the shape from (" << centre.x << ", " << centre.y << ")";
		}
		if (!covered) {
			return;
		}
		std::vector<Vec2i64> samples;
		for (int i = 0; i < 200; ++i) {
			const std::int64_t x = boxLow.x + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(boxHigh.x - boxLow.x + 1));
			const std::int64_t y = boxLow.y + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(boxHigh.y - boxLow.y + 1));
			samples.push_back({x, y});
		}
		for (const auto& [from, to] : near) {
			// The ground is left of a mesh edge; step off it to the right, just past the tolerance.
			const double alongX		= static_cast<double>(to.x - from.x);
			const double alongY		= static_cast<double>(to.y - from.y);
			const double length = std::sqrt(alongX * alongX + alongY * alongY);
			if (length == 0.0) {
				continue;
			}
			for (const double along : {0.1, 0.5, 0.9}) {
				for (const double off : {static_cast<double>(kSeenCoverToleranceMm) + 2.0, 50.0, 500.0}) {
					samples.push_back({from.x + std::llround(alongX * along + alongY / length * off), from.y + std::llround(alongY * along - alongX / length * off)});
				}
			}
		}
		for (const Vec2i64& sample : samples) {
			if (geometry::pointInPolygon(sample, shape) != geometry::PointInPolygon::Inside ||
				distanceToOutline(sample, shape) <= static_cast<double>(kSeenCoverToleranceMm) + 1.0) {
				continue;
			}
			++tally.samples;
			EXPECT_GE(mesh.locate(sample), 0) << "a covered shape from (" << centre.x << ", " << centre.y << ") leaves (" << sample.x << ", " << sample.y
											  << ") unseen, " << distanceToOutline(sample, shape) << " mm inside it";
		}
	}

} // namespace

// Visibility polygons from observers in and round random buildings, legs between them, and each
// polygon again from a few millimetres away (its walls exact, its rim moved), each tested against
// the record and then added when it is not covered.
TEST(SightCover, AgreesWithGroundCoversAndTheOracle) {
	std::mt19937	  rng(832);
	SightCoverScratch scratch;
	Tally			  tally;
	for (int scene = 0; scene < 3; ++scene) {
		const geometry::buildings::Building building  = geometry::buildings::randomBuilding(rng);
		const std::vector<Vec2i64>			observers = geometry::buildings::observersFor(building, rng, 24);
		PatchableMesh						mesh;
		for (std::size_t i = 0; i < observers.size(); ++i) {
			const Vec2i64&				observer = observers[i];
			const std::vector<Vec2i64> polygon	 = geometry::computeVisibilityPolygon(observer, 30000, building.walls);
			check(mesh, polygon, observer, rng, scratch, tally);
			if (geometry::simpleAround(polygon, observer) && !sightShapeCovered(mesh, polygon, observer, scratch)) {
				add(mesh, polygon);
			}
			const std::int64_t nudgeX = static_cast<std::int64_t>(rng() % 11) - 5;
			const std::int64_t nudgeY = static_cast<std::int64_t>(rng() % 11) - 5;
			const Vec2i64	   nudged = observer + Vec2i64{nudgeX, nudgeY};
			if (!building.insideWall(nudged)) {
				check(mesh, geometry::computeVisibilityPolygon(nudged, 30000, building.walls), nudged, rng, scratch, tally);
			}
			if (i > 0) {
				const Vec2i64&				previous = observers[i - 1];
				std::vector<Vec2i64>		points	 = geometry::sightPolygon(previous, 29950);
				const std::vector<Vec2i64> atEnd	 = geometry::sightPolygon(observer, 29950);
				points.insert(points.end(), atEnd.begin(), atEnd.end());
				std::sort(points.begin(), points.end());
				points.erase(std::unique(points.begin(), points.end()), points.end());
				std::vector<Vec2i64> hull;
				for (int pass = 0; pass < 2; ++pass) {
					const std::size_t floor = hull.size();
					for (const Vec2i64& point : points) {
						while (hull.size() >= floor + 2 &&
							   geometry::orientation(hull[hull.size() - 2], hull.back(), point) != geometry::Orientation::CounterClockwise) {
							hull.pop_back();
						}
						hull.push_back(point);
					}
					hull.pop_back();
					std::reverse(points.begin(), points.end());
				}
				if (previous != observer) {
					check(mesh, hull, previous, rng, scratch, tally);
				}
			}
		}
		EXPECT_EQ(mesh.validate(), "");
	}
	EXPECT_GT(tally.shapes, 150);
	EXPECT_GT(tally.covered, 10);
	EXPECT_GT(tally.bandFree, 10);
	EXPECT_GT(tally.samples, 1000);
	std::printf("[sight cover] %d shapes (%d not simple, skipped): %d covered, %d held exactly, %d with no mesh edge near the outline; %d deep samples\n",
				tally.shapes, tally.nonStar, tally.covered, tally.exact, tally.bandFree, tally.samples);
}

// A polygon from the same place twice is covered the second time, though every wall face of it
// lies exactly on the record's edge.
TEST(SightCover, SamePolygonAgainIsCovered) {
	std::mt19937						rng(9);
	const geometry::buildings::Building building = geometry::buildings::makeBuilding({5000, 7000}, {6000}, 0.3, {1234567, -7654321}, 1.0, rng);
	PatchableMesh						mesh;
	SightCoverScratch					scratch;
	const Vec2i64						observer = building.roomCentres[0];
	const std::vector<Vec2i64>			polygon	 = geometry::computeVisibilityPolygon(observer, 30000, building.walls);
	ASSERT_TRUE(geometry::simpleAround(polygon, observer));
	EXPECT_FALSE(sightShapeCovered(mesh, polygon, observer, scratch));
	add(mesh, polygon);
	EXPECT_TRUE(sightShapeCovered(mesh, polygon, observer, scratch));
	// Off the ground, the centre is not seen.
	EXPECT_FALSE(sightShapeCovered(mesh, geometry::sightPolygon({0, 0}, 30000), {0, 0}, scratch));
}

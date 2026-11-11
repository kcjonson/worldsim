#include "nav/PatchableMesh.h"

#include "arrangement/Arrangement.h"
#include "nav/NavMesh.h"
#include "nav/NavMeshDumpRings.test.h"
#include "nav/NavMeshRealRings.test.h"
#include "nav/PathQuery.h"
#include "predicates/Predicates.h"
#include "triangulation/ConstrainedDelaunay.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <random>
#include <set>
#include <utility>
#include <vector>

using geometry::Arrangement;
using geometry::InputSegment;
using geometry::Int128;
using geometry::Vec2i64;
using namespace geometry::nav;

namespace {

	constexpr std::int64_t kWater  = -1;
	constexpr std::int64_t kTree   = -2;
	constexpr std::int64_t kBorder = -3;

	// What the mesh should hold: every live ring under its id.
	using Model = std::map<RingId, NavInputPolygon>;

	std::int64_t encode(RingId ring, std::int32_t edge) {
		return (static_cast<std::int64_t>(ring) << 32) | static_cast<std::int64_t>(edge);
	}

	EdgeSource decode(std::int64_t index) {
		return {static_cast<RingId>(index >> 32), static_cast<std::int32_t>(index & 0xFFFFFFFF)};
	}

	std::string describe(const std::vector<EdgeSource>& sources) {
		std::string out;
		for (const EdgeSource& s : sources) {
			out += " " + std::to_string(s.ring) + "." + std::to_string(s.edge);
		}
		return out;
	}

	std::string describe(const Model& model) {
		std::string out;
		for (const auto& [id, poly] : model) {
			out += "\nring " + std::to_string(id) + (poly.blocked ? " blocked" : " bounds") + (poly.holeCapable ? " hole" : "") + ":";
			for (const Vec2i64& v : poly.ring) {
				out += " (" + std::to_string(v.x) + ", " + std::to_string(v.y) + ")";
			}
		}
		return out;
	}

	using EdgeKey = std::pair<Vec2i64, Vec2i64>;

	EdgeKey edgeKey(Vec2i64 a, Vec2i64 b) {
		if (b < a) {
			std::swap(a, b);
		}
		return {a, b};
	}

	// The arrangement triangulated inside its frame, each triangle covered by ring parity
	// counted from the frame inward. No sample decides any cover.
	struct Topology {
		geometry::ConstrainedDelaunay		   cdt;
		std::vector<std::vector<RingId>>	   cover; // per triangle
		bool								   consistent  = true;
		Int128								   groundArea2 = Int128(0);
		Vec2i64								   lo3;
		std::int64_t						   cell3 = 3;
		std::int64_t						   cols	 = 1;
		std::int64_t						   rows	 = 1;
		std::vector<std::vector<std::int32_t>> buckets; // ground triangles per cell

		// The cover of the ground triangle holding p3 (3x space), or null.
		const std::vector<RingId>* coverAt(const Vec2i64& p3) const {
			if (buckets.empty() || p3.x < lo3.x || p3.y < lo3.y) {
				return nullptr;
			}
			const std::int64_t cx = (p3.x - lo3.x) / cell3;
			const std::int64_t cy = (p3.y - lo3.y) / cell3;
			if (cx >= cols || cy >= rows) {
				return nullptr;
			}
			for (std::int32_t t : buckets[static_cast<std::size_t>(cy * cols + cx)]) {
				const geometry::CdtTriangle& tri = cdt.triangles[static_cast<std::size_t>(t)];
				const Vec2i64				 a	 = cdt.vertices[tri.v[0]] * 3;
				const Vec2i64				 b	 = cdt.vertices[tri.v[1]] * 3;
				const Vec2i64				 c	 = cdt.vertices[tri.v[2]] * 3;
				if (geometry::orientation(a, b, p3) != geometry::Orientation::Clockwise &&
					geometry::orientation(b, c, p3) != geometry::Orientation::Clockwise &&
					geometry::orientation(c, a, p3) != geometry::Orientation::Clockwise) {
					return &cover[static_cast<std::size_t>(t)];
				}
			}
			return nullptr;
		}
	};

	std::vector<RingId> toggledBy(const std::vector<RingId>& cover, const std::vector<std::int64_t>& provenance) {
		std::map<RingId, int> count;
		for (std::int64_t p : provenance) {
			++count[decode(p).ring];
		}
		std::set<RingId> out(cover.begin(), cover.end());
		for (const auto& [r, n] : count) {
			if (n % 2 == 1 && out.erase(r) == 0) {
				out.insert(r);
			}
		}
		return {out.begin(), out.end()};
	}

	Topology topology(const Arrangement& arrangement, const Model& model) {
		Topology topo;
		if (arrangement.vertices.empty()) {
			return topo;
		}
		std::vector<std::array<std::uint32_t, 2>> constraints;
		for (const geometry::ArrangementEdge& e : arrangement.edges) {
			constraints.push_back({static_cast<std::uint32_t>(e.from), static_cast<std::uint32_t>(e.to)});
		}
		topo.cdt			= geometry::buildConstrainedDelaunay(arrangement.vertices, constraints);
		const std::size_t n = topo.cdt.triangles.size();
		topo.cover.assign(n, {});
		std::vector<std::uint8_t> reached(n, 0);
		std::vector<std::int32_t> stack;
		for (std::size_t t = 0; t < n; ++t) {
			for (std::uint32_t v : topo.cdt.triangles[t].v) {
				if (v >= topo.cdt.frameStart && reached[t] == 0) {
					reached[t] = 1;
					stack.push_back(static_cast<std::int32_t>(t));
				}
			}
		}
		while (!stack.empty()) {
			const std::int32_t t = stack.back();
			stack.pop_back();
			const geometry::CdtTriangle& tri = topo.cdt.triangles[static_cast<std::size_t>(t)];
			for (int e = 0; e < 3; ++e) {
				const std::int32_t nb = tri.neighbor[static_cast<std::size_t>(e)];
				if (nb < 0) {
					continue;
				}
				const std::int32_t	c	   = tri.constraint[static_cast<std::size_t>(e)];
				std::vector<RingId> across = c < 0 ? topo.cover[static_cast<std::size_t>(t)]
												   : toggledBy(topo.cover[static_cast<std::size_t>(t)],
															   arrangement.edges[static_cast<std::size_t>(c)].provenance);
				if (reached[static_cast<std::size_t>(nb)] == 0) {
					reached[static_cast<std::size_t>(nb)]	 = 1;
					topo.cover[static_cast<std::size_t>(nb)] = std::move(across);
					stack.push_back(nb);
				} else if (topo.cover[static_cast<std::size_t>(nb)] != across) {
					topo.consistent = false;
				}
			}
		}
		Vec2i64 lo = arrangement.vertices.front();
		Vec2i64 hi = lo;
		for (const Vec2i64& p : arrangement.vertices) {
			lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
			hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
		}
		topo.lo3   = lo * 3;
		topo.cell3 = 3 * (std::max(hi.x - lo.x, hi.y - lo.y) / 64 + 1);
		topo.cols  = (3 * (hi.x - lo.x)) / topo.cell3 + 1;
		topo.rows  = (3 * (hi.y - lo.y)) / topo.cell3 + 1;
		topo.buckets.assign(static_cast<std::size_t>(topo.cols * topo.rows), {});
		for (std::size_t t = 0; t < n; ++t) {
			bool ground = false;
			for (RingId r : topo.cover[t]) {
				ground = ground || !model.at(r).blocked;
			}
			if (!ground) {
				continue;
			}
			const geometry::CdtTriangle& tri = topo.cdt.triangles[t];
			const Vec2i64&				 a	 = topo.cdt.vertices[tri.v[0]];
			const Vec2i64&				 b	 = topo.cdt.vertices[tri.v[1]];
			const Vec2i64&				 c	 = topo.cdt.vertices[tri.v[2]];
			topo.groundArea2				 = topo.groundArea2 + cross(b - a, c - a);
			const Vec2i64 tlo{std::min({a.x, b.x, c.x}) * 3, std::min({a.y, b.y, c.y}) * 3};
			const Vec2i64 thi{std::max({a.x, b.x, c.x}) * 3, std::max({a.y, b.y, c.y}) * 3};
			for (std::int64_t cy = (tlo.y - topo.lo3.y) / topo.cell3; cy <= (thi.y - topo.lo3.y) / topo.cell3; ++cy) {
				for (std::int64_t cx = (tlo.x - topo.lo3.x) / topo.cell3; cx <= (thi.x - topo.lo3.x) / topo.cell3; ++cx) {
					topo.buckets[static_cast<std::size_t>(cy * topo.cols + cx)].push_back(static_cast<std::int32_t>(t));
				}
			}
		}
		return topo;
	}

	// The patched mesh against a from-scratch build of the same rings: (1) vertices and
	// constrained edges with their rings, exactly, over kept ground; (2) every triangle's
	// cover equals the arrangement's ring parity at its centroid and kept ground has equal
	// area; its tags equal buildNavMesh's at the centroid, where a mismatch is counted
	// into `builderMismatches` when given and fails otherwise; (3) validate().
	::testing::AssertionResult matchesFresh(const PatchableMesh& mesh, const Model& model, int* builderMismatches = nullptr) {
		const std::string problem = mesh.validate();
		if (!problem.empty()) {
			return ::testing::AssertionFailure() << "validate: " << problem;
		}

		NavMeshInput			  input;
		std::vector<InputSegment> segments;
		for (const auto& [id, poly] : model) {
			input.polygons.push_back(poly);
			for (std::size_t i = 0; i < poly.ring.size(); ++i) {
				segments.push_back({poly.ring[i], poly.ring[(i + 1) % poly.ring.size()], encode(id, static_cast<std::int32_t>(i))});
			}
		}
		const Arrangement arrangement = buildArrangement(segments);
		const NavMesh	  fresh		  = buildNavMesh(input);

		std::set<Vec2i64> freshVertices;
		std::set<Vec2i64> patchedVertices;
		for (const NavTriangle& t : fresh.triangles) {
			for (std::uint32_t v : t.v) {
				freshVertices.insert(fresh.vertices[v]);
			}
		}
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (mesh.triangleLive(t)) {
				for (std::uint32_t v : mesh.triangles()[static_cast<std::size_t>(t)].v) {
					patchedVertices.insert(mesh.vertices()[v]);
				}
			}
		}
		if (freshVertices != patchedVertices) {
			std::vector<Vec2i64> missing;
			std::vector<Vec2i64> extra;
			std::set_difference(freshVertices.begin(), freshVertices.end(), patchedVertices.begin(), patchedVertices.end(), std::back_inserter(missing));
			std::set_difference(patchedVertices.begin(), patchedVertices.end(), freshVertices.begin(), freshVertices.end(), std::back_inserter(extra));
			auto result = ::testing::AssertionFailure() << "vertex sets differ: " << missing.size() << " missing, " << extra.size() << " extra";
			if (!missing.empty()) {
				result << "; first missing (" << missing.front().x << ", " << missing.front().y << ")";
			}
			if (!extra.empty()) {
				result << "; first extra (" << extra.front().x << ", " << extra.front().y << ")";
			}
			return result;
		}

		std::map<std::pair<std::size_t, std::size_t>, const std::vector<std::int64_t>*> arrangementEdges;
		for (const geometry::ArrangementEdge& e : arrangement.edges) {
			arrangementEdges[{e.from, e.to}] = &e.provenance;
		}
		std::map<EdgeKey, std::vector<EdgeSource>> expected;
		for (const NavTriangle& t : fresh.triangles) {
			for (int e = 0; e < 3; ++e) {
				std::size_t a = t.v[static_cast<std::size_t>(e)];
				std::size_t b = t.v[static_cast<std::size_t>((e + 1) % 3)];
				auto		it = arrangementEdges.find({std::min(a, b), std::max(a, b)});
				if (it == arrangementEdges.end()) {
					continue;
				}
				std::vector<EdgeSource> sources;
				for (std::int64_t p : *it->second) {
					sources.push_back(decode(p));
				}
				std::sort(sources.begin(), sources.end());
				expected[edgeKey(arrangement.vertices[a], arrangement.vertices[b])] = sources;
			}
		}
		std::map<EdgeKey, std::vector<EdgeSource>> patched;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (!mesh.triangleLive(t)) {
				continue;
			}
			const NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
			for (int e = 0; e < 3; ++e) {
				const std::vector<EdgeSource>& sources = mesh.edgeSources(t, e);
				if (!sources.empty()) {
					patched[edgeKey(mesh.vertices()[tri.v[static_cast<std::size_t>(e)]], mesh.vertices()[tri.v[static_cast<std::size_t>((e + 1) % 3)]])] =
						sources;
				}
			}
		}
		if (expected != patched) {
			for (const auto& [key, sources] : expected) {
				auto it = patched.find(key);
				if (it == patched.end() || it->second != sources) {
					return ::testing::AssertionFailure() << "constrained edge (" << key.first.x << ", " << key.first.y << ")-(" << key.second.x
														 << ", " << key.second.y << ") " << (it == patched.end() ? "missing" : "has other rings")
														 << describe(sources) << " vs" << (it == patched.end() ? std::string() : describe(it->second));
				}
			}
			for (const auto& [key, sources] : patched) {
				if (expected.count(key) == 0) {
					return ::testing::AssertionFailure() << "extra constrained edge (" << key.first.x << ", " << key.first.y << ")-(" << key.second.x
														 << ", " << key.second.y << ")";
				}
			}
		}

		const Topology topo = topology(arrangement, model);
		if (!topo.consistent) {
			return ::testing::AssertionFailure() << "ring parity disagrees around a face of the fresh arrangement";
		}
		Int128 patchedGround(0);
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (!mesh.triangleLive(t)) {
				continue;
			}
			const NavTriangle&		   tri			 = mesh.triangles()[static_cast<std::size_t>(t)];
			const Vec2i64&			   a			 = mesh.vertices()[tri.v[0]];
			const Vec2i64&			   b			 = mesh.vertices()[tri.v[1]];
			const Vec2i64&			   c			 = mesh.vertices()[tri.v[2]];
			const std::vector<RingId>* expectedCover = topo.coverAt(a + b + c);
			if (expectedCover == nullptr || *expectedCover != mesh.cover(t)) {
				return ::testing::AssertionFailure() << "triangle " << t << " (" << a.x << ", " << a.y << ")-(" << b.x << ", " << b.y << ")-(" << c.x
													 << ", " << c.y << ") is covered by other rings than the arrangement's parity";
			}
			patchedGround = patchedGround + cross(b - a, c - a);
		}
		if (patchedGround != topo.groundArea2) {
			return ::testing::AssertionFailure() << "kept ground area differs from the arrangement's";
		}

		// buildNavMesh's tags, the fresh mesh scaled by 3 so it locates centroids exactly.
		NavMesh scaled = fresh;
		for (Vec2i64& v : scaled.vertices) {
			v = v * 3;
		}
		scaled.grid.minPt	 = scaled.grid.minPt * 3;
		scaled.grid.maxPt	 = scaled.grid.maxPt * 3;
		scaled.grid.cellSize = scaled.grid.cellSize * 3;
		if (builderMismatches != nullptr) {
			*builderMismatches = 0;
		}
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (!mesh.triangleLive(t)) {
				continue;
			}
			const NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
			const Vec2i64&	   a   = mesh.vertices()[tri.v[0]];
			const std::int32_t ft  = locateTriangle(scaled, a + mesh.vertices()[tri.v[1]] + mesh.vertices()[tri.v[2]]);
			if (ft < 0) {
				return ::testing::AssertionFailure() << "triangle " << t << " lies off the fresh mesh";
			}
			const NavTriangle& f = fresh.triangles[static_cast<std::size_t>(ft)];
			if (f.faceBlocker == tri.faceBlocker && f.faceOpening == tri.faceOpening) {
				continue;
			}
			if (builderMismatches == nullptr) {
				return ::testing::AssertionFailure() << "triangle " << t << " at (" << a.x << ", " << a.y << ") tagged " << tri.faceBlocker
													 << ", fresh build " << f.faceBlocker;
			}
			++*builderMismatches;
		}
		return ::testing::AssertionSuccess();
	}

	NavInputPolygon blocked(std::vector<Vec2i64> ring, std::int64_t provenance = kTree, bool holeCapable = false) {
		return NavInputPolygon{std::move(ring), true, provenance, kNoOpening, holeCapable};
	}

	NavInputPolygon bounds(std::vector<Vec2i64> ring) {
		return NavInputPolygon{std::move(ring), false, kBorder};
	}

	std::vector<Vec2i64> rect(std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1) {
		return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
	}

	std::vector<Vec2i64> translated(std::vector<Vec2i64> ring, Vec2i64 by) {
		for (Vec2i64& p : ring) {
			p = p + by;
		}
		return ring;
	}

	// Applies a patch to the mesh and the model together.
	struct Harness {
		PatchableMesh mesh;
		Model		  model;

		PatchReport apply(const PatchEdits& edits) {
			PatchReport report = mesh.applyPatch(edits);
			for (std::size_t i = 0; i < edits.adds.size(); ++i) {
				model[report.added[i]] = edits.adds[i];
			}
			for (RingId r : edits.removes) {
				model.erase(r);
			}
			for (const RingMove& move : edits.moves) {
				model[move.ring].ring = move.outline;
			}
			return report;
		}

		RingId add(const NavInputPolygon& ring) {
			PatchEdits edits;
			edits.adds.push_back(ring);
			return apply(edits).added.front();
		}
	};

	std::uint64_t pick(std::mt19937& rng, std::uint64_t n) {
		return static_cast<std::uint64_t>(rng()) % n;
	}

	std::int64_t between(std::mt19937& rng, std::int64_t lo, std::int64_t hi) {
		return lo + static_cast<std::int64_t>(pick(rng, static_cast<std::uint64_t>(hi - lo + 1)));
	}

	// --- Game fixtures: border and water, then trees in random batches ------------------

	struct Fixture {
		std::vector<NavInputPolygon> base;
		std::vector<NavInputPolygon> trees;
	};

	Fixture dumpFixture(const std::vector<testdata::DumpRing>& rings) {
		Fixture f;
		for (const testdata::DumpRing& r : rings) {
			NavInputPolygon poly{r.ring, r.blocked, r.provenanceId, kNoOpening, r.holeCapable};
			(r.provenanceId == kTree ? f.trees : f.base).push_back(std::move(poly));
		}
		return f;
	}

	Fixture realRingsFixture() {
		Fixture f;
		f.base.push_back(bounds(testdata::realBorderRing()));
		f.base.push_back(blocked(testdata::realWaterRing(), kWater, true));
		for (const std::vector<Vec2i64>& tree : testdata::realTreeRings()) {
			f.trees.push_back(blocked(tree));
		}
		return f;
	}

	void runFixtureSequence(const Fixture& fixture, std::uint32_t seed, int checkEvery) {
		std::mt19937 rng(seed);
		Harness		 h;
		{
			PatchEdits edits;
			edits.adds = fixture.base;
			h.apply(edits);
		}
		ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "after the base rings";

		std::vector<NavInputPolygon> pending = fixture.trees;
		std::shuffle(pending.begin(), pending.end(), rng);
		std::vector<RingId> placed;
		int					step = 0;
		while (!pending.empty()) {
			PatchEdits	   edits;
			const unsigned op = static_cast<unsigned>(pick(rng, 10));
			if (op < 6 || placed.size() < 8) {
				const std::size_t count = std::min<std::size_t>(pending.size(), 1 + pick(rng, 40));
				edits.adds.assign(pending.end() - static_cast<std::ptrdiff_t>(count), pending.end());
				pending.resize(pending.size() - count);
			}
			if (op >= 6 && op < 8 && placed.size() >= 8) {
				const std::size_t count = 1 + pick(rng, 8);
				for (std::size_t i = 0; i < count; ++i) {
					const std::size_t at = pick(rng, placed.size());
					pending.insert(pending.begin(), h.model[placed[at]]);
					edits.removes.push_back(placed[at]);
					placed.erase(placed.begin() + static_cast<std::ptrdiff_t>(at));
				}
			}
			if (op >= 8 && placed.size() >= 8) {
				const std::size_t count = 1 + pick(rng, 4);
				std::set<RingId>  moved;
				for (std::size_t i = 0; i < count; ++i) {
					const RingId r = placed[pick(rng, placed.size())];
					if (moved.insert(r).second) {
						edits.moves.push_back({r, translated(h.model[r].ring, {between(rng, -1500, 1500), between(rng, -1500, 1500)})});
					}
				}
			}
			const PatchReport report = h.apply(edits);
			placed.insert(placed.end(), report.added.begin(), report.added.end());
			if (++step % checkEvery == 0) {
				ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "seed " << seed << " step " << step;
			}
		}
		ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "seed " << seed << " at the end";

		// Remove every tree in a few batches: the mesh returns to border and water.
		std::shuffle(placed.begin(), placed.end(), rng);
		while (!placed.empty()) {
			PatchEdits		  edits;
			const std::size_t count = std::min<std::size_t>(placed.size(), 1 + pick(rng, 120));
			edits.removes.assign(placed.end() - static_cast<std::ptrdiff_t>(count), placed.end());
			placed.resize(placed.size() - count);
			h.apply(edits);
		}
		ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "seed " << seed << " with every tree removed";
	}

	// --- Adversarial soups: tiny coordinates so rings overlap, share edges, touch, nest --

	std::vector<Vec2i64> randomRing(std::mt19937& rng) {
		switch (pick(rng, 3)) {
			case 0: {
				const std::int64_t x = between(rng, -4, 42);
				const std::int64_t y = between(rng, -4, 42);
				return rect(x, y, x + between(rng, 1, 12), y + between(rng, 1, 12));
			}
			case 1: {
				for (;;) {
					const Vec2i64 a{between(rng, -4, 44), between(rng, -4, 44)};
					const Vec2i64 b{a.x + between(rng, -10, 10), a.y + between(rng, -10, 10)};
					const Vec2i64 c{a.x + between(rng, -10, 10), a.y + between(rng, -10, 10)};
					const geometry::Orientation o = geometry::orientation(a, b, c);
					if (o != geometry::Orientation::Collinear) {
						return o == geometry::Orientation::CounterClockwise ? std::vector<Vec2i64>{a, b, c} : std::vector<Vec2i64>{a, c, b};
					}
				}
			}
			default: {
				// One point per open quadrant around a centre: simple, often not convex.
				const Vec2i64 o{between(rng, 0, 40), between(rng, 0, 40)};
				return {{o.x + between(rng, 1, 7), o.y + between(rng, 1, 7)},
						{o.x - between(rng, 1, 7), o.y + between(rng, 1, 7)},
						{o.x - between(rng, 1, 7), o.y - between(rng, 1, 7)},
						{o.x + between(rng, 1, 7), o.y - between(rng, 1, 7)}};
			}
		}
	}

	// Water with an island and sometimes a pond on it, as separate hole-capable rings.
	std::vector<NavInputPolygon> randomWater(std::mt19937& rng) {
		const std::int64_t x = between(rng, -6, 20);
		const std::int64_t y = between(rng, -6, 20);
		const std::int64_t w = between(rng, 10, 26);
		std::vector<NavInputPolygon> out;
		out.push_back(blocked(rect(x, y, x + w, y + w), kWater, true));
		const std::int64_t ix = x + between(rng, 1, w / 3);
		const std::int64_t iy = y + between(rng, 1, w / 3);
		const std::int64_t iw = between(rng, 3, w - (ix - x) - 1);
		out.push_back(blocked({{ix, iy}, {ix, iy + iw}, {ix + iw, iy + iw}, {ix + iw, iy}}, kWater, true));
		if (pick(rng, 2) == 0 && iw >= 4) {
			out.push_back(blocked(rect(ix + 1, iy + 1, ix + iw - 1, iy + iw - 1), kWater, true));
		}
		return out;
	}

	// Every patched state is checked; a state where buildNavMesh's tags disagree with the
	// arrangement's parity is counted into builderMismatchStates.
	void runSoup(std::uint32_t seed, int steps, int& builderMismatchStates) {
		std::mt19937 rng(seed);
		Harness		 h;
		// Two chunk squares sharing x = 20; the second arrives part way through, and
		// sometimes leaves and comes back.
		const NavInputPolygon west		= bounds(rect(0, 0, 20, 40));
		const NavInputPolygon east		= bounds(rect(20, 0, 40, 40));
		const bool			  eastFirst = pick(rng, 2) == 0;
		h.add(eastFirst ? east : west);
		RingId second = kNoRing;
		const int			  arrival	= static_cast<int>(pick(rng, static_cast<std::uint64_t>(steps)));
		std::vector<RingId>	  placed;
		ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "seed " << seed << " first bounds";
		for (int step = 0; step < steps; ++step) {
			PatchEdits edits;
			if (step == arrival) {
				edits.adds.push_back(eastFirst ? west : east);
			} else if (second != kNoRing && pick(rng, 12) == 0) {
				edits.removes.push_back(second);
				second = kNoRing;
			} else if (second == kNoRing && step > arrival && pick(rng, 3) == 0) {
				edits.adds.push_back(eastFirst ? west : east);
			}
			const bool boundsAdded = !edits.adds.empty();
			const unsigned op = static_cast<unsigned>(pick(rng, 10));
			if (op < 5 || placed.size() < 3) {
				const std::size_t count = 1 + pick(rng, 4);
				for (std::size_t i = 0; i < count; ++i) {
					if (pick(rng, 8) == 0) {
						for (NavInputPolygon& w : randomWater(rng)) {
							edits.adds.push_back(std::move(w));
						}
					} else {
						edits.adds.push_back(blocked(randomRing(rng)));
					}
				}
			} else if (op < 8) {
				const std::size_t count = 1 + pick(rng, 2);
				for (std::size_t i = 0; i < count && !placed.empty(); ++i) {
					const std::size_t at = pick(rng, placed.size());
					edits.removes.push_back(placed[at]);
					placed.erase(placed.begin() + static_cast<std::ptrdiff_t>(at));
				}
			} else {
				const RingId r = placed[pick(rng, placed.size())];
				edits.moves.push_back({r, translated(h.model[r].ring, {between(rng, -3, 3), between(rng, -3, 3)})});
			}
			const PatchReport report = h.apply(edits);
			for (std::size_t i = 0; i < report.added.size(); ++i) {
				if (boundsAdded && i == 0) {
					second = report.added[i];
				} else {
					placed.push_back(report.added[i]);
				}
			}
			int mismatches = 0;
			ASSERT_TRUE(matchesFresh(h.mesh, h.model, &mismatches)) << "seed " << seed << " step " << step << describe(h.model);
			builderMismatchStates += mismatches > 0 ? 1 : 0;
		}
	}

} // namespace

TEST(PatchableMesh, FirstBoundsMeshesAnEmptyMesh) {
	Harness h;
	h.add(bounds(rect(0, 0, 1000, 1000)));
	EXPECT_EQ(h.mesh.liveTriangleCount(), 2);
	EXPECT_EQ(h.mesh.lastPatch().newTriangles.size(), 2U);
	EXPECT_TRUE(h.mesh.lastPatch().freedTriangles.empty());
	EXPECT_TRUE(matchesFresh(h.mesh, h.model));
}

TEST(PatchableMesh, RingOffGroundTouchesNothing) {
	Harness h;
	h.add(bounds(rect(0, 0, 1000, 1000)));
	h.add(blocked(rect(2000, 2000, 2100, 2100)));
	EXPECT_EQ(h.mesh.liveTriangleCount(), 2);
	EXPECT_TRUE(h.mesh.lastPatch().newTriangles.empty());
	EXPECT_TRUE(matchesFresh(h.mesh, h.model));
}

TEST(PatchableMesh, AddRemoveMoveTree) {
	Harness h;
	h.add(bounds(rect(0, 0, 10000, 10000)));
	const RingId tree = h.add(blocked(rect(4000, 4000, 4500, 4600)));
	EXPECT_TRUE(matchesFresh(h.mesh, h.model));
	PatchEdits move;
	move.moves.push_back({tree, rect(4300, 4100, 4800, 4700)});
	h.apply(move);
	EXPECT_TRUE(matchesFresh(h.mesh, h.model));
	PatchEdits remove;
	remove.removes.push_back(tree);
	h.apply(remove);
	EXPECT_EQ(h.mesh.liveTriangleCount(), 2);
	EXPECT_TRUE(matchesFresh(h.mesh, h.model));
}

TEST(PatchableMesh, StableIdsOutsideTheHole) {
	Harness h;
	h.add(bounds(rect(0, 0, 100000, 100000)));
	for (std::int64_t i = 0; i < 10; ++i) {
		h.add(blocked(rect(5000 + i * 9000, 5000, 6000 + i * 9000, 6000)));
	}
	const std::vector<NavTriangle> before = h.mesh.triangles();
	std::vector<std::uint8_t>	   liveBefore;
	for (std::int32_t t = 0; t < static_cast<std::int32_t>(before.size()); ++t) {
		liveBefore.push_back(h.mesh.triangleLive(t) ? 1 : 0);
	}
	h.add(blocked(rect(50000, 80000, 51000, 81000)));
	const PatchReport report = h.mesh.lastPatch();
	for (std::int32_t t = 0; t < static_cast<std::int32_t>(before.size()); ++t) {
		const bool touched = std::binary_search(report.freedTriangles.begin(), report.freedTriangles.end(), t);
		if (liveBefore[static_cast<std::size_t>(t)] == 0 || touched) {
			continue;
		}
		const NavTriangle& now = h.mesh.triangles()[static_cast<std::size_t>(t)];
		EXPECT_TRUE(h.mesh.triangleLive(t));
		EXPECT_EQ(now.v, before[static_cast<std::size_t>(t)].v) << "triangle " << t;
	}
	EXPECT_TRUE(matchesFresh(h.mesh, h.model));
}

TEST(PatchableMesh, LocateTieBreaksToLowestId) {
	Harness h;
	h.add(bounds(rect(0, 0, 1000, 1000)));
	h.add(blocked(rect(400, 400, 600, 600)));
	const std::vector<NavTriangle>& tris = h.mesh.triangles();
	for (std::int32_t t = 0; t < static_cast<std::int32_t>(tris.size()); ++t) {
		if (!h.mesh.triangleLive(t)) {
			continue;
		}
		for (std::uint32_t v : tris[static_cast<std::size_t>(t)].v) {
			const Vec2i64 p = h.mesh.vertices()[v];
			std::int32_t  lowest = -1;
			for (std::int32_t u = 0; u < static_cast<std::int32_t>(tris.size()); ++u) {
				if (h.mesh.triangleLive(u) && lowest < 0) {
					const NavTriangle& x = tris[static_cast<std::size_t>(u)];
					if (x.v[0] == v || x.v[1] == v || x.v[2] == v) {
						lowest = u;
					}
				}
			}
			EXPECT_EQ(h.mesh.locate(p), lowest);
		}
	}
	EXPECT_EQ(h.mesh.locate({-1, 500}), -1);
	EXPECT_EQ(h.mesh.locate({1001, 500}), -1);
}

// Chunk squares sharing a side, in both orders, with a tree hanging off the first one's
// edge: its outside part has no triangles until the neighbour arrives and completes it.
TEST(PatchableMesh, HangingRingCompletedByNeighbourBounds) {
	for (const bool westFirst : {true, false}) {
		Harness				  h;
		const NavInputPolygon west = bounds(rect(0, 0, 10000, 10000));
		const NavInputPolygon east = bounds(rect(10000, 0, 20000, 10000));
		h.add(westFirst ? west : east);
		h.add(blocked({{9400, 4000}, {10700, 4300}, {10500, 5200}, {9300, 5100}}));
		h.add(blocked(rect(9000, 6000, 11000, 6500)));
		ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << (westFirst ? "west" : "east") << " alone";
		h.add(westFirst ? east : west);
		ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "both";
		PatchEdits leave;
		leave.removes.push_back(h.model.begin()->first);
		h.apply(leave);
		ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "first square unloaded";
	}
}

TEST(PatchableMesh, WaterIslandAndPondParity) {
	Harness h;
	h.add(bounds(rect(0, 0, 10000, 10000)));
	PatchEdits water;
	water.adds.push_back(blocked(rect(1000, 1000, 9000, 9000), kWater, true));
	water.adds.push_back(blocked({{3000, 3000}, {3000, 7000}, {7000, 7000}, {7000, 3000}}, kWater, true));
	water.adds.push_back(blocked(rect(4000, 4000, 6000, 6000), kWater, true));
	h.apply(water);
	ASSERT_TRUE(matchesFresh(h.mesh, h.model));
	const RingId tree = h.add(blocked(rect(2500, 4500, 3500, 5500)));
	ASSERT_TRUE(matchesFresh(h.mesh, h.model));
	PatchEdits move;
	move.moves.push_back({tree, rect(5500, 5500, 6500, 6500)});
	h.apply(move);
	ASSERT_TRUE(matchesFresh(h.mesh, h.model));
}

TEST(PatchableMesh, OverlappingAddsInOneBatchMerge) {
	Harness h;
	h.add(bounds(rect(0, 0, 10000, 10000)));
	PatchEdits batch;
	for (std::int64_t i = 0; i < 30; ++i) {
		batch.adds.push_back(blocked({{3000 + i * 97, 3000 + i * 61}, {3900 + i * 83, 3100 + i * 71}, {3800 + i * 53, 4100 + i * 43}}));
	}
	h.apply(batch);
	EXPECT_EQ(h.mesh.lastPatch().added.size(), 30U);
	EXPECT_TRUE(matchesFresh(h.mesh, h.model));
}

TEST(PatchableMesh, FixtureManyTreesMatchesFreshBuild) {
	runFixtureSequence(dumpFixture(testdata::manyTreesDump()), 11, 6);
}

TEST(PatchableMesh, FixtureControlMatchesFreshBuild) {
	runFixtureSequence(dumpFixture(testdata::controlDump()), 23, 6);
}

TEST(PatchableMesh, FixtureRealRingsMatchesFreshBuild) {
	runFixtureSequence(realRingsFixture(), 37, 6);
}

// Tiny coordinates, so rings overlap, share edges, touch at corners, nest, and hang off
// ground, and snap rounding bends nearly every crossing. Raise kSoupSeeds for a heavier
// sweep (300 seeds of 40 steps held).
TEST(PatchableMesh, AdversarialSoupsMatchFreshBuild) {
	constexpr std::uint32_t kSoupSeeds			  = 8;
	int						builderMismatchStates = 0;
	for (std::uint32_t seed = 1; seed <= kSoupSeeds; ++seed) {
		runSoup(seed, 40, builderMismatchStates);
		if (HasFatalFailure()) {
			return;
		}
	}
	RecordProperty("builderMismatchStates", builderMismatchStates);
}

// buildNavMesh tags a face by the exact containment of one sample, its largest
// triangle's centroid. Snap rounding can leave a face straddling the original line of a
// ring, and then the tag depends on which triangle is largest: here a tree wholly off
// ground, touching nothing on it, changes the build's triangulation enough to flip one
// face's tag. The patched mesh takes covers from ring parity and matches the
// arrangement either way.
TEST(PatchableMesh, BuilderTagDependsOnItsSampleTriangle) {
	const std::vector<NavInputPolygon> rings = {
		bounds(rect(0, 0, 20, 40)),
		blocked({{-4, 9}, {13, 9}, {13, 26}, {-4, 26}}, kWater, true),
		blocked({{2, 8}, {2, 20}, {14, 20}, {14, 8}}, kWater, true),
		blocked({{3, 9}, {13, 9}, {13, 19}, {3, 19}}, kWater, true),
		blocked({{14, 22}, {4, 27}, {4, 13}, {12, 13}}),
	};
	for (const bool withOffGround : {false, true}) {
		Harness	   h;
		PatchEdits edits;
		edits.adds = rings;
		if (withOffGround) {
			edits.adds.push_back(blocked({{-4, 42}, {-5, 41}, {2, 46}}));
		}
		h.apply(edits);
		int mismatches = 0;
		ASSERT_TRUE(matchesFresh(h.mesh, h.model, &mismatches));
		EXPECT_EQ(mismatches > 0, withOffGround);
	}
}

// Hole sizes on the Y confluence with every tree in: each tree taken out and put back
// alone, sorted by what it touches, plus a tree across the border. Recorded, not
// asserted: the design's cost depends on them.
TEST(PatchableMesh, HoleSizesOnRealRings) {
	const Fixture fixture = realRingsFixture();
	Harness		  h;
	{
		PatchEdits edits;
		edits.adds = fixture.base;
		edits.adds.insert(edits.adds.end(), fixture.trees.begin(), fixture.trees.end());
		h.apply(edits);
	}
	const NavInputPolygon& water = fixture.base[1];
	struct Sizes {
		std::vector<std::size_t> freed;
		std::vector<std::size_t> created;
		std::vector<int>		 passes;
	};
	std::map<std::string, Sizes> sizes;
	auto						 overlaps = [](const NavInputPolygon& a, const NavInputPolygon& b) {
		for (std::size_t i = 0; i < a.ring.size(); ++i) {
			for (std::size_t j = 0; j < b.ring.size(); ++j) {
				if (geometry::intersectSegments(a.ring[i], a.ring[(i + 1) % a.ring.size()], b.ring[j], b.ring[(j + 1) % b.ring.size()]).relation !=
					geometry::SegmentRelation::Disjoint) {
					return true;
				}
			}
		}
		return false;
	};
	std::vector<RingId> trees;
	for (const auto& [id, poly] : h.model) {
		if (poly.provenanceId == kTree) {
			trees.push_back(id);
		}
	}
	for (RingId tree : trees) {
		const NavInputPolygon poly = h.model[tree];
		bool				  alone = true;
		for (const auto& [other, otherPoly] : h.model) {
			alone = alone && (other == tree || !overlaps(poly, otherPoly));
		}
		const std::string kind = overlaps(poly, water) ? "shoreline" : (alone ? "lone" : "");
		PatchEdits		  remove;
		remove.removes.push_back(tree);
		h.apply(remove);
		const PatchReport report = h.mesh.applyPatch([&] {
			PatchEdits add;
			add.adds.push_back(poly);
			return add;
		}());
		h.model[report.added.front()] = poly;
		if (!kind.empty()) {
			sizes[kind].freed.push_back(report.freedTriangles.size());
			sizes[kind].created.push_back(report.newTriangles.size());
			sizes[kind].passes.push_back(report.passes);
		}
	}
	h.add(blocked(rect(-63300, 30000, -62700, 30600)));
	sizes["border"].freed.push_back(h.mesh.lastPatch().freedTriangles.size());
	sizes["border"].created.push_back(h.mesh.lastPatch().newTriangles.size());
	sizes["border"].passes.push_back(h.mesh.lastPatch().passes);
	ASSERT_TRUE(matchesFresh(h.mesh, h.model));

	for (auto& [kind, s] : sizes) {
		auto summary = [](auto values) {
			std::sort(values.begin(), values.end());
			return std::to_string(values.front()) + "/" + std::to_string(values[values.size() / 2]) + "/" + std::to_string(values.back());
		};
		RecordProperty(kind + "Count", static_cast<int>(s.freed.size()));
		RecordProperty(kind + "FreedMinMedianMax", summary(s.freed));
		RecordProperty(kind + "NewMinMedianMax", summary(s.created));
		RecordProperty(kind + "PassesMinMedianMax", summary(s.passes));
	}
}


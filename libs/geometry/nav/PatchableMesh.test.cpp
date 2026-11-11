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
#include <array>
#include <cmath>
#include <string>
#include <unordered_set>
#include <cstdint>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <utility>
#include <vector>

using geometry::Arrangement;
using geometry::InputSegment;
using geometry::Int128;
using geometry::PagedVector;
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

	// Applies a patch to the mesh and the model together. The model takes each ring as
	// registered, so a split bounds outline is what the from-scratch build sees too.
	struct Harness {
		explicit Harness(std::int64_t maxBoundsEdgeMm = 0) : mesh({.maxBoundsEdgeMm = maxBoundsEdgeMm}) {}

		PatchableMesh mesh;
		Model		  model;
		PatchReport	  last;

		PatchReport apply(const PatchEdits& edits) {
			PatchReport report = mesh.applyPatch(edits);
			mesh.refreshWidths();
			last = report;
			for (RingId r : report.added) {
				model[r] = mesh.ring(r).polygon;
			}
			for (RingId r : edits.removes) {
				model.erase(r);
			}
			for (const RingMove& move : edits.moves) {
				model[move.ring] = mesh.ring(move.ring).polygon;
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

	void runFixtureSequence(const Fixture& fixture, std::uint32_t seed, int checkEvery, std::int64_t maxBoundsEdgeMm = 0) {
		std::mt19937 rng(seed);
		Harness		 h(maxBoundsEdgeMm);
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
		Harness		 h(seed % 2 == 0 ? 7 : 0); // even seeds split the 20 and 40 mm sides
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
	EXPECT_EQ(h.last.newTriangles.size(), 2U);
	EXPECT_TRUE(h.last.freedTriangles.empty());
	EXPECT_TRUE(matchesFresh(h.mesh, h.model));
}

TEST(PatchableMesh, RingOffGroundTouchesNothing) {
	Harness h;
	h.add(bounds(rect(0, 0, 1000, 1000)));
	h.add(blocked(rect(2000, 2000, 2100, 2100)));
	EXPECT_EQ(h.mesh.liveTriangleCount(), 2);
	EXPECT_TRUE(h.last.newTriangles.empty());
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

// Ground pinched at a point: two squares meeting at one corner, so the corner's star
// is two fans the walk around a vertex can't cross between.
TEST(PatchableMesh, SquaresMeetingAtACorner) {
	Harness h;
	h.add(bounds(rect(0, 0, 10000, 10000)));
	h.add(bounds(rect(10000, 10000, 20000, 20000)));
	EXPECT_TRUE(matchesFresh(h.mesh, h.model));
	const RingId tree = h.add(blocked(rect(9500, 9000, 10500, 10000)));
	EXPECT_TRUE(matchesFresh(h.mesh, h.model));
	PatchEdits remove;
	remove.removes.push_back(tree);
	h.apply(remove);
	EXPECT_TRUE(matchesFresh(h.mesh, h.model));
	h.add(bounds(rect(10000, 0, 20000, 10000)));
	EXPECT_TRUE(matchesFresh(h.mesh, h.model));
}

// A copy starts with empty patch scratch; its first patch must size it before reading.
TEST(PatchableMesh, CopiedMeshPatchesLikeTheOriginal) {
	PatchableMesh original;
	PatchEdits	  load;
	load.adds.push_back(bounds(rect(0, 0, 40000, 40000)));
	for (std::int64_t i = 0; i < 30; ++i) {
		load.adds.push_back(blocked(rect(2000 + i * 1200, 3000 + (i % 5) * 6000, 2600 + i * 1200, 3700 + (i % 5) * 6000)));
	}
	const PatchReport loaded = original.applyPatch(load);
	PatchEdits		  edits;
	edits.removes.push_back(loaded.added[12]);
	edits.adds.push_back(blocked(rect(20000, 20000, 21000, 21500)));

	PatchableMesh copy = original;
	PatchableMesh moved(PatchableMesh{original});
	const PatchReport expected = original.applyPatch(edits);
	for (PatchableMesh* mesh : {&copy, &moved}) {
		const PatchReport got = mesh->applyPatch(edits);
		EXPECT_EQ(mesh->validate(), "");
		EXPECT_EQ(got.added, expected.added);
		EXPECT_EQ(got.freedTriangles, expected.freedTriangles);
		EXPECT_EQ(got.newTriangles, expected.newTriangles);
		EXPECT_EQ(mesh->vertices(), original.vertices());
		ASSERT_EQ(mesh->triangles().size(), original.triangles().size());
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(original.triangles().size()); ++t) {
			ASSERT_EQ(mesh->triangleLive(t), original.triangleLive(t));
			if (original.triangleLive(t)) {
				EXPECT_EQ(mesh->triangles()[static_cast<std::size_t>(t)].v, original.triangles()[static_cast<std::size_t>(t)].v);
				EXPECT_EQ(mesh->cover(t), original.cover(t));
			}
		}
	}
}

TEST(PatchableMesh, StableIdsOutsideTheHole) {
	Harness h;
	h.add(bounds(rect(0, 0, 100000, 100000)));
	for (std::int64_t i = 0; i < 10; ++i) {
		h.add(blocked(rect(5000 + i * 9000, 5000, 6000 + i * 9000, 6000)));
	}
	const PagedVector<NavTriangle> before = h.mesh.triangles();
	std::vector<std::uint8_t>	   liveBefore;
	for (std::int32_t t = 0; t < static_cast<std::int32_t>(before.size()); ++t) {
		liveBefore.push_back(h.mesh.triangleLive(t) ? 1 : 0);
	}
	h.add(blocked(rect(50000, 80000, 51000, 81000)));
	const PatchReport report = h.last;
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

// A point on shared corners goes to the triangle whose sorted corners come first, so the
// answer is the geometry's and not the slot order's (which follows patch history).
TEST(PatchableMesh, LocateTieBreaksOnCorners) {
	Harness h;
	h.add(bounds(rect(0, 0, 1000, 1000)));
	h.add(blocked(rect(400, 400, 600, 600)));
	const PagedVector<NavTriangle>& tris = h.mesh.triangles();
	auto sortedCorners = [&](const NavTriangle& x) {
		std::array<Vec2i64, 3> c{h.mesh.vertices()[x.v[0]], h.mesh.vertices()[x.v[1]], h.mesh.vertices()[x.v[2]]};
		std::sort(c.begin(), c.end());
		return c;
	};
	for (std::int32_t t = 0; t < static_cast<std::int32_t>(tris.size()); ++t) {
		if (!h.mesh.triangleLive(t)) {
			continue;
		}
		for (std::uint32_t v : tris[static_cast<std::size_t>(t)].v) {
			const Vec2i64 p	   = h.mesh.vertices()[v];
			std::int32_t  first = -1;
			for (std::int32_t u = 0; u < static_cast<std::int32_t>(tris.size()); ++u) {
				const NavTriangle& x = tris[static_cast<std::size_t>(u)];
				if (h.mesh.triangleLive(u) && (x.v[0] == v || x.v[1] == v || x.v[2] == v) &&
					(first < 0 || sortedCorners(x) < sortedCorners(tris[static_cast<std::size_t>(first)]))) {
					first = u;
				}
			}
			EXPECT_EQ(h.mesh.locate(p), first);
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
	EXPECT_EQ(h.last.added.size(), 30U);
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

TEST(PatchableMesh, FixtureRealRingsWithSplitBoundsMatchesFreshBuild) {
	runFixtureSequence(realRingsFixture(), 41, 8, 16000);
}

TEST(PatchableMesh, SplitLongEdgesCutsOnTheEdge) {
	EXPECT_EQ(splitLongEdges(rect(0, 0, 10, 4), 0), rect(0, 0, 10, 4));
	EXPECT_EQ(splitLongEdges(rect(0, 0, 10, 4), 4),
			  (std::vector<Vec2i64>{{0, 0}, {3, 0}, {6, 0}, {10, 0}, {10, 4}, {7, 4}, {4, 4}, {0, 4}}));
	// A diagonal edge cuts only at its lattice points: (0, 0)-(9, 6) has them every (3, 2).
	const std::vector<Vec2i64> diagonal = splitLongEdges({{0, 0}, {9, 6}, {0, 6}}, 4);
	EXPECT_EQ(diagonal, (std::vector<Vec2i64>{{0, 0}, {3, 2}, {6, 4}, {9, 6}, {6, 6}, {3, 6}, {0, 6}, {0, 3}}));
	Harness h(4);
	const RingId bounds = h.add(NavInputPolygon{rect(0, 0, 10, 4), false, kBorder});
	EXPECT_EQ(h.mesh.ring(bounds).polygon.ring.size(), 8U);
	EXPECT_TRUE(matchesFresh(h.mesh, h.model));
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
	sizes["border"].freed.push_back(h.last.freedTriangles.size());
	sizes["border"].created.push_back(h.last.newTriangles.size());
	sizes["border"].passes.push_back(h.last.passes);
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


// --- Queries: doors, widths, paths, and reachability against buildNavMesh ---------

namespace {

	// Rooms behind wall bands with door spans, door portals on the spans' faces, and
	// trees scattered over walls and floor alike.
	struct WallsAndDoors {
		std::vector<NavInputPolygon> base;
		std::vector<NavInputPolygon> rings; // walls, door spans, trees
		std::vector<DoorPortal>		 doors;
	};

	WallsAndDoors wallsAndDoors() {
		WallsAndDoors w;
		w.base.push_back(bounds(rect(0, 0, 20000, 20000)));
		auto wall = [&](std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1, std::int64_t segment) {
			w.rings.push_back(NavInputPolygon{rect(x0, y0, x1, y1), true, segment});
		};
		auto door = [&](std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1, std::int64_t segment, std::int64_t opening,
						std::int64_t clearWidth) {
			w.rings.push_back(NavInputPolygon{rect(x0, y0, x1, y1), true, segment, opening});
			DoorPortal portal;
			portal.openingId	= opening;
			portal.a			= {x0, y0};
			portal.b			= x1 - x0 > y1 - y0 ? Vec2i64{x1, y0} : Vec2i64{x0, y1};
			portal.clearWidthMm = clearWidth;
			w.doors.push_back(portal);
		};
		wall(2000, 10000, 7000, 10200, 11);
		door(7000, 10000, 8000, 10200, 11, 1, 900);
		wall(8000, 10000, 12000, 10200, 11);
		door(12000, 10000, 12500, 10200, 11, 2, 450);
		wall(12500, 10000, 18000, 10200, 11);
		wall(10000, 2000, 10200, 5000, 12);
		door(10000, 5000, 10200, 6000, 12, 3, 900);
		wall(10000, 6000, 10200, 10000, 12);
		std::mt19937 rng(17);
		for (int i = 0; i < 120; ++i) {
			const std::int64_t x = 500 + static_cast<std::int64_t>(rng() % 19000);
			const std::int64_t y = 500 + static_cast<std::int64_t>(rng() % 19000);
			w.rings.push_back(blocked({{x, y}, {x + 400, y + 60}, {x + 350, y + 420}, {x - 40, y + 380}}));
		}
		return w;
	}

	WallsAndDoors fixtureRings(const Fixture& f) {
		return {f.base, f.trees, {}};
	}

	// A mesh that reached `rings` through a random sequence: batches in, some taken out
	// and put back, doors added early and some removed and re-added. Forests rebuilt.
	struct Reached {
		Harness				h;
		std::vector<DoorId> doorIds;
	};

	void reach(Reached& r, const WallsAndDoors& input, std::uint32_t seed) {
		std::mt19937 rng(seed);
		{
			PatchEdits edits;
			edits.adds = input.base;
			r.h.apply(edits);
		}
		for (const DoorPortal& d : input.doors) {
			r.doorIds.push_back(r.h.mesh.addDoor(d));
		}
		std::vector<NavInputPolygon> pending = input.rings;
		std::shuffle(pending.begin(), pending.end(), rng);
		std::vector<RingId> placed;
		while (!pending.empty()) {
			PatchEdits		  edits;
			const std::size_t count = std::min<std::size_t>(pending.size(), 1 + pick(rng, 48));
			edits.adds.assign(pending.end() - static_cast<std::ptrdiff_t>(count), pending.end());
			pending.resize(pending.size() - count);
			if (placed.size() > 10 && pick(rng, 3) == 0) {
				const std::size_t at = pick(rng, placed.size());
				pending.insert(pending.begin(), r.h.model[placed[at]]);
				edits.removes.push_back(placed[at]);
				placed.erase(placed.begin() + static_cast<std::ptrdiff_t>(at));
			}
			const PatchReport report = r.h.apply(edits);
			placed.insert(placed.end(), report.added.begin(), report.added.end());
			if (!r.doorIds.empty() && pick(rng, 4) == 0) {
				const std::size_t at = pick(rng, r.doorIds.size());
				r.h.mesh.removeDoor(r.doorIds[at]);
				r.doorIds[at] = r.h.mesh.addDoor(input.doors[at]);
			}
		}
		r.h.mesh.rebuildForests();
	}

	NavMeshInput freshInput(const Reached& r, const WallsAndDoors& input) {
		NavMeshInput in;
		for (const auto& [id, poly] : r.h.model) {
			in.polygons.push_back(poly);
		}
		std::vector<std::pair<DoorId, DoorPortal>> doors;
		for (std::size_t i = 0; i < r.doorIds.size(); ++i) {
			doors.push_back({r.doorIds[i], input.doors[i]});
		}
		std::sort(doors.begin(), doors.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		for (const auto& [id, door] : doors) {
			in.doors.push_back(door);
		}
		return in;
	}

	// Floor points from a fixed sequence, the same in both meshes' frames.
	std::vector<Vec2i64> samplePoints(const NavInputPolygon& border, std::size_t n, std::uint32_t seed) {
		Vec2i64 lo = border.ring.front();
		Vec2i64 hi = lo;
		for (const Vec2i64& p : border.ring) {
			lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
			hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
		}
		std::mt19937		 rng(seed);
		std::vector<Vec2i64> out;
		for (std::size_t i = 0; i < n; ++i) {
			out.push_back({lo.x + 1 + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(hi.x - lo.x - 2)),
						   lo.y + 1 + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(hi.y - lo.y - 2))});
		}
		return out;
	}

	std::int64_t pathLengthMm(const PathResult& p) {
		double length = 0;
		for (std::size_t i = 1; i < p.points.size(); ++i) {
			const double dx = static_cast<double>(p.points[i].x - p.points[i - 1].x);
			const double dy = static_cast<double>(p.points[i].y - p.points[i - 1].y);
			length += std::sqrt(dx * dx + dy * dy);
		}
		return std::llround(length);
	}

	// Door tags on every edge both meshes have, and every patched width equal to the
	// uncapped width read through the cap.
	::testing::AssertionResult tagsAndWidthsMatch(const PatchableMesh& mesh, const NavMesh& fresh, std::int64_t maxClearanceMm) {
		std::map<EdgeKey, std::pair<std::int64_t, std::int64_t>> freshTags;
		for (const NavTriangle& t : fresh.triangles) {
			for (int e = 0; e < 3; ++e) {
				freshTags[edgeKey(fresh.vertices[t.v[static_cast<std::size_t>(e)]], fresh.vertices[t.v[static_cast<std::size_t>((e + 1) % 3)]])] = {
					t.edgeOpening[static_cast<std::size_t>(e)], t.edgeClearWidthMm[static_cast<std::size_t>(e)]};
			}
		}
		const NavView view = mesh.view();
		WidthSearch	  search;
		int			  sharedDoorEdges = 0;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (!mesh.triangleLive(t)) {
				continue;
			}
			const NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
			for (int e = 0; e < 3; ++e) {
				auto it = freshTags.find(edgeKey(mesh.vertices()[tri.v[static_cast<std::size_t>(e)]], mesh.vertices()[tri.v[static_cast<std::size_t>((e + 1) % 3)]]));
				if (it != freshTags.end()) {
					if (it->second != std::make_pair(tri.edgeOpening[static_cast<std::size_t>(e)], tri.edgeClearWidthMm[static_cast<std::size_t>(e)])) {
						return ::testing::AssertionFailure() << "door tags differ on an edge of triangle " << t;
					}
					sharedDoorEdges += tri.edgeOpening[static_cast<std::size_t>(e)] != kNoOpening ? 1 : 0;
				}
				const std::int64_t uncapped = corridorWidthMm(view, search, t, e, kUnconstrainedWidth);
				const std::int64_t expected = uncapped < maxClearanceMm ? uncapped : kUnconstrainedWidth;
				if (tri.edgePairWidthMm[static_cast<std::size_t>(e)] != expected) {
					return ::testing::AssertionFailure() << "triangle " << t << " apex " << e << " width " << tri.edgePairWidthMm[static_cast<std::size_t>(e)]
														 << ", uncapped " << uncapped;
				}
			}
		}
		return ::testing::AssertionSuccess() << sharedDoorEdges << " door half-edges";
	}

	constexpr std::array<std::int64_t, 4> kRadii = {0, 300, 600, 1500};

	// Same verdict and the same length to the millimetre for every pair and radius, the
	// patched mesh through its view against buildNavMesh of the same rings and doors;
	// and the forests' reach verdicts agree, in truth and for an optimistic belief.
	void expectQueriesMatch(const Reached& r, const NavMesh& fresh, const NavInputPolygon& border, std::uint32_t seed) {
		const NavView				  view	 = r.h.mesh.view();
		const std::vector<Vec2i64>	  points = samplePoints(border, 96, seed);
		const std::unordered_set<std::uint64_t> noneKnown;
		const BeliefFilter			  optimistic{&noneKnown, &noneKnown};
		int							  lengths = 0;
		int							  blocked = 0;
		for (std::size_t i = 0; i + 1 < points.size(); i += 2) {
			const Vec2i64& a = points[i];
			const Vec2i64& b = points[i + 1];
			for (const std::int64_t radius : kRadii) {
				const PathResult patched = pathThrough(view, a, b, radius);
				const PathResult built	 = pathThrough(fresh, a, b, radius);
				EXPECT_EQ(patched.reachable, built.reachable) << "pair " << i << " radius " << radius;
				blocked += built.reachable ? 0 : 1;
				if (patched.reachable && built.reachable) {
					++lengths;
					EXPECT_EQ(pathLengthMm(patched), pathLengthMm(built)) << "pair " << i << " radius " << radius << " from (" << a.x << ", " << a.y
																		 << ") to (" << b.x << ", " << b.y << ")";
				}
				for (const BeliefFilter& belief : {BeliefFilter{}, optimistic}) {
					EXPECT_EQ(reaches(view, reachFrom(view, a, radius, belief), b, belief), reaches(fresh, reachFrom(fresh, a, radius, belief), b, belief))
						<< "reach verdict, pair " << i << " radius " << radius;
				}
			}
		}
		// The pairs exercise both answers: real paths compared, and some refused.
		EXPECT_GT(lengths, 40);
		EXPECT_GT(blocked, 0);
		::testing::Test::RecordProperty("pathsCompared", lengths);
	}

} // namespace

TEST(PatchableMesh, WallsAndDoorsQueriesMatchBuildNavMesh) {
	const WallsAndDoors input = wallsAndDoors();
	for (const std::uint32_t seed : {3U, 4U}) {
		Reached r;
		reach(r, input, seed);
		ASSERT_TRUE(matchesFresh(r.h.mesh, r.h.model));
		const NavMesh fresh = buildNavMesh(freshInput(r, input));
		const auto	  tags	= tagsAndWidthsMatch(r.h.mesh, fresh, kDefaultMaxClearanceMm);
		ASSERT_TRUE(tags);
		EXPECT_NE(std::string(tags.message()), "0 door half-edges");
		expectQueriesMatch(r, fresh, input.base.front(), seed);
	}
}

TEST(PatchableMesh, FixtureQueriesMatchBuildNavMesh) {
	const std::array<Fixture, 3> fixtures = {dumpFixture(testdata::manyTreesDump()), dumpFixture(testdata::controlDump()), realRingsFixture()};
	for (std::size_t f = 0; f < fixtures.size(); ++f) {
		const WallsAndDoors input = fixtureRings(fixtures[f]);
		Reached				r;
		reach(r, input, static_cast<std::uint32_t>(50 + f));
		const NavMesh fresh = buildNavMesh(freshInput(r, input));
		ASSERT_TRUE(tagsAndWidthsMatch(r.h.mesh, fresh, kDefaultMaxClearanceMm)) << "fixture " << f;
		expectQueriesMatch(r, fresh, input.base.front(), static_cast<std::uint32_t>(f));
	}
}

namespace {

	// realRings tiled k x k, under one bounds square over the whole tiling or under k x k
	// adjacent squares, one per tile; bounds sides split to 16 m, as in the game.
	struct TiledGround {
		NavInputPolygon				 border; // the whole tiling's outline
		std::vector<NavInputPolygon> squares;
		std::vector<NavInputPolygon> rings;
	};

	TiledGround tiledRealRings(std::int64_t k) {
		const Fixture tile = realRingsFixture();
		Vec2i64		  lo   = tile.base.front().ring.front();
		Vec2i64		  hi   = lo;
		for (const Vec2i64& p : tile.base.front().ring) {
			lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
			hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
		}
		const std::int64_t side = hi.x - lo.x;
		TiledGround		   g;
		g.border = bounds(rect(lo.x, lo.y, lo.x + k * side, lo.y + k * side));
		for (std::int64_t j = 0; j < k; ++j) {
			for (std::int64_t i = 0; i < k; ++i) {
				const Vec2i64 offset{i * side, j * side};
				g.squares.push_back(bounds(rect(lo.x + offset.x, lo.y + offset.y, lo.x + offset.x + side, lo.y + offset.y + side)));
				for (std::size_t b = 1; b < tile.base.size(); ++b) {
					g.rings.push_back(tile.base[b]);
					g.rings.back().ring = translated(g.rings.back().ring, offset);
				}
				for (const NavInputPolygon& tree : tile.trees) {
					g.rings.push_back(tree);
					g.rings.back().ring = translated(g.rings.back().ring, offset);
				}
			}
		}
		return g;
	}

	void loadGround(PatchableMesh& mesh, const std::vector<NavInputPolygon>& bounds, const std::vector<NavInputPolygon>& rings) {
		PatchEdits edits;
		edits.adds = bounds;
		edits.adds.insert(edits.adds.end(), rings.begin(), rings.end());
		mesh.applyPatch(edits);
		mesh.refreshWidths();
		mesh.rebuildForests();
	}

	// The widest disc diameter some path between the truth-forest triangles holding a
	// and b admits; -1 when either is no node or they are apart.
	std::int64_t bottleneckMm(const NavView& view, const Vec2i64& a, const Vec2i64& b) {
		const ReachabilityForest& forest = *view.forest(true);
		const std::int32_t		  ta	 = view.locate(a);
		const std::int32_t		  tb	 = view.locate(b);
		if (ta < 0 || tb < 0 || forest.component[static_cast<std::size_t>(ta)] < 0 ||
			forest.component[static_cast<std::size_t>(ta)] != forest.component[static_cast<std::size_t>(tb)]) {
			return -1;
		}
		std::int32_t node = ta;
		while (!inForestSubtree(forest, node, tb)) {
			node = forest.up[0][static_cast<std::size_t>(node)];
		}
		return forest.nodeCap[static_cast<std::size_t>(node)];
	}

	// How far apart two paths between the same points may be in length when only the
	// triangulation differs (seams, split vertices, free diagonals): a fraction of the
	// length, for corridors the search keeps or drops by the per-state label cap, plus
	// two radii, for a disc's clearance at a corner being set along whichever portal
	// ends there.
	constexpr double kPathLengthToleranceFraction = 0.02;
	constexpr double kPathLengthToleranceRadii	  = 2.0;
	// From this radius on the corridor widths that gate a disc, read per triangle, can
	// shut a gap in one triangulation that the other leaves open, sending the disc one
	// obstacle over: a different route, bounded only by this fraction of the length.
	constexpr std::int64_t kWideDiscRadiusMm		  = 1000;
	constexpr double	   kWideDiscToleranceFraction = 0.15;

	bool lengthsAgree(std::int64_t a, std::int64_t b, std::int64_t radius) {
		const double longer	   = static_cast<double>(std::max(a, b));
		const double tolerance = radius >= kWideDiscRadiusMm ? kWideDiscToleranceFraction * longer
															 : kPathLengthToleranceFraction * longer + kPathLengthToleranceRadii * static_cast<double>(radius);
		return static_cast<double>(std::abs(a - b)) <= tolerance;
	}

	using Corners = std::array<Vec2i64, 3>;

	Corners cornersOf(const PatchableMesh& mesh, std::int32_t t) {
		const NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
		Corners			   c{mesh.vertices()[tri.v[0]], mesh.vertices()[tri.v[1]], mesh.vertices()[tri.v[2]]};
		std::sort(c.begin(), c.end());
		return c;
	}

	// Trees priced at kOraclePrice: the game's flora, cheap enough that some walks go
	// through and some round.
	constexpr double   kOraclePrice		= 1.5;
	constexpr TagPrice kOraclePrices[] = {{kTree, kOraclePrice}};
	// Priced walks are compared for the movers' radii: a priced search skips the forest
	// pre-check, so each pair it cannot reach costs a search of the whole mesh.
	constexpr std::int64_t kOracleMaxPricedRadiusMm = 300;

	// Internal bounds edges must not change an answer: the same rings under one bounds
	// square and under a grid of adjacent squares give the same path verdicts, reach
	// verdicts, and bottlenecks, path lengths that agree (lengthsAgree), and the same
	// corridor width at every apex of a triangle both meshes have. With the trees priced,
	// the same verdicts, costs that agree as lengths do (a radius of slack weighed at the
	// price). A walk through and a way round whose costs agree that closely are a tie the
	// tilings may settle either way, so whether a walk crosses priced faces is counted,
	// not required to match.
	void expectBoundsSplitInvisible(std::int64_t k, std::uint32_t seed) {
		const TiledGround g = tiledRealRings(k);
		PatchableMesh	  whole({.maxBoundsEdgeMm = 16000, .detailTags = {kTree}});
		PatchableMesh	  split({.maxBoundsEdgeMm = 16000, .detailTags = {kTree}});
		loadGround(whole, {g.border}, g.rings);
		loadGround(split, g.squares, g.rings);
		ASSERT_TRUE(whole.validate().empty()) << whole.validate();
		ASSERT_TRUE(split.validate().empty()) << split.validate();

		const NavView						   wv = whole.view();
		const NavView						   sv = split.view();
		const std::vector<Vec2i64>			   points = samplePoints(g.border, 128, seed);
		const std::unordered_set<std::uint64_t> noneKnown;
		const BeliefFilter					   optimistic{&noneKnown, &noneKnown};
		int									   lengths		  = 0;
		int									   lengthDiffers  = 0;
		int									   lengthsApart	  = 0;
		std::int64_t						   worstDeltaMm	  = 0;
		int									   verdictDiffers = 0;
		int									   reachDiffers	  = 0;
		int									   bottleneckDiffers = 0;
		int									   pricedCompared	 = 0;
		int									   pricedThrough	 = 0;
		int									   flagsDiffer		 = 0;
		int									   boxedCompared	 = 0;
		int									   boxBinds		 = 0;
		int									   boxShuts		 = 0;
		int									   hullCompared	 = 0;
		BeliefFilter						   priced;
		priced.prices = kOraclePrices;
		for (std::size_t i = 0; i + 1 < points.size(); i += 2) {
			const Vec2i64& a = points[i];
			const Vec2i64& b = points[i + 1];
			if (bottleneckMm(wv, a, b) != bottleneckMm(sv, a, b)) {
				++bottleneckDiffers;
			}
			for (const std::int64_t radius : kRadii) {
				const PathResult w = pathThrough(wv, a, b, radius);
				const PathResult s = pathThrough(sv, a, b, radius);
				if (w.reachable != s.reachable) {
					++verdictDiffers;
				} else if (w.reachable) {
					++lengths;
					const std::int64_t delta = pathLengthMm(s) - pathLengthMm(w);
					if (delta != 0) {
						++lengthDiffers;
						worstDeltaMm = std::abs(delta) > std::abs(worstDeltaMm) ? delta : worstDeltaMm;
					}
					if (!lengthsAgree(pathLengthMm(s), pathLengthMm(w), radius)) {
						++lengthsApart;
						ADD_FAILURE() << "radius " << radius << " from (" << a.x << ", " << a.y << ") to (" << b.x << ", " << b.y
									  << "): whole " << pathLengthMm(w) << " mm, split " << pathLengthMm(s) << " mm";
					}
				}
				for (const BeliefFilter& belief : {BeliefFilter{}, optimistic}) {
					if (reaches(wv, reachFrom(wv, a, radius, belief), b, belief) != reaches(sv, reachFrom(sv, a, radius, belief), b, belief)) {
						++reachDiffers;
					}
				}
				if (radius > kOracleMaxPricedRadiusMm) {
					continue;
				}
				// Confined to a box round the pair, a little past it, whose sides cut the
				// whole mesh's big triangles where the split mesh has its square sides: the
				// same verdict, lengths that agree, and every point inside the box.
				const std::int64_t margin = 2000 + static_cast<std::int64_t>((i / 2) % 4) * 4567;
				const geometry::RectMm box{{std::min(a.x, b.x) - margin, std::min(a.y, b.y) - margin}, {std::max(a.x, b.x) + margin, std::max(a.y, b.y) + margin}};
				const PathResult   bw = pathThrough(wv, a, b, radius, {}, {.within = &box});
				const PathResult   bs = pathThrough(sv, a, b, radius, {}, {.within = &box});
				EXPECT_EQ(bw.stop, bs.stop) << "boxed, radius " << radius << " from (" << a.x << ", " << a.y << ") to (" << b.x << ", " << b.y << ")";
				for (const PathResult* r : {&bw, &bs}) {
					for (const Vec2i64& p : r->points) {
						EXPECT_TRUE(p.x >= box.min.x && p.x <= box.max.x && p.y >= box.min.y && p.y <= box.max.y)
							<< "(" << p.x << ", " << p.y << ") outside the box, radius " << radius;
					}
				}
				if (bw.reachable && bs.reachable) {
					++boxedCompared;
					boxBinds += pathLengthMm(bw) != pathLengthMm(w) ? 1 : 0;
					EXPECT_TRUE(lengthsAgree(pathLengthMm(bs), pathLengthMm(bw), radius))
						<< "boxed, radius " << radius << " from (" << a.x << ", " << a.y << ") to (" << b.x << ", " << b.y << "): whole "
						<< pathLengthMm(bw) << " mm, split " << pathLengthMm(bs) << " mm";
				} else {
					boxShuts += w.reachable ? 1 : 0;
				}
				// A box just round the walk found without one shuts nothing that walk needs. Its
				// margin is the radius: each tiling rounds a corner along its own portals, a
				// little inside the radius either way (PathQuery's clearance caveat).
				if (w.reachable) {
					const std::int64_t grow = radius + 1;
					geometry::RectMm   hull{w.points.front(), w.points.front()};
					for (const Vec2i64& p : w.points) {
						hull.min = {std::min(hull.min.x, p.x - grow), std::min(hull.min.y, p.y - grow)};
						hull.max = {std::max(hull.max.x, p.x + grow), std::max(hull.max.y, p.y + grow)};
					}
					const PathResult hw = pathThrough(wv, a, b, radius, {}, {.within = &hull});
					const PathResult hs = pathThrough(sv, a, b, radius, {}, {.within = &hull});
					EXPECT_EQ(hw.stop, PathStop::Found) << "hull-boxed, radius " << radius << " from (" << a.x << ", " << a.y << ") to (" << b.x << ", " << b.y << ")";
					EXPECT_EQ(hs.stop, PathStop::Found) << "hull-boxed, radius " << radius << " from (" << a.x << ", " << a.y << ") to (" << b.x << ", " << b.y << ")";
					EXPECT_TRUE(lengthsAgree(pathLengthMm(hw), pathLengthMm(w), radius));
					EXPECT_TRUE(lengthsAgree(pathLengthMm(hs), pathLengthMm(w), radius));
					++hullCompared;
				}
				const PathResult pw = pathThrough(wv, a, b, radius, priced);
				const PathResult ps = pathThrough(sv, a, b, radius, priced);
				EXPECT_EQ(pw.reachable, ps.reachable) << "priced, radius " << radius << " from (" << a.x << ", " << a.y << ") to (" << b.x << ", " << b.y << ")";
				if (pw.reachable && ps.reachable) {
					++pricedCompared;
					pricedThrough += pw.crossesPricedFaces ? 1 : 0;
					const double longer	   = std::max(pw.cost, ps.cost);
					const double tolerance = radius >= kWideDiscRadiusMm ? kWideDiscToleranceFraction * longer
																		 : kPathLengthToleranceFraction * longer +
																			   kPathLengthToleranceRadii * kOraclePrice * static_cast<double>(radius);
					flagsDiffer += pw.crossesPricedFaces != ps.crossesPricedFaces ? 1 : 0;
					EXPECT_NEAR(pw.cost, ps.cost, tolerance) << "priced, radius " << radius << " from (" << a.x << ", " << a.y << ") to (" << b.x << ", " << b.y << ")";
				}
			}
		}
		EXPECT_GT(lengths, 100);
		EXPECT_EQ(lengthsApart, 0);
		::testing::Test::RecordProperty("pathLengthsDiffering", lengthDiffers);
		::testing::Test::RecordProperty("worstSplitMinusWholeMm", std::to_string(worstDeltaMm));
		EXPECT_EQ(verdictDiffers, 0);
		EXPECT_EQ(reachDiffers, 0);
		EXPECT_EQ(bottleneckDiffers, 0);
		EXPECT_GT(pricedCompared, 60);
		EXPECT_GT(pricedThrough, 5) << "some priced walks must go through trees for the comparison to bind";
		EXPECT_GT(boxedCompared, 30);
		EXPECT_GT(hullCompared, 60);
		EXPECT_GT(boxBinds + boxShuts, 5) << "some boxes must change an answer for the comparison to bind";
		::testing::Test::RecordProperty("boxedCompared", boxedCompared);
		::testing::Test::RecordProperty("boxBinds", boxBinds);
		::testing::Test::RecordProperty("boxShuts", boxShuts);
		::testing::Test::RecordProperty("hullCompared", hullCompared);
		::testing::Test::RecordProperty("pricedFlagsDiffering", flagsDiffer);
		::testing::Test::RecordProperty("pricedCompared", pricedCompared);
		::testing::Test::RecordProperty("pricedThrough", pricedThrough);

		std::map<Corners, std::int32_t> wholeTriangles;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(whole.triangles().size()); ++t) {
			if (whole.triangleLive(t)) {
				wholeTriangles.emplace(cornersOf(whole, t), t);
			}
		}
		int shared		  = 0;
		int widthsDiffer = 0;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(split.triangles().size()); ++t) {
			if (!split.triangleLive(t)) {
				continue;
			}
			auto it = wholeTriangles.find(cornersOf(split, t));
			if (it == wholeTriangles.end()) {
				continue;
			}
			++shared;
			const NavTriangle& st = split.triangles()[static_cast<std::size_t>(t)];
			const NavTriangle& wt = whole.triangles()[static_cast<std::size_t>(it->second)];
			for (int e = 0; e < 3; ++e) {
				const Vec2i64& apex = split.vertices()[st.v[static_cast<std::size_t>(e)]];
				for (int f = 0; f < 3; ++f) {
					if (whole.vertices()[wt.v[static_cast<std::size_t>(f)]] == apex &&
						(wt.edgePairWidthMm[static_cast<std::size_t>(f)] != st.edgePairWidthMm[static_cast<std::size_t>(e)] ||
						 wt.structuralWidthMm[static_cast<std::size_t>(f)] != st.structuralWidthMm[static_cast<std::size_t>(e)])) {
						++widthsDiffer;
					}
				}
			}
		}
		EXPECT_GT(shared, 1000);
		EXPECT_EQ(widthsDiffer, 0) << "of " << shared << " shared triangles";
		::testing::Test::RecordProperty("pathsCompared", lengths);
	}

} // namespace

TEST(PatchableMesh, BoundsSplitTwoByTwoChangesNoAnswer) {
	expectBoundsSplitInvisible(2, 61);
}

// A box that binds, on two tilings of one 128 m field: a wall whose near end lies outside
// the box, so the only way round inside it passes the far end, 4 m from the box's side at
// x = 64 m. That side cuts the one-square tiling's big triangles and runs along the 2x2
// tiling's square sides. Both answer Found with lengths that agree and every point inside;
// without the box the walk leaves it. A goal or start on that side, which lies on the 2x2
// tiling's mesh edges, stands in the triangle inside the box.
TEST(PatchableMesh, BoxedWalksRoundNearTheSideAgreeAcrossTilings) {
	const NavInputPolygon wall = blocked(rect(68000, 60000, 118000, 60500), kWater);
	PatchableMesh		  whole;
	PatchableMesh		  split;
	loadGround(whole, {bounds(rect(0, 0, 128000, 128000))}, {wall});
	loadGround(split,
			   {bounds(rect(0, 0, 64000, 64000)), bounds(rect(64000, 0, 128000, 64000)), bounds(rect(0, 64000, 64000, 128000)),
				bounds(rect(64000, 64000, 128000, 128000))},
			   {wall});
	const geometry::RectMm box{{64000, 30000}, {114000, 100000}};
	auto				   inside = [&](const PathResult& r) {
		  return std::all_of(r.points.begin(), r.points.end(), [&](const Vec2i64& p) {
			  return p.x >= box.min.x && p.x <= box.max.x && p.y >= box.min.y && p.y <= box.max.y;
		  });
	};
	// Point location settles the side by sorted corners, so it names the triangle outside.
	ASSERT_LE(cornersOf(split, split.locate({64000, 70000}))[2].x, 64000);
	std::mt19937										rng(83);
	std::vector<std::pair<Vec2i64, Vec2i64>> pairs{{{64000, 50000}, {64000, 70000}}, {{98000, 70000}, {64000, 70000}}, {{64000, 45000}, {90000, 80000}}};
	while (pairs.size() < 11) {
		pairs.push_back({{80000 + static_cast<std::int64_t>(rng() % 32000), 32000 + static_cast<std::int64_t>(rng() % 26000)},
						 {66000 + static_cast<std::int64_t>(rng() % 46000), 63000 + static_cast<std::int64_t>(rng() % 35000)}});
	}
	int leaves = 0;
	for (const auto& [a, b] : pairs) {
		for (const std::int64_t radius : {0, 300}) {
			const PathResult open = pathThrough(whole.view(), a, b, radius);
			leaves += open.reachable && !inside(open) ? 1 : 0;
			const PathResult bw = pathThrough(whole.view(), a, b, radius, {}, {.within = &box});
			const PathResult bs = pathThrough(split.view(), a, b, radius, {}, {.within = &box});
			const std::string at = "radius " + std::to_string(radius) + " from (" + std::to_string(a.x) + ", " + std::to_string(a.y) + ") to (" +
								   std::to_string(b.x) + ", " + std::to_string(b.y) + ")";
			ASSERT_EQ(bw.stop, PathStop::Found) << at;
			ASSERT_EQ(bs.stop, PathStop::Found) << at;
			EXPECT_TRUE(inside(bw)) << at;
			EXPECT_TRUE(inside(bs)) << at;
			EXPECT_TRUE(lengthsAgree(pathLengthMm(bw), pathLengthMm(bs), radius)) << at << ": whole " << pathLengthMm(bw) << " mm, split " << pathLengthMm(bs) << " mm";
		}
	}
	EXPECT_GE(leaves, 10) << "the box must bind: most walks without it go round the wall's near end";
}

namespace {

	// Square posts 1 m across, `pitch` apart, nudged by up to `jitter` mm. Unnudged, each
	// post's corners are cocircular with its neighbours', so the ground between them has
	// diagonals a patch may choose freely; nudged, the triangulation is unique.
	std::vector<NavInputPolygon> posts(std::int64_t pitch, std::int64_t jitter, std::uint32_t seed) {
		std::mt19937				 rng(seed);
		std::vector<NavInputPolygon> out;
		for (std::int64_t j = 0; j < 6; ++j) {
			for (std::int64_t i = 0; i < 6; ++i) {
				const std::int64_t x = 2000 + i * pitch + (jitter > 0 ? between(rng, 0, jitter) : 0);
				const std::int64_t y = 2000 + j * pitch + (jitter > 0 ? between(rng, 0, jitter) : 0);
				out.push_back(blocked(rect(x, y, x + 1000, y + 1000)));
			}
		}
		return out;
	}

	struct HistoryComparison {
		int otherTriangles = 0; // triangles only one history has
		int compared	   = 0;
		int identical	   = 0;
	};

	// The same final rings reached two ways (all at once; in shuffled batches with
	// removals and re-adds, around scaffolding that comes and goes so slots are reused
	// differently), then the same pairs pathed in both. Verdicts must agree, and paths
	// that differ must agree in length (lengthsAgree).
	HistoryComparison compareHistories(const std::vector<NavInputPolygon>& rings, std::int64_t side, std::uint32_t seed) {
		const NavInputPolygon border = bounds(rect(0, 0, side, side));
		Harness				  direct;
		{
			PatchEdits edits;
			edits.adds.push_back(border);
			edits.adds.insert(edits.adds.end(), rings.begin(), rings.end());
			direct.apply(edits);
		}

		Harness		 winding;
		std::mt19937 rng(seed);
		{
			PatchEdits edits;
			edits.adds.push_back(border);
			winding.apply(edits);
		}
		std::vector<RingId> scaffold;
		for (int i = 0; i < 12; ++i) {
			const std::int64_t x = 1000 + between(rng, 0, side - 2000);
			const std::int64_t y = 1000 + between(rng, 0, side - 2000);
			scaffold.push_back(winding.add(blocked(rect(x, y, x + 300, y + 300))));
		}
		std::vector<NavInputPolygon> pending = rings;
		std::shuffle(pending.begin(), pending.end(), rng);
		std::vector<RingId> placed;
		while (!pending.empty()) {
			PatchEdits		  edits;
			const std::size_t count = std::min<std::size_t>(pending.size(), 1 + pick(rng, 7));
			edits.adds.assign(pending.end() - static_cast<std::ptrdiff_t>(count), pending.end());
			pending.resize(pending.size() - count);
			if (!scaffold.empty()) {
				edits.removes.push_back(scaffold.back());
				scaffold.pop_back();
			}
			if (placed.size() > 4 && pick(rng, 2) == 0) {
				const std::size_t at = pick(rng, placed.size());
				pending.insert(pending.begin(), winding.model[placed[at]]);
				edits.removes.push_back(placed[at]);
				placed.erase(placed.begin() + static_cast<std::ptrdiff_t>(at));
			}
			const PatchReport report = winding.apply(edits);
			placed.insert(placed.end(), report.added.begin(), report.added.end());
		}
		while (!scaffold.empty()) {
			PatchEdits edits;
			edits.removes.push_back(scaffold.back());
			winding.apply(edits);
			scaffold.pop_back();
		}
		EXPECT_TRUE(matchesFresh(direct.mesh, direct.model));
		EXPECT_TRUE(matchesFresh(winding.mesh, winding.model));

		int								   sameSlots = 0;
		std::set<Corners>				   directTriangles;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(direct.mesh.triangles().size()); ++t) {
			if (!direct.mesh.triangleLive(t)) {
				continue;
			}
			directTriangles.insert(cornersOf(direct.mesh, t));
			sameSlots += static_cast<std::size_t>(t) < winding.mesh.triangles().size() && winding.mesh.triangleLive(t) &&
								 cornersOf(direct.mesh, t) == cornersOf(winding.mesh, t)
							 ? 1
							 : 0;
		}
		EXPECT_LT(sameSlots, static_cast<int>(directTriangles.size())) << "the histories must leave triangles in different slots";
		HistoryComparison out;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(winding.mesh.triangles().size()); ++t) {
			if (winding.mesh.triangleLive(t) && directTriangles.count(cornersOf(winding.mesh, t)) == 0) {
				++out.otherTriangles;
			}
		}

		const std::vector<Vec2i64> points = samplePoints(border, 160, seed + 1);
		for (std::size_t i = 0; i + 1 < points.size(); i += 2) {
			for (const std::int64_t radius : kRadii) {
				const PathResult d = pathThrough(direct.mesh.view(), points[i], points[i + 1], radius);
				const PathResult w = pathThrough(winding.mesh.view(), points[i], points[i + 1], radius);
				EXPECT_EQ(d.reachable, w.reachable) << "pair " << i << " radius " << radius;
				if (!d.reachable || !w.reachable) {
					continue;
				}
				++out.compared;
				out.identical += d.points == w.points ? 1 : 0;
				EXPECT_TRUE(lengthsAgree(pathLengthMm(d), pathLengthMm(w), radius))
					<< "pair " << i << " radius " << radius << ": " << pathLengthMm(d) << " vs " << pathLengthMm(w) << " mm";
			}
		}
		EXPECT_GT(out.compared, 200);
		return out;
	}

} // namespace

// Point location and search ties go by coordinates, and corridor scores by geometry, so
// on one triangulation the edit history changes no path at all.
TEST(PatchableMesh, PathsIgnoreEditHistory) {
	const HistoryComparison r = compareHistories(posts(4000, 600, 5), 26000, 77);
	EXPECT_EQ(r.otherTriangles, 0) << "nudged posts leave the triangulation no choice";
	EXPECT_EQ(r.identical, r.compared);
}

// Where the history picks different free diagonals, paths may differ, within the
// tolerance the bounds-split oracle uses.
// A start or goal exactly on a tree's edge or corner stands on the floor beside it:
// point location settles a shared edge by geometry, which may name the tree's face, and
// the query looks past it to a face it can cross.
TEST(PatchableMesh, EndpointsOnATreeEdgeStandOnTheFloor) {
	PatchableMesh mesh;
	PatchEdits	  edits;
	edits.adds.push_back(bounds(rect(0, 0, 4000, 4000)));
	edits.adds.push_back(blocked(rect(1000, 1000, 2000, 2000)));
	mesh.applyPatch(edits);
	mesh.refreshWidths();
	mesh.rebuildForests();
	const NavView view		  = mesh.view();
	int			  onTreeFace = 0;
	for (const Vec2i64& p : {Vec2i64{1500, 1000}, Vec2i64{2000, 1500}, Vec2i64{1500, 2000}, Vec2i64{1000, 1500}, Vec2i64{1000, 1000},
							 Vec2i64{2000, 2000}, Vec2i64{2000, 1000}, Vec2i64{1000, 2000}}) {
		onTreeFace += isFloorFace(mesh.triangles()[static_cast<std::size_t>(mesh.locate(p))]) ? 0 : 1;
		EXPECT_TRUE(pathThrough(view, p, {3500, 3500}, 0).reachable) << "from (" << p.x << ", " << p.y << ")";
		EXPECT_TRUE(pathThrough(view, {3500, 3500}, p, 0).reachable) << "to (" << p.x << ", " << p.y << ")";
		EXPECT_TRUE(reaches(view, reachFrom(view, p, 0), {3500, 3500})) << "reach from (" << p.x << ", " << p.y << ")";
	}
	EXPECT_GT(onTreeFace, 0) << "some of these points must locate to the tree's face for the test to bind";
}

TEST(PatchableMesh, PathsAcrossFreeDiagonalsAgree) {
	const HistoryComparison r = compareHistories(posts(5000, 0, 0), 31000, 79);
	EXPECT_GT(r.otherTriangles, 0) << "the lattice must leave diagonals to choose";
	::testing::Test::RecordProperty("trianglesOnlyInOneHistory", r.otherTriangles);
	::testing::Test::RecordProperty("pathsCompared", r.compared);
	::testing::Test::RecordProperty("pathsIdentical", r.identical);
}

TEST(PatchableMesh, BoundsSplitFourByFourChangesNoAnswer) {
	expectBoundsSplitInvisible(4, 62);
}

TEST(PatchableMesh, DoorEditsChangeTagsOnly) {
	const WallsAndDoors input = wallsAndDoors();
	Reached				r;
	reach(r, input, 9);
	const PagedVector<NavTriangle> before = r.h.mesh.triangles();
	r.h.mesh.removeDoor(r.doorIds[1]);
	EXPECT_FALSE(r.h.mesh.hasForests());
	EXPECT_TRUE(r.h.mesh.validate().empty()) << r.h.mesh.validate();
	for (std::size_t t = 0; t < before.size(); ++t) {
		EXPECT_EQ(before[t].v, r.h.mesh.triangles()[t].v);
	}
	r.doorIds[1] = r.h.mesh.addDoor(input.doors[1]);
	EXPECT_TRUE(r.h.mesh.validate().empty()) << r.h.mesh.validate();
	r.h.mesh.rebuildForests();
	const NavMesh fresh = buildNavMesh(freshInput(r, input));
	EXPECT_TRUE(tagsAndWidthsMatch(r.h.mesh, fresh, kDefaultMaxClearanceMm));
}

// A dead slot is never located, never a neighbour, never a forest node.
TEST(PatchableMesh, DeadSlotsAreUnreachable) {
	Harness h;
	h.add(bounds(rect(0, 0, 10000, 10000)));
	const RingId tree = h.add(blocked(rect(4000, 4000, 6000, 6000)));
	PatchEdits	 remove;
	remove.removes.push_back(tree);
	h.apply(remove);
	h.mesh.rebuildForests();
	const NavView view = h.mesh.view();
	int			  dead = 0;
	for (std::int32_t t = 0; t < view.triangleSlots(); ++t) {
		if (view.isLive(t)) {
			continue;
		}
		++dead;
		EXPECT_EQ(view.forest(true)->component[static_cast<std::size_t>(t)], -1);
		EXPECT_EQ(view.forest(false)->component[static_cast<std::size_t>(t)], -1);
	}
	EXPECT_GT(dead, 0);
	EXPECT_TRUE(pathThrough(view, {1000, 1000}, {9000, 9000}, 300).reachable);
}

// Widths lag behind patches until refreshWidths, which serves every patch since the
// last call at once.
TEST(PatchableMesh, OneRefreshServesManyPatches) {
	const Fixture fixture = realRingsFixture();
	PatchableMesh mesh;
	PatchEdits	  base;
	base.adds = fixture.base;
	mesh.applyPatch(base);
	mesh.refreshWidths();
	std::mt19937		rng(21);
	std::vector<RingId> placed;
	for (std::size_t i = 0; i < fixture.trees.size(); i += 37) {
		PatchEdits edits;
		edits.adds.push_back(fixture.trees[i]);
		if (!placed.empty() && pick(rng, 3) == 0) {
			edits.removes.push_back(placed.back());
			placed.pop_back();
		}
		const PatchReport r = mesh.applyPatch(edits);
		placed.push_back(r.added.front());
		EXPECT_FALSE(mesh.widthsCurrent());
	}
	mesh.refreshWidths();
	EXPECT_TRUE(mesh.widthsCurrent());
	EXPECT_TRUE(mesh.validate().empty()) << mesh.validate();
}

// The refresh walks out from where patches changed the mesh, so it must see every kind of
// change left pending at once: ground removed (its neighbours' far side gone, no fresh
// triangle beside them), ground added, and patches that re-derive triangles an earlier
// pending patch made. One refresh after all of them gives widths a fresh search agrees
// with, detail tags included.
TEST(PatchableMesh, OneRefreshServesRemovedGroundAndReplacedTriangles) {
	const Fixture fixture = realRingsFixture();
	PatchableMesh mesh({.maxBoundsEdgeMm = 16000, .detailTags = {kTree}});
	Vec2i64		  lo;
	Vec2i64		  hi;
	lo = hi = fixture.base.front().ring.front();
	for (const Vec2i64& p : fixture.base.front().ring) {
		lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
		hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
	}
	const std::int64_t side = hi.x - lo.x;
	PatchEdits		   base;
	base.adds = fixture.base;
	base.adds.insert(base.adds.end(), fixture.trees.begin(), fixture.trees.end());
	base.adds.push_back(bounds(rect(hi.x, lo.y, hi.x + side, hi.y))); // a neighbour square
	const PatchReport first = mesh.applyPatch(base);
	mesh.refreshWidths();
	ASSERT_TRUE(mesh.validate().empty()) << mesh.validate();
	const RingId neighbour = first.added.back();

	// A tree beside the shared side, then the same ground re-derived by two more patches.
	PatchEdits addTree;
	addTree.adds.push_back(blocked(rect(hi.x - 1500, (lo.y + hi.y) / 2, hi.x - 900, (lo.y + hi.y) / 2 + 600)));
	const RingId tree = mesh.applyPatch(addTree).added.front();
	EXPECT_FALSE(mesh.widthsCurrent());
	PatchEdits moveTree;
	moveTree.moves.push_back({tree, rect(hi.x - 1200, (lo.y + hi.y) / 2 + 300, hi.x - 600, (lo.y + hi.y) / 2 + 900)});
	mesh.applyPatch(moveTree);
	PatchEdits removeTrees;
	removeTrees.removes.push_back(first.added[fixture.base.size()]);
	removeTrees.removes.push_back(first.added[fixture.base.size() + 1]);
	mesh.applyPatch(removeTrees);
	// The neighbour square goes: ground beside the shared side loses its far side.
	PatchEdits removeGround;
	removeGround.removes.push_back(neighbour);
	mesh.applyPatch(removeGround);
	// And a new square comes back further out, joined by nothing.
	PatchEdits addGround;
	addGround.adds.push_back(bounds(rect(hi.x + side, lo.y, hi.x + 2 * side, hi.y)));
	mesh.applyPatch(addGround);
	EXPECT_FALSE(mesh.widthsCurrent());
	mesh.refreshWidths();
	EXPECT_TRUE(mesh.widthsCurrent());
	EXPECT_TRUE(mesh.validate().empty()) << mesh.validate();
}

// --- Review findings ---------------------------------------------------------------

// A width search's visit stamp wraps after 2^32 searches on a long-lived mesh. A wrapped
// stamp that still matched marks from earlier searches would skip triangles and widen
// corridors; past the wrap every width must come out as before.
TEST(PatchableMesh, WidthStampWrapKeepsWidths) {
	const Fixture fixture = realRingsFixture();
	Harness		  h;
	PatchEdits	  edits;
	edits.adds = fixture.base;
	edits.adds.insert(edits.adds.end(), fixture.trees.begin(), fixture.trees.end());
	h.apply(edits);
	const NavView			  view = h.mesh.view();
	WidthSearch				  search;
	std::vector<std::int64_t> first;
	for (std::int32_t t = 0; t < view.triangleSlots(); ++t) {
		for (int apex = 0; view.isLive(t) && apex < 3; ++apex) {
			first.push_back(corridorWidthMm(view, search, t, apex, kDefaultMaxClearanceMm));
		}
	}
	// Cross the wrap with one search. Wrapped without clearing, the pass below would number
	// its searches exactly as the first pass did and meet each search's own marks.
	search.stamp = std::numeric_limits<std::uint32_t>::max();
	for (std::int32_t t = 0; t < view.triangleSlots() && search.stamp == std::numeric_limits<std::uint32_t>::max(); ++t) {
		for (int apex = 0; view.isLive(t) && apex < 3; ++apex) {
			corridorWidthMm(view, search, t, apex, kDefaultMaxClearanceMm);
		}
	}
	std::size_t i = 0;
	for (std::int32_t t = 0; t < view.triangleSlots(); ++t) {
		for (int apex = 0; view.isLive(t) && apex < 3; ++apex) {
			ASSERT_EQ(corridorWidthMm(view, search, t, apex, kDefaultMaxClearanceMm), first[i++]) << "triangle " << t << " apex " << apex;
		}
	}
}

// A view taken before a patch reads the patched mesh: no forests until they are
// rebuilt, no path while widths lag, then the same answers as a fresh view.
TEST(PatchableMesh, ViewTakenBeforePatch) {
	const WallsAndDoors input = wallsAndDoors();
	Reached				r;
	reach(r, input, 32);
	const NavView		 early = r.h.mesh.view();
	const std::uint64_t	 epoch = r.h.mesh.epoch();
	const Vec2i64		 a{1000, 1000};
	const Vec2i64		 b{15000, 15000};
	ASSERT_TRUE(pathThrough(early, a, b, 300).reachable);
	PatchEdits moreTrees;
	moreTrees.adds.push_back(blocked(rect(4000, 4000, 4600, 4500)));
	r.h.mesh.applyPatch(moreTrees);
	EXPECT_NE(early.epoch(), epoch);
	EXPECT_EQ(early.forest(true), nullptr);
	EXPECT_FALSE(early.widthsCurrent());
	EXPECT_DEBUG_DEATH(EXPECT_FALSE(pathThrough(early, a, b, 300).reachable), "widths lag");
	r.h.mesh.rebuildForests();
	EXPECT_NE(early.forest(true), nullptr);
	EXPECT_EQ(pathLengthMm(pathThrough(early, a, b, 300)), pathLengthMm(pathThrough(r.h.mesh.view(), a, b, 300)));
}

// Cell keys hold full coordinates: a mesh far out, at 2^39 and 2^40 mm, patches and
// locates exactly as one near the origin.
TEST(PatchableMesh, FarCoordinatesMatchFreshBuild) {
	for (const std::int64_t offset : {std::int64_t{1} << 39, std::int64_t{1} << 40}) {
		WallsAndDoors input = wallsAndDoors();
		const Vec2i64 by{offset, offset / 2};
		for (std::vector<NavInputPolygon>* list : {&input.base, &input.rings}) {
			for (NavInputPolygon& p : *list) {
				p.ring = translated(p.ring, by);
			}
		}
		for (DoorPortal& d : input.doors) {
			d.a = d.a + by;
			d.b = d.b + by;
		}
		Reached r;
		reach(r, input, 33);
		ASSERT_TRUE(matchesFresh(r.h.mesh, r.h.model)) << "offset " << offset;
		const NavMesh fresh = buildNavMesh(freshInput(r, input));
		ASSERT_TRUE(tagsAndWidthsMatch(r.h.mesh, fresh, kDefaultMaxClearanceMm)) << "offset " << offset;
		const PathResult far	= pathThrough(r.h.mesh.view(), Vec2i64{1000, 1000} + by, Vec2i64{15000, 15000} + by, 300);
		const PathResult origin = pathThrough(fresh, Vec2i64{1000, 1000} + by, Vec2i64{15000, 15000} + by, 300);
		EXPECT_TRUE(far.reachable);
		EXPECT_EQ(pathLengthMm(far), pathLengthMm(origin));
	}
}

// Two equal-area walls crossing: the junction's tag depends on the walls, never on
// which was added first or re-added later, and buildNavMesh agrees.
TEST(PatchableMesh, EqualAreaTieBreakIgnoresHistory) {
	const NavInputPolygon across{rect(0, 900, 2000, 1100), true, 12};
	const NavInputPolygon down{rect(900, 0, 1100, 2000), true, 11};
	auto				  junctionTag = [](const PatchableMesh& mesh) {
		 return mesh.triangles()[static_cast<std::size_t>(mesh.locate({1000, 1000}))].faceBlocker;
	};
	std::vector<std::int64_t> tags;
	for (const bool acrossFirst : {true, false}) {
		Harness h;
		h.add(bounds(rect(-1000, -1000, 3000, 3000)));
		const RingId first = h.add(acrossFirst ? across : down);
		h.add(acrossFirst ? down : across);
		tags.push_back(junctionTag(h.mesh));
		PatchEdits out;
		out.removes.push_back(first);
		h.apply(out);
		h.add(acrossFirst ? across : down);
		tags.push_back(junctionTag(h.mesh));
		ASSERT_TRUE(matchesFresh(h.mesh, h.model));
	}
	NavMeshInput in;
	in.polygons = {bounds(rect(-1000, -1000, 3000, 3000)), across, down};
	const NavMesh fresh = buildNavMesh(in);
	tags.push_back(fresh.triangles[static_cast<std::size_t>(locateTriangle(fresh, {1000, 1000}))].faceBlocker);
	std::swap(in.polygons[1], in.polygons[2]);
	const NavMesh swapped = buildNavMesh(in);
	tags.push_back(swapped.triangles[static_cast<std::size_t>(locateTriangle(swapped, {1000, 1000}))].faceBlocker);
	for (const std::int64_t tag : tags) {
		EXPECT_EQ(tag, 11);
	}
}

// Misuse in release: a ring removed twice, or removed dead, is refused and leaves the
// registry sound; reachability without forests answers "maybe" instead of crashing.
TEST(PatchableMesh, MisuseIsRefusedInRelease) {
	Harness h;
	h.add(bounds(rect(0, 0, 10000, 10000)));
	const RingId tree = h.add(blocked(rect(4000, 4000, 4500, 4500)));
	PatchEdits	 twice;
	twice.removes = {tree, tree};
	EXPECT_DEBUG_DEATH(h.mesh.applyPatch(twice), "editing a dead ring");
	EXPECT_DEBUG_DEATH(h.mesh.applyPatch(twice), "editing a dead ring");
	h.mesh.refreshWidths();
	EXPECT_TRUE(h.mesh.validate().empty()) << h.mesh.validate();
	const RingId a = h.mesh.applyPatch([] {
		PatchEdits e;
		e.adds.push_back(blocked(rect(1000, 1000, 1500, 1500)));
		return e;
	}()).added.front();
	const RingId b = h.mesh.applyPatch([] {
		PatchEdits e;
		e.adds.push_back(blocked(rect(7000, 7000, 7500, 7500)));
		return e;
	}()).added.front();
	EXPECT_NE(a, b);
	h.mesh.refreshWidths();
	EXPECT_TRUE(h.mesh.validate().empty()) << h.mesh.validate();
	ASSERT_FALSE(h.mesh.hasForests());
	const NavView view = h.mesh.view();
	EXPECT_DEBUG_DEATH(EXPECT_EQ(reachFrom(view, {100, 100}, 300).startTri, -1), "no reachability forests");
	EXPECT_DEBUG_DEATH(EXPECT_TRUE(reaches(view, ReachFrom{}, {9000, 9000})), "no reachability forests");
}

// A long hedge on an open 512 m chunk, priced: going through costs far less than going
// round its end, whatever the bounds sides are split into, and says so.
TEST(PatchableMesh, PricedHedgeIsCrossedOnEveryTiling) {
	constexpr TagPrice kTrees[] = {{kTree, 50.0}};
	for (const std::int64_t split : {64000, 16000}) {
		PatchableMesh mesh({.maxBoundsEdgeMm = split, .detailTags = {kTree}});
		PatchEdits	  edits;
		edits.adds.push_back(bounds(rect(0, 0, 512000, 512000)));
		for (std::int64_t y = 2000; y <= 60000; y += 500) {
			edits.adds.push_back(blocked(rect(39700, y - 300, 40300, y + 300)));
		}
		mesh.applyPatch(edits);
		mesh.refreshWidths();
		BeliefFilter belief;
		belief.prices = kTrees;
		const PathResult r = pathThrough(mesh.view(), {37000, 30000}, {43000, 30000}, 300, belief);
		ASSERT_TRUE(r.reachable) << "split " << split;
		EXPECT_TRUE(r.crossesPricedFaces) << "split " << split;
		EXPECT_EQ(pathLengthMm(r), 6000) << "split " << split;
		// 5.4 m of open ground and the 0.6 m hedge at 50 times.
		EXPECT_NEAR(r.cost, 5400.0 + 50.0 * 600.0, 4.0) << "split " << split;
	}
}

// A goal ringed by trees (0.6 m trunks every 0.55 m round a 3 m circle) on an open chunk:
// the only way in is through a trunk, at its price, on every tiling.
TEST(PatchableMesh, PricedRingIsEnteredOnEveryTiling) {
	constexpr TagPrice kTrees[] = {{kTree, 50.0}};
	const Vec2i64	   goal{20000, 20000};
	for (const std::int64_t split : {64000, 16000}) {
		PatchableMesh mesh({.maxBoundsEdgeMm = split, .detailTags = {kTree}});
		PatchEdits	  edits;
		edits.adds.push_back(bounds(rect(0, 0, 512000, 512000)));
		const int count = static_cast<int>(std::ceil(6.2831853 * 3000.0 / 550.0));
		for (int i = 0; i < count; ++i) {
			const double	   angle = 6.2831853 * i / count;
			const std::int64_t x	 = goal.x + std::llround(3000.0 * std::cos(angle));
			const std::int64_t y	 = goal.y + std::llround(3000.0 * std::sin(angle));
			edits.adds.push_back(blocked(rect(x - 300, y - 300, x + 300, y + 300)));
		}
		mesh.applyPatch(edits);
		mesh.refreshWidths();
		EXPECT_FALSE(pathThrough(mesh.view(), {10000, 20000}, goal, 300).reachable) << "split " << split;
		BeliefFilter belief;
		belief.prices = kTrees;
		const PathResult r = pathThrough(mesh.view(), {10000, 20000}, goal, 300, belief);
		ASSERT_TRUE(r.reachable) << "split " << split;
		EXPECT_TRUE(r.crossesPricedFaces) << "split " << split;
		EXPECT_LT(pathLengthMm(r), 10500) << "split " << split;
	}
}

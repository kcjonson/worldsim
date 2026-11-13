#include "nav/PatchableMesh.h"

#include "arrangement/Arrangement.h"
#include "nav/LimitPolygon.h"
#include "nav/NavMesh.h"
#include "nav/NavMeshDumpRings.test.h"
#include "nav/NavMeshRealRings.test.h"
#include "nav/PathQuery.h"
#include "predicates/Predicates.h"
#include "triangulation/ConstrainedDelaunay.h"
#include "visibility/Visibility.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <numeric>
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
	constexpr std::int64_t kObstacle   = -2;
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

	std::vector<RingId> toggledBy(const std::vector<RingId>& cover, const std::vector<std::int64_t>& provenance, const Model& model) {
		std::map<RingId, int> count;
		for (std::int64_t p : provenance) {
			const RingId r = decode(p).ring;
			++count[model.at(r).blocked ? r : kGroundToken];
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
															   arrangement.edges[static_cast<std::size_t>(c)].provenance, model);
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
			if (topo.cover[t].empty() || topo.cover[t].front() != kGroundToken) {
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
	// area; its tags equal buildNavMesh's at the centroid; (3) validate().
	::testing::AssertionResult matchesFresh(const PatchableMesh& mesh, const Model& model) {
		const std::string problem = mesh.validate();
		if (!problem.empty()) {
			return ::testing::AssertionFailure() << "validate: " << problem;
		}

		NavMeshInput						input;
		std::vector<RingId>					ids;
		std::vector<const NavInputPolygon*> rings;
		for (const auto& [id, poly] : model) {
			input.polygons.push_back(poly);
			ids.push_back(id);
			rings.push_back(&poly);
		}
		std::vector<InputSegment> segments;
		ringsArrangementInput(
			rings, [&](std::int32_t ring, std::int32_t edge) { return encode(ids[static_cast<std::size_t>(ring)], edge); }, segments);
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
			if (f.faceBlocker != tri.faceBlocker || f.faceOpening != tri.faceOpening) {
				return ::testing::AssertionFailure() << "triangle " << t << " at (" << a.x << ", " << a.y << ") tagged " << tri.faceBlocker
													 << ", fresh build " << f.faceBlocker;
			}
		}
		return ::testing::AssertionSuccess();
	}

	// Even-odd containment of a point that lies on no edge of the ring.
	bool insideEvenOdd(const Vec2i64& p, const std::vector<Vec2i64>& ring) {
		bool inside = false;
		for (std::size_t i = 0; i < ring.size(); ++i) {
			const Vec2i64& a = ring[i];
			const Vec2i64& b = ring[(i + 1) % ring.size()];
			if ((a.y > p.y) != (b.y > p.y)) {
				const int side = cross(b - a, p - a).sign();
				if (b.y > a.y ? side > 0 : side < 0) {
					inside = !inside;
				}
			}
		}
		return inside;
	}

	// The mesh against the rings as given, by nothing the library builds them with: at
	// sample points clear of every original edge by kOffGroundReachMm, the mesh is there
	// exactly where some ground outline holds the point, and the cover's blocked rings are
	// exactly those that hold it, each even-odd. Half the points fall anywhere in the rings'
	// box, half in one ring's box, chosen by a hash of the rings, so a run is repeatable.
	::testing::AssertionResult agreesWithTheRingsAsGiven(const PatchableMesh& mesh, const Model& model, int samples = 64) {
		if (model.empty()) {
			return ::testing::AssertionSuccess();
		}
		std::uint64_t		   hash = 0xCBF29CE484222325ULL;
		std::vector<const NavInputPolygon*> rings;
		Vec2i64				   lo = model.begin()->second.ring.front();
		Vec2i64				   hi = lo;
		for (const auto& [id, poly] : model) {
			rings.push_back(&poly);
			for (const Vec2i64& v : poly.ring) {
				hash = (hash ^ static_cast<std::uint64_t>(v.x)) * 0x100000001B3ULL;
				hash = (hash ^ static_cast<std::uint64_t>(v.y)) * 0x100000001B3ULL;
				lo	 = {std::min(lo.x, v.x), std::min(lo.y, v.y)};
				hi	 = {std::max(hi.x, v.x), std::max(hi.y, v.y)};
			}
		}
		std::mt19937 rng(static_cast<std::uint32_t>(hash ^ (hash >> 32)));
		auto		 draw = [&](std::int64_t from, std::int64_t to) {
			return from + static_cast<std::int64_t>(static_cast<std::uint64_t>(rng()) % static_cast<std::uint64_t>(to - from + 1));
		};
		for (int i = 0; i < samples; ++i) {
			Vec2i64 boxLo = lo;
			Vec2i64 boxHi = hi;
			if (i % 2 == 1) {
				const std::vector<Vec2i64>& ring = rings[static_cast<std::size_t>(draw(0, static_cast<std::int64_t>(rings.size()) - 1))]->ring;
				boxLo							 = ring.front();
				boxHi							 = boxLo;
				for (const Vec2i64& v : ring) {
					boxLo = {std::min(boxLo.x, v.x), std::min(boxLo.y, v.y)};
					boxHi = {std::max(boxHi.x, v.x), std::max(boxHi.y, v.y)};
				}
			}
			const Vec2i64 reach{kOffGroundReachMm, kOffGroundReachMm};
			boxLo				= boxLo - reach;
			boxHi				= boxHi + reach;
			const Vec2i64 p{draw(boxLo.x, boxHi.x), draw(boxLo.y, boxHi.y)};
			bool		  clear = true;
			for (const NavInputPolygon* poly : rings) {
				for (std::size_t k = 0; k < poly->ring.size() && clear; ++k) {
					clear = !geometry::withinDistanceOfSegment(p, poly->ring[k], poly->ring[(k + 1) % poly->ring.size()], kOffGroundReachMm);
				}
			}
			if (!clear) {
				continue;
			}
			bool				ground = false;
			std::vector<RingId> holding{kGroundToken};
			for (const auto& [id, poly] : model) {
				if (!insideEvenOdd(p, poly.ring)) {
					continue;
				}
				if (poly.blocked) {
					holding.push_back(id);
				} else {
					ground = true;
				}
			}
			const std::int32_t t = mesh.locate(p);
			if ((t >= 0) != ground) {
				return ::testing::AssertionFailure() << "(" << p.x << ", " << p.y << ") is " << (ground ? "" : "not ") << "inside a ground outline but "
													 << (t >= 0 ? "on" : "off") << " the mesh";
			}
			if (t >= 0 && mesh.cover(t) != holding) {
				return ::testing::AssertionFailure() << "(" << p.x << ", " << p.y << ") lies in other blocked rings than its triangle's cover names";
			}
		}
		return ::testing::AssertionSuccess();
	}

	NavInputPolygon blocked(std::vector<Vec2i64> ring, std::int64_t provenance = kObstacle, bool holeCapable = false) {
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

	// The ring edges the live mesh edge pq lies on; empty when it is unconstrained or no
	// live triangle has it.
	std::vector<EdgeSource> sourcesBetween(const PatchableMesh& mesh, const Vec2i64& p, const Vec2i64& q) {
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (!mesh.triangleLive(t)) {
				continue;
			}
			const NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
			for (int e = 0; e < 3; ++e) {
				const Vec2i64& a = mesh.vertices()[tri.v[static_cast<std::size_t>(e)]];
				const Vec2i64& b = mesh.vertices()[tri.v[static_cast<std::size_t>((e + 1) % 3)]];
				if ((a == p && b == q) || (a == q && b == p)) {
					return mesh.edgeSources(t, e);
				}
			}
		}
		return {};
	}

	// The vertices live triangles use on the closed segment ab, ascending.
	std::vector<Vec2i64> liveVerticesOn(const PatchableMesh& mesh, const Vec2i64& a, const Vec2i64& b) {
		std::set<Vec2i64> on;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (!mesh.triangleLive(t)) {
				continue;
			}
			for (const std::uint32_t v : mesh.triangles()[static_cast<std::size_t>(t)].v) {
				const Vec2i64& p = mesh.vertices()[v];
				if (cross(b - a, p - a).sign() == 0 && std::min(a.x, b.x) <= p.x && p.x <= std::max(a.x, b.x) && std::min(a.y, b.y) <= p.y &&
					p.y <= std::max(a.y, b.y)) {
					on.insert(p);
				}
			}
		}
		return {on.begin(), on.end()};
	}

	// The ids a report lists with `why`, ascending.
	std::vector<std::int32_t> idsWith(const PatchReport& report, std::uint8_t why) {
		std::vector<std::int32_t> out;
		for (const ChangedTriangle& c : report.changes) {
			if ((c.why & why) != 0) {
				out.push_back(c.id);
			}
		}
		return out;
	}

	// Applies a patch to the mesh and the model together.
	struct Harness {
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

	// std::shuffle's draws are the standard library's own, so a seed would give each
	// toolchain a different history.
	template <typename T> void shuffle(std::vector<T>& items, std::mt19937& rng) {
		for (std::size_t i = items.size(); i > 1; --i) {
			std::swap(items[i - 1], items[pick(rng, i)]);
		}
	}

	// --- Game fixtures: border and water, then obstacles in random batches ------------------

	struct Fixture {
		std::vector<NavInputPolygon> base;
		std::vector<NavInputPolygon> obstacles;
	};

	Fixture dumpFixture(const std::vector<testdata::DumpRing>& rings) {
		Fixture f;
		for (const testdata::DumpRing& r : rings) {
			NavInputPolygon poly{r.ring, r.blocked, r.provenanceId, kNoOpening, r.holeCapable};
			(r.provenanceId == kObstacle ? f.obstacles : f.base).push_back(std::move(poly));
		}
		return f;
	}

	Fixture realRingsFixture() {
		Fixture f;
		f.base.push_back(bounds(testdata::realBorderRing()));
		f.base.push_back(blocked(testdata::realWaterRing(), kWater, true));
		for (const std::vector<Vec2i64>& obstacle : testdata::realObstacleRings()) {
			f.obstacles.push_back(blocked(obstacle));
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

		std::vector<NavInputPolygon> pending = fixture.obstacles;
		shuffle(pending, rng);
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

		// Remove every obstacle in a few batches: the mesh returns to border and water.
		shuffle(placed, rng);
		while (!placed.empty()) {
			PatchEdits		  edits;
			const std::size_t count = std::min<std::size_t>(placed.size(), 1 + pick(rng, 120));
			edits.removes.assign(placed.end() - static_cast<std::ptrdiff_t>(count), placed.end());
			placed.resize(placed.size() - count);
			h.apply(edits);
		}
		ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "seed " << seed << " with every obstacle removed";
	}

	// --- Adversarial soups: tiny coordinates so rings overlap, share edges, touch, nest --

	std::vector<Vec2i64> randomRing(std::mt19937& rng) {
		switch (pick(rng, 3)) {
			case 0: {
				const std::int64_t x = between(rng, -4, 42);
				const std::int64_t y = between(rng, -4, 42);
				const std::int64_t h = between(rng, 1, 12);
				const std::int64_t w = between(rng, 1, 12);
				return rect(x, y, x + w, y + h);
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

	// Every patched state is checked against a from-scratch build.
	void runSoup(std::uint32_t seed, int steps) {
		std::mt19937 rng(seed);
		Harness		 h;
		// Two ground squares sharing x = 20; the second arrives part way through, and
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
			ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "seed " << seed << " step " << step << describe(h.model);
		}
	}

} // namespace

TEST(PatchableMesh, FirstBoundsMeshesAnEmptyMesh) {
	Harness h;
	h.add(bounds(rect(0, 0, 1000, 1000)));
	EXPECT_EQ(h.mesh.liveTriangleCount(), 2);
	EXPECT_EQ(idsWith(h.last, kTriangleCreated).size(), 2U);
	EXPECT_TRUE(idsWith(h.last, kTriangleFreed).empty());
	EXPECT_TRUE(matchesFresh(h.mesh, h.model));
}

TEST(PatchableMesh, RingOffGroundTouchesNothing) {
	Harness h;
	h.add(bounds(rect(0, 0, 1000, 1000)));
	h.add(blocked(rect(2000, 2000, 2100, 2100)));
	EXPECT_EQ(h.mesh.liveTriangleCount(), 2);
	EXPECT_TRUE(idsWith(h.last, kTriangleCreated).empty());
	EXPECT_TRUE(matchesFresh(h.mesh, h.model));
	// Inside a slanted outline's box, beyond its sides.
	Harness slanted;
	slanted.add(bounds(limitPolygon({0, 0}, 5000)));
	const std::int32_t before = slanted.mesh.liveTriangleCount();
	slanted.add(blocked({{4700, 4700}, {5000, 4700}, {4800, 5000}}));
	EXPECT_EQ(slanted.mesh.liveTriangleCount(), before);
	EXPECT_TRUE(idsWith(slanted.last, kTriangleCreated | kTriangleFreed).empty());
	EXPECT_TRUE(matchesFresh(slanted.mesh, slanted.model));
}

TEST(PatchableMesh, AddRemoveMoveObstacle) {
	Harness h;
	h.add(bounds(rect(0, 0, 10000, 10000)));
	const RingId obstacle = h.add(blocked(rect(4000, 4000, 4500, 4600)));
	EXPECT_TRUE(matchesFresh(h.mesh, h.model));
	PatchEdits move;
	move.moves.push_back({obstacle, rect(4300, 4100, 4800, 4700)});
	h.apply(move);
	EXPECT_TRUE(matchesFresh(h.mesh, h.model));
	PatchEdits remove;
	remove.removes.push_back(obstacle);
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
	const RingId obstacle = h.add(blocked(rect(9500, 9000, 10500, 10000)));
	EXPECT_TRUE(matchesFresh(h.mesh, h.model));
	PatchEdits remove;
	remove.removes.push_back(obstacle);
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
		EXPECT_EQ(got.changes, expected.changes);
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
	const std::vector<std::int32_t> freed = idsWith(h.last, kTriangleFreed);
	for (std::int32_t t = 0; t < static_cast<std::int32_t>(before.size()); ++t) {
		const bool touched = std::binary_search(freed.begin(), freed.end(), t);
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

// Ground squares sharing a side, in both orders, with an obstacle hanging off the first one's
// edge: its outside part has no triangles until the neighbour arrives and completes it.
// When the first square leaves, the second's side is the mesh's constrained boundary again;
// when it comes back, the side is dissolved again.
TEST(PatchableMesh, HangingRingCompletedByNeighbourBounds) {
	// Mesh edges on x = 10000: their length, how much of it is boundary carrying `square`'s
	// edge, and how many name any bounds ring.
	struct SharedLine {
		std::int64_t length		  = 0;
		std::int64_t ownBoundary  = 0;
		int			 boundsEdges  = 0;
	};
	const auto onSharedLine = [](const PatchableMesh& mesh, RingId square) {
		SharedLine out;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (!mesh.triangleLive(t)) {
				continue;
			}
			const NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
			for (int e = 0; e < 3; ++e) {
				const Vec2i64& a = mesh.vertices()[tri.v[static_cast<std::size_t>(e)]];
				const Vec2i64& b = mesh.vertices()[tri.v[static_cast<std::size_t>((e + 1) % 3)]];
				if (a.x != 10000 || b.x != 10000) {
					continue;
				}
				const std::int64_t length = std::abs(b.y - a.y);
				out.length += length;
				bool own = false;
				for (const EdgeSource& s : mesh.edgeSources(t, e)) {
					own = own || s.ring == square;
					out.boundsEdges += mesh.ring(s.ring).polygon.blocked ? 0 : 1;
				}
				if (own && tri.neighbor[static_cast<std::size_t>(e)] < 0) {
					out.ownBoundary += length;
				}
			}
		}
		return out;
	};
	for (const bool westFirst : {true, false}) {
		Harness				  h;
		const NavInputPolygon west = bounds(rect(0, 0, 10000, 10000));
		const NavInputPolygon east = bounds(rect(10000, 0, 20000, 10000));
		h.add(westFirst ? west : east);
		h.add(blocked({{9400, 4000}, {10700, 4300}, {10500, 5200}, {9300, 5100}}));
		h.add(blocked(rect(9000, 6000, 11000, 6500)));
		ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << (westFirst ? "west" : "east") << " alone";
		const RingId second = h.add(westFirst ? east : west);
		ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "both";
		EXPECT_EQ(onSharedLine(h.mesh, second).boundsEdges, 0) << "both";
		PatchEdits leave;
		leave.removes.push_back(h.model.begin()->first);
		h.apply(leave);
		ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "first square unloaded";
		const SharedLine alone = onSharedLine(h.mesh, second);
		EXPECT_EQ(alone.ownBoundary, 10000) << "the side, whole, is boundary carrying the second square's edge";
		EXPECT_EQ(alone.length, 10000);
		h.add(westFirst ? west : east);
		ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "first square back";
		EXPECT_EQ(onSharedLine(h.mesh, second).boundsEdges, 0) << "first square back";
	}
}

// Two squares sharing a side, in both arrival orders and in one patch: ground on both
// sides has one cover, and the side's pieces are dissolved, so no constrained edge lies
// on it and only its two ends, which end the outer sides, are vertices on it.
TEST(PatchableMesh, AdjacentSquaresShareOneGroundCover) {
	const NavInputPolygon west = bounds(rect(0, 0, 12000, 12000));
	const NavInputPolygon east = bounds(rect(12000, 0, 24000, 12000));
	for (int order = 0; order < 3; ++order) {
		Harness h;
		if (order == 2) {
			PatchEdits both;
			both.adds = {west, east};
			h.apply(both);
		} else {
			h.add(order == 0 ? west : east);
			h.add(order == 0 ? east : west);
		}
		ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "order " << order;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(h.mesh.triangles().size()); ++t) {
			if (!h.mesh.triangleLive(t)) {
				continue;
			}
			EXPECT_EQ(h.mesh.cover(t), std::vector<RingId>{kGroundToken});
			const NavTriangle& tri = h.mesh.triangles()[static_cast<std::size_t>(t)];
			for (int e = 0; e < 3; ++e) {
				const std::vector<EdgeSource>& sources = h.mesh.edgeSources(t, e);
				std::set<RingId>			   from;
				for (const EdgeSource& s : sources) {
					from.insert(s.ring);
				}
				EXPECT_LE(from.size(), 1U) << "a mesh edge with both squares' sides on it";
				const Vec2i64& a = h.mesh.vertices()[tri.v[static_cast<std::size_t>(e)]];
				const Vec2i64& b = h.mesh.vertices()[tri.v[static_cast<std::size_t>((e + 1) % 3)]];
				const bool	   onSeam = a.x == 12000 && b.x == 12000 && a.y > 0 && b.y > 0 && a.y < 12000 && b.y < 12000;
				EXPECT_FALSE(onSeam && !sources.empty()) << "a constrained edge on the shared side, order " << order;
			}
		}
		for (std::int64_t y = 0; y <= 12000; y += 1000) {
			EXPECT_GE(h.mesh.locate({12000, y}), 0) << "the shared side at y " << y << ", order " << order;
		}
		EXPECT_EQ(liveVerticesOn(h.mesh, {12000, 0}, {12000, 12000}), (std::vector<Vec2i64>{{12000, 0}, {12000, 12000}})) << "order " << order;
	}
}

// A square with no neighbour keeps all four sides as constrained mesh boundary; so does
// one whose neighbour arrives and leaves again, or moves off the shared side.
TEST(PatchableMesh, LoneSquareKeepsFourBoundarySides) {
	auto expectFourSides = [](const PatchableMesh& mesh, const std::string& when) {
		std::int64_t boundaryLength = 0;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (!mesh.triangleLive(t)) {
				continue;
			}
			const NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
			for (int e = 0; e < 3; ++e) {
				const Vec2i64& a = mesh.vertices()[tri.v[static_cast<std::size_t>(e)]];
				const Vec2i64& b = mesh.vertices()[tri.v[static_cast<std::size_t>((e + 1) % 3)]];
				const bool boundary = tri.neighbor[static_cast<std::size_t>(e)] < 0;
				EXPECT_EQ(boundary, !mesh.edgeSources(t, e).empty()) << when;
				const bool onSide = (a.x == b.x && (a.x == 0 || a.x == 12000)) || (a.y == b.y && (a.y == 0 || a.y == 12000));
				if (boundary && onSide) {
					boundaryLength += std::abs(b.x - a.x) + std::abs(b.y - a.y);
				}
			}
		}
		EXPECT_EQ(boundaryLength, 4 * 12000) << when;
	};
	Harness		 h;
	const RingId west = h.add(bounds(rect(0, 0, 12000, 12000)));
	expectFourSides(h.mesh, "alone");
	ASSERT_TRUE(matchesFresh(h.mesh, h.model));
	const RingId east = h.add(bounds(rect(12000, 0, 24000, 12000)));
	ASSERT_TRUE(matchesFresh(h.mesh, h.model));
	PatchEdits leave;
	leave.removes.push_back(east);
	h.apply(leave);
	ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "after the neighbour left";
	expectFourSides(h.mesh, "after the neighbour left");
	const RingId again = h.add(bounds(rect(12000, 0, 24000, 12000)));
	ASSERT_TRUE(matchesFresh(h.mesh, h.model));
	PatchEdits move;
	move.moves.push_back({again, rect(12000, 2000, 24000, 14000)});
	h.apply(move);
	ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "after the neighbour moved";
	PatchEdits back;
	back.moves.push_back({again, rect(12000, 0, 24000, 12000)});
	back.moves.push_back({west, rect(0, 0, 12000, 12000)});
	h.apply(back);
	ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "after both moved back into place";
}

TEST(PatchableMesh, ObstacleInsideASquareShowsInItsCover) {
	Harness		 h;
	h.add(bounds(rect(0, 0, 10000, 10000)));
	h.add(bounds(rect(10000, 0, 20000, 10000)));
	const RingId obstacle = h.add(blocked(rect(4000, 4000, 5000, 5000)));
	ASSERT_TRUE(matchesFresh(h.mesh, h.model));
	const std::int32_t inside = h.mesh.locate({4500, 4500});
	ASSERT_GE(inside, 0);
	EXPECT_EQ(h.mesh.cover(inside), (std::vector<RingId>{kGroundToken, obstacle}));
	const std::int32_t outside = h.mesh.locate({15000, 5000});
	ASSERT_GE(outside, 0);
	EXPECT_EQ(h.mesh.cover(outside), std::vector<RingId>{kGroundToken});
}

// A cover-only ring is in the mesh and in the cover of the ground inside it, and tags
// nothing: that ground is floor, a disc wider than the ring crosses it straight, and an
// obstacle over it still blocks. Removed, it leaves the cover.
TEST(PatchableMesh, CoverOnlyRingMarksGroundAndBlocksNothing) {
	Harness			h;
	h.add(bounds(rect(0, 0, 20000, 10000)));
	NavInputPolygon strip = blocked(rect(9000, 1000, 10000, 9000), kWater);
	strip.coverOnly		  = true;
	const RingId marked	  = h.add(strip);
	ASSERT_TRUE(matchesFresh(h.mesh, h.model));

	const Vec2i64	   inStrip{9500, 5000};
	const std::int32_t inside = h.mesh.locate(inStrip);
	ASSERT_GE(inside, 0);
	EXPECT_EQ(h.mesh.cover(inside), (std::vector<RingId>{kGroundToken, marked}));
	EXPECT_TRUE(isFloorFace(h.mesh.triangles()[static_cast<std::size_t>(inside)]));

	h.mesh.refreshWidths();
	const PathResult across = pathThrough(h.mesh.view(), {2000, 4000}, {18000, 6000}, 600);
	ASSERT_TRUE(across.reachable) << "the strip's sides pinch no corridor";
	EXPECT_NEAR(across.cost, std::hypot(16000.0, 2000.0), 1.0) << "straight across the strip";

	const RingId obstacle = h.add(blocked(rect(9200, 4000, 9800, 6000)));
	ASSERT_TRUE(matchesFresh(h.mesh, h.model));
	const std::int32_t under = h.mesh.locate(inStrip);
	ASSERT_GE(under, 0);
	EXPECT_EQ(h.mesh.cover(under), (std::vector<RingId>{kGroundToken, marked, obstacle}));
	EXPECT_EQ(h.mesh.triangles()[static_cast<std::size_t>(under)].faceBlocker, kObstacle);

	PatchEdits leave;
	leave.removes.push_back(marked);
	h.apply(leave);
	ASSERT_TRUE(matchesFresh(h.mesh, h.model));
	EXPECT_EQ(h.mesh.cover(h.mesh.locate(inStrip)), (std::vector<RingId>{kGroundToken, obstacle}));
	EXPECT_EQ(h.mesh.cover(h.mesh.locate({9500, 2000})), std::vector<RingId>{kGroundToken});
}

// Water clipped at the shared side arrives as one ring per square: its stretch of the
// side stays constrained by the water rings alone, and the open stretches either side
// dissolve.
TEST(PatchableMesh, WaterAcrossTheSharedSideStaysConstrained) {
	for (const bool westFirst : {true, false}) {
		Harness				  h;
		const NavInputPolygon west		= bounds(rect(0, 0, 12000, 12000));
		const NavInputPolygon east		= bounds(rect(12000, 0, 24000, 12000));
		const NavInputPolygon westWater = blocked(rect(9000, 4000, 12000, 8000), kWater, true);
		const NavInputPolygon eastWater = blocked(rect(12000, 4000, 15000, 8000), kWater, true);
		PatchEdits			  first;
		first.adds = {westFirst ? west : east, westFirst ? westWater : eastWater};
		h.apply(first);
		PatchEdits second;
		second.adds = {westFirst ? east : west, westFirst ? eastWater : westWater};
		h.apply(second);
		ASSERT_TRUE(matchesFresh(h.mesh, h.model));
		std::int64_t waterSide = 0;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(h.mesh.triangles().size()); ++t) {
			if (!h.mesh.triangleLive(t)) {
				continue;
			}
			const NavTriangle& tri = h.mesh.triangles()[static_cast<std::size_t>(t)];
			for (int e = 0; e < 3; ++e) {
				const Vec2i64& a = h.mesh.vertices()[tri.v[static_cast<std::size_t>(e)]];
				const Vec2i64& b = h.mesh.vertices()[tri.v[static_cast<std::size_t>((e + 1) % 3)]];
				if (a.x != 12000 || b.x != 12000 || std::max(a.y, b.y) <= 0 || std::min(a.y, b.y) >= 12000) {
					continue;
				}
				const std::vector<EdgeSource>& sources = h.mesh.edgeSources(t, e);
				const bool					   inWater = std::min(a.y, b.y) >= 4000 && std::max(a.y, b.y) <= 8000;
				if (!inWater) {
					EXPECT_TRUE(sources.empty()) << "open ground on the shared side (" << a.y << ", " << b.y << ") is constrained";
					continue;
				}
				ASSERT_EQ(sources.size(), 2U) << "the water stretch carries both water rings' edges and no square's";
				for (const EdgeSource& s : sources) {
					EXPECT_TRUE(h.model.at(s.ring).blocked);
				}
				waterSide += std::abs(b.y - a.y);
			}
		}
		EXPECT_EQ(waterSide, 2 * 4000) << "the water stretch, seen from each side";
		EXPECT_EQ(liveVerticesOn(h.mesh, {12000, 0}, {12000, 12000}), (std::vector<Vec2i64>{{12000, 0}, {12000, 4000}, {12000, 8000}, {12000, 12000}}))
			<< "the side's ends and the water's cut points";
	}
}

// An obstacle edge crossing a dissolved side, exactly at a cut point of the side, is one
// mesh edge: only the obstacle's own corner on the side is a vertex there, whether the
// obstacle comes before or after the second square.
TEST(PatchableMesh, ObstacleEdgeAcrossADissolvedSideIsWhole) {
	for (const bool obstacleFirst : {true, false}) {
		Harness				  h;
		const NavInputPolygon obstacle = blocked({{11000, 3000}, {13000, 5000}, {12000, 6000}});
		h.add(bounds(rect(0, 0, 12000, 12000)));
		if (obstacleFirst) {
			h.add(obstacle);
		}
		h.add(bounds(rect(12000, 0, 24000, 12000)));
		if (!obstacleFirst) {
			h.add(obstacle);
		}
		ASSERT_TRUE(matchesFresh(h.mesh, h.model));
		EXPECT_EQ(liveVerticesOn(h.mesh, {12000, 0}, {12000, 12000}), (std::vector<Vec2i64>{{12000, 0}, {12000, 6000}, {12000, 12000}}))
			<< "obstacle first " << obstacleFirst;
		int wholeEdge = 0;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(h.mesh.triangles().size()); ++t) {
			if (!h.mesh.triangleLive(t)) {
				continue;
			}
			const NavTriangle& tri = h.mesh.triangles()[static_cast<std::size_t>(t)];
			for (int e = 0; e < 3; ++e) {
				const Vec2i64& a = h.mesh.vertices()[tri.v[static_cast<std::size_t>(e)]];
				const Vec2i64& b = h.mesh.vertices()[tri.v[static_cast<std::size_t>((e + 1) % 3)]];
				if (std::minmax(a, b) == std::minmax(Vec2i64{11000, 3000}, Vec2i64{13000, 5000})) {
					++wholeEdge;
				}
			}
		}
		EXPECT_EQ(wholeEdge, 2) << "the obstacle edge, seen from each side";
	}
}

namespace {

	// Square posts 1 m across in rows 4 m apart. Beside each of the lines x = 32 m and
	// x = 64 m a post stands on either side with its near corners 0.1 to 0.5 m from the
	// line; between the lines, more columns 4 m apart.
	std::vector<NavInputPolygon> postsBesideLines(std::uint32_t seed) {
		std::mt19937				 rng(seed);
		std::vector<NavInputPolygon> out;
		for (std::int64_t y = 2000; y + 1000 < 64000; y += 4000) {
			for (const std::int64_t line : {32000, 64000}) {
				const std::int64_t left	 = line - between(rng, 100, 500) - 1000;
				const std::int64_t right = line + between(rng, 100, 500);
				out.push_back(blocked(rect(left, y, left + 1000, y + 1000)));
				out.push_back(blocked(rect(right, y, right + 1000, y + 1000)));
				for (std::int64_t x = line + 4000; x + 1000 < line + 32000 - 3000 && x + 1000 < 96000; x += 4000) {
					out.push_back(blocked(rect(x, y + between(rng, 0, 300), x + 1000, y + 1000)));
				}
			}
			for (std::int64_t x = 4000; x + 1000 < 32000 - 3000; x += 4000) {
				out.push_back(blocked(rect(x, y + between(rng, 0, 300), x + 1000, y + 1000)));
			}
		}
		return out;
	}

	struct Shape {
		double longestMm = 0;
		double aspect	 = 0; // longest edge squared over twice the area
	};

	// The worst triangle among those whose box meets the vertical line x within y 8 m to
	// 56 m (clear of the outer sides).
	Shape worstAlong(const PatchableMesh& mesh, std::int64_t x) {
		Shape worst;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (!mesh.triangleLive(t)) {
				continue;
			}
			const NavTriangle&			 tri = mesh.triangles()[static_cast<std::size_t>(t)];
			const std::array<Vec2i64, 3> p{mesh.vertices()[tri.v[0]], mesh.vertices()[tri.v[1]], mesh.vertices()[tri.v[2]]};
			const std::int64_t			 loX = std::min({p[0].x, p[1].x, p[2].x});
			const std::int64_t			 hiX = std::max({p[0].x, p[1].x, p[2].x});
			const std::int64_t			 loY = std::min({p[0].y, p[1].y, p[2].y});
			const std::int64_t			 hiY = std::max({p[0].y, p[1].y, p[2].y});
			if (loX > x || hiX < x || hiY < 8000 || loY > 56000) {
				continue;
			}
			double longest2 = 0;
			for (int i = 0; i < 3; ++i) {
				const double dx = static_cast<double>(p[static_cast<std::size_t>((i + 1) % 3)].x - p[static_cast<std::size_t>(i)].x);
				const double dy = static_cast<double>(p[static_cast<std::size_t>((i + 1) % 3)].y - p[static_cast<std::size_t>(i)].y);
				longest2		= std::max(longest2, dx * dx + dy * dy);
			}
			const double area2 = cross(p[1] - p[0], p[2] - p[0]).toDouble();
			worst.longestMm	   = std::max(worst.longestMm, std::sqrt(longest2));
			worst.aspect	   = std::max(worst.aspect, longest2 / area2);
		}
		return worst;
	}

} // namespace

// Two ground squares with posts crowding their shared side: the triangles over the side
// are shaped like those over a line of the same posts inside one square.
TEST(PatchableMesh, SharedSideMeshesLikeTheInterior) {
	Harness h;
	h.add(bounds(rect(0, 0, 64000, 64000)));
	PatchEdits posts;
	posts.adds = postsBesideLines(17);
	h.apply(posts);
	h.add(bounds(rect(64000, 0, 128000, 64000)));
	ASSERT_TRUE(h.mesh.validate().empty()) << h.mesh.validate();
	const Shape seam	 = worstAlong(h.mesh, 64000);
	const Shape interior = worstAlong(h.mesh, 32000);
	RecordProperty("seamLongestMm", std::to_string(seam.longestMm));
	RecordProperty("interiorLongestMm", std::to_string(interior.longestMm));
	RecordProperty("seamAspect", std::to_string(seam.aspect));
	RecordProperty("interiorAspect", std::to_string(interior.aspect));
	EXPECT_LE(seam.longestMm, 1.25 * interior.longestMm);
	EXPECT_LE(seam.aspect, 1.25 * interior.aspect);
}

// Over 2 x 2 squares, every point inside the whole, shared sides and the middle corner
// included, is ground in exactly one square, and every point outside the whole or on its
// outline in at most one.
TEST(PatchableMesh, BoundsContainSampleHoldsSharedOutlineOnce) {
	std::vector<SampleRing> squares;
	for (std::int64_t j = 0; j < 2; ++j) {
		for (std::int64_t i = 0; i < 2; ++i) {
			const std::vector<Vec2i64> ring = rect(i * 4, j * 4, (i + 1) * 4, (j + 1) * 4);
			squares.push_back(sampleRing(bounds(j == 0 ? ring : std::vector<Vec2i64>(ring.rbegin(), ring.rend()))));
		}
	}
	for (std::int64_t y = -3; y <= 27; ++y) {
		for (std::int64_t x = -3; x <= 27; ++x) {
			int holding = 0;
			for (const SampleRing& square : squares) {
				holding += boundsContainSample(square, {x, y}) ? 1 : 0;
			}
			const bool inside = x > 0 && x < 24 && y > 0 && y < 24;
			if (inside) {
				EXPECT_EQ(holding, 1) << "(" << x << ", " << y << ") in 3x space";
			} else {
				EXPECT_LE(holding, 1) << "(" << x << ", " << y << ") in 3x space";
			}
		}
	}
}

// Two overlapping squares are one ground area, in one patch or either order, and equal a
// from-scratch build; removing either leaves the other whole.
TEST(PatchableMesh, OverlappingBoundsAreOneGround) {
	const NavInputPolygon a = bounds(rect(0, 0, 10000, 10000));
	const NavInputPolygon b = bounds(rect(6000, 2000, 16000, 12000));
	for (int order = 0; order < 3; ++order) {
		for (const bool removeFirst : {true, false}) {
			Harness h;
			RingId	first  = kNoRing;
			RingId	second = kNoRing;
			if (order == 2) {
				PatchEdits both;
				both.adds = {a, b};
				const PatchReport report = h.apply(both);
				first					 = report.added[0];
				second					 = report.added[1];
			} else {
				first  = h.add(order == 0 ? a : b);
				second = h.add(order == 0 ? b : a);
			}
			ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "order " << order;
			Int128 area(0);
			for (std::int32_t t = 0; t < static_cast<std::int32_t>(h.mesh.triangles().size()); ++t) {
				if (h.mesh.triangleLive(t)) {
					const NavTriangle& tri = h.mesh.triangles()[static_cast<std::size_t>(t)];
					area				   = area + cross(h.mesh.vertices()[tri.v[1]] - h.mesh.vertices()[tri.v[0]], h.mesh.vertices()[tri.v[2]] - h.mesh.vertices()[tri.v[0]]);
				}
			}
			EXPECT_EQ(area, Int128::product(2, 10000 * 10000 * 2 - 4000 * 8000)) << "the union's area, order " << order;
			EXPECT_GE(h.mesh.locate({8000, 5000}), 0) << "the overlap is ground";
			EXPECT_EQ(liveVerticesOn(h.mesh, {6000, 2000}, {6000, 2000}).size(), 0U) << "a corner inside the other square is no vertex";
			PatchEdits leave;
			const RingId gone = removeFirst ? first : second;
			leave.removes.push_back(gone);
			h.apply(leave);
			ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "order " << order << ", one left";
			std::int64_t boundary = 0;
			for (std::int32_t t = 0; t < static_cast<std::int32_t>(h.mesh.triangles().size()); ++t) {
				if (!h.mesh.triangleLive(t)) {
					continue;
				}
				const NavTriangle& tri = h.mesh.triangles()[static_cast<std::size_t>(t)];
				for (int e = 0; e < 3; ++e) {
					if (tri.neighbor[static_cast<std::size_t>(e)] < 0) {
						const Vec2i64 d = h.mesh.vertices()[tri.v[static_cast<std::size_t>((e + 1) % 3)]] - h.mesh.vertices()[tri.v[static_cast<std::size_t>(e)]];
						boundary += std::abs(d.x) + std::abs(d.y);
					}
				}
			}
			EXPECT_EQ(boundary, 4 * 10000) << "the square left is whole, order " << order;
		}
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
	const RingId obstacle = h.add(blocked(rect(2500, 4500, 3500, 5500)));
	ASSERT_TRUE(matchesFresh(h.mesh, h.model));
	PatchEdits move;
	move.moves.push_back({obstacle, rect(5500, 5500, 6500, 6500)});
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

TEST(PatchableMesh, FixtureManyObstaclesMatchesFreshBuild) {
	runFixtureSequence(dumpFixture(testdata::manyObstaclesDump()), 11, 6);
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
	constexpr std::uint32_t kSoupSeeds = 8;
	for (std::uint32_t seed = 1; seed <= kSoupSeeds; ++seed) {
		runSoup(seed, 40);
		if (HasFatalFailure()) {
			return;
		}
	}
}

// Snap rounding leaves a face here straddling the original line of a ring, so one exact
// sample of the face would read a side that depends on which of its triangles is
// sampled; an obstacle wholly off ground, touching nothing on it, changes the triangulation
// enough to move that sample. buildNavMesh classifies by ring parity, as the patched
// mesh does, so the two agree with and without the obstacle.
TEST(PatchableMesh, BuilderTagFollowsRingParity) {
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
		EXPECT_TRUE(matchesFresh(h.mesh, h.model)) << (withOffGround ? "with" : "without") << " the obstacle off ground";
	}
}

// Hole sizes on the Y confluence with every obstacle in: each obstacle taken out and put back
// alone, sorted by what it touches, plus an obstacle across the border. Recorded, not
// asserted: the design's cost depends on them.
TEST(PatchableMesh, HoleSizesOnRealRings) {
	const Fixture fixture = realRingsFixture();
	Harness		  h;
	{
		PatchEdits edits;
		edits.adds = fixture.base;
		edits.adds.insert(edits.adds.end(), fixture.obstacles.begin(), fixture.obstacles.end());
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
	std::vector<RingId> obstacles;
	for (const auto& [id, poly] : h.model) {
		if (poly.provenanceId == kObstacle) {
			obstacles.push_back(id);
		}
	}
	for (RingId obstacle : obstacles) {
		const NavInputPolygon poly = h.model[obstacle];
		bool				  alone = true;
		for (const auto& [other, otherPoly] : h.model) {
			alone = alone && (other == obstacle || !overlaps(poly, otherPoly));
		}
		const std::string kind = overlaps(poly, water) ? "shoreline" : (alone ? "lone" : "");
		PatchEdits		  remove;
		remove.removes.push_back(obstacle);
		h.apply(remove);
		const PatchReport report = h.mesh.applyPatch([&] {
			PatchEdits add;
			add.adds.push_back(poly);
			return add;
		}());
		h.model[report.added.front()] = poly;
		if (!kind.empty()) {
			sizes[kind].freed.push_back(idsWith(report, kTriangleFreed).size());
			sizes[kind].created.push_back(idsWith(report, kTriangleCreated).size());
			sizes[kind].passes.push_back(report.passes);
		}
	}
	h.add(blocked(rect(-63300, 30000, -62700, 30600)));
	sizes["border"].freed.push_back(idsWith(h.last, kTriangleFreed).size());
	sizes["border"].created.push_back(idsWith(h.last, kTriangleCreated).size());
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
	// obstacles scattered over walls and floor alike.
	struct WallsAndDoors {
		std::vector<NavInputPolygon> base;
		std::vector<NavInputPolygon> rings; // walls, door spans, obstacles
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
		return {f.base, f.obstacles, {}};
	}

	// A mesh that reached `rings` through a random sequence: batches in, some taken out
	// and put back, doors added early and some removed and re-added. Widths refreshed.
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
		shuffle(pending, rng);
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
		r.h.mesh.refreshWidths();
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
	// and the verdicts agree for an optimistic belief.
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
				EXPECT_EQ(pathThrough(view, a, b, radius, optimistic).reachable, pathThrough(fresh, a, b, radius, optimistic).reachable)
					<< "optimistic verdict, pair " << i << " radius " << radius;
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
	const std::array<Fixture, 3> fixtures = {dumpFixture(testdata::manyObstaclesDump()), dumpFixture(testdata::controlDump()), realRingsFixture()};
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
	// adjacent squares, one per tile.
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
				for (const NavInputPolygon& obstacle : tile.obstacles) {
					g.rings.push_back(obstacle);
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
	}

	bool sameComponentAt(const PatchableMesh& mesh, const Vec2i64& a, const Vec2i64& b) {
		return mesh.components().sameComponent(mesh.locate(a), mesh.locate(b));
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

	// The constrained edges as segments, every run of collinear edges through vertices
	// no other constrained edge reaches joined into one: where the constraints lie,
	// whatever vertices split them.
	std::set<EdgeKey> constraintLines(const PatchableMesh& mesh) {
		std::set<EdgeKey> edges;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (!mesh.triangleLive(t)) {
				continue;
			}
			const NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
			for (int e = 0; e < 3; ++e) {
				if (!mesh.edgeSources(t, e).empty()) {
					edges.insert(edgeKey(mesh.vertices()[tri.v[static_cast<std::size_t>(e)]], mesh.vertices()[tri.v[static_cast<std::size_t>((e + 1) % 3)]]));
				}
			}
		}
		std::map<Vec2i64, std::vector<Vec2i64>> around;
		for (const auto& [a, b] : edges) {
			around[a].push_back(b);
			around[b].push_back(a);
		}
		auto passesThrough = [&](const Vec2i64& v) {
			const std::vector<Vec2i64>& ends = around[v];
			return ends.size() == 2 && geometry::orientation(ends[0], v, ends[1]) == geometry::Orientation::Collinear;
		};
		std::set<EdgeKey> lines;
		std::set<EdgeKey> walked;
		for (const EdgeKey& edge : edges) {
			if (walked.count(edge) != 0) {
				continue;
			}
			walked.insert(edge);
			std::array<Vec2i64, 2> ends{edge.first, edge.second};
			for (int side = 0; side < 2; ++side) {
				Vec2i64 from = side == 0 ? edge.second : edge.first;
				Vec2i64 at	 = ends[static_cast<std::size_t>(side)];
				while (passesThrough(at)) {
					const std::vector<Vec2i64>& next = around[at];
					const Vec2i64				to	 = next[0] == from ? next[1] : next[0];
					if (!walked.insert(edgeKey(at, to)).second) {
						break;
					}
					from = at;
					at	 = to;
				}
				ends[static_cast<std::size_t>(side)] = at;
			}
			lines.insert(edgeKey(ends[0], ends[1]));
		}
		return lines;
	}

	// Internal bounds edges must not change an answer: the same rings under one bounds
	// square and under a grid of adjacent squares give the same constraints apart from
	// the vertices the squares' corners add on the outline, the same path verdicts and
	// component verdicts, path lengths that agree (lengthsAgree), and the same corridor
	// width at every apex of a triangle both meshes have.
	void expectBoundsSplitInvisible(std::int64_t k, std::uint32_t seed) {
		const TiledGround g = tiledRealRings(k);
		PatchableMesh	  whole;
		PatchableMesh	  split;
		loadGround(whole, {g.border}, g.rings);
		loadGround(split, g.squares, g.rings);
		ASSERT_TRUE(whole.validate().empty()) << whole.validate();
		ASSERT_TRUE(split.validate().empty()) << split.validate();
		EXPECT_TRUE(constraintLines(whole) == constraintLines(split)) << "the squares' shared sides must leave no constraint";

		const NavView						   wv = whole.view();
		const NavView						   sv = split.view();
		const std::vector<Vec2i64>			   points = samplePoints(g.border, 128, seed);
		int									   lengths		  = 0;
		int									   lengthDiffers  = 0;
		int									   lengthsApart	  = 0;
		std::int64_t						   worstDeltaMm	  = 0;
		int									   verdictDiffers = 0;
		int									   componentsDiffer = 0;
		for (std::size_t i = 0; i + 1 < points.size(); i += 2) {
			const Vec2i64& a = points[i];
			const Vec2i64& b = points[i + 1];
			if (sameComponentAt(whole, a, b) != sameComponentAt(split, a, b)) {
				++componentsDiffer;
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
			}
		}
		EXPECT_GT(lengths, 100);
		EXPECT_EQ(lengthsApart, 0);
		::testing::Test::RecordProperty("pathLengthsDiffering", lengthDiffers);
		::testing::Test::RecordProperty("worstSplitMinusWholeMm", std::to_string(worstDeltaMm));
		EXPECT_EQ(verdictDiffers, 0);
		EXPECT_EQ(componentsDiffer, 0);

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
						wt.edgePairWidthMm[static_cast<std::size_t>(f)] != st.edgePairWidthMm[static_cast<std::size_t>(e)]) {
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

namespace {

	// Square posts 1 m across on a lattice `pitch` apart. A square's corners are
	// cocircular, and so are the corners around each gap between four posts, so the
	// inside of every post and the ground between them have diagonals a patch may choose
	// freely.
	std::vector<NavInputPolygon> latticePosts(std::int64_t pitch) {
		std::vector<NavInputPolygon> out;
		for (std::int64_t j = 0; j < 6; ++j) {
			for (std::int64_t i = 0; i < 6; ++i) {
				const std::int64_t x = 2000 + i * pitch;
				const std::int64_t y = 2000 + j * pitch;
				out.push_back(blocked(rect(x, y, x + 1000, y + 1000)));
			}
		}
		return out;
	}

	// Posts 1 m across, `pitch` apart, each nudged by up to `jitter` mm. A post is a
	// right trapezoid, which is never cyclic, so its inside has one triangulation; the
	// nudges break the lattice's cocircular gaps.
	std::vector<NavInputPolygon> nudgedPosts(std::int64_t pitch, std::int64_t jitter, std::uint32_t seed) {
		std::mt19937				 rng(seed);
		std::vector<NavInputPolygon> out;
		for (std::int64_t j = 0; j < 6; ++j) {
			for (std::int64_t i = 0; i < 6; ++i) {
				const std::int64_t x = 2000 + i * pitch + between(rng, 0, jitter);
				const std::int64_t y = 2000 + j * pitch + between(rng, 0, jitter);
				out.push_back(blocked({{x, y}, {x + 1000, y}, {x + 1000, y + 1000}, {x + 200, y + 1000}}));
			}
		}
		return out;
	}

	// Unconstrained edges whose far apex is on the near triangle's circumcircle: each is
	// a diagonal the triangulation may take either way.
	int freeDiagonals(const PatchableMesh& mesh) {
		int count = 0;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (!mesh.triangleLive(t)) {
				continue;
			}
			const NavTriangle& tt = mesh.triangles()[static_cast<std::size_t>(t)];
			for (std::size_t e = 0; e < 3; ++e) {
				const std::int32_t u = tt.neighbor[e];
				if (u < t || tt.edgeProvenance[e] != kNoProvenance) {
					continue;
				}
				const NavTriangle& uu = mesh.triangles()[static_cast<std::size_t>(u)];
				std::size_t		   j  = 0;
				while (uu.neighbor[j] != t) {
					++j;
				}
				const Vec2i64& apex = mesh.vertices()[uu.v[(j + 2) % 3]];
				count += geometry::inCircle(mesh.vertices()[tt.v[0]], mesh.vertices()[tt.v[1]], mesh.vertices()[tt.v[2]], apex) ==
								 geometry::InCircle::OnCircle
							 ? 1
							 : 0;
			}
		}
		return count;
	}

	struct HistoryComparison {
		int freeDiagonals  = 0; // in the triangulation built in one patch
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
		shuffle(pending, rng);
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
		out.freeDiagonals = freeDiagonals(direct.mesh);
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
	const HistoryComparison r = compareHistories(nudgedPosts(4000, 600, 5), 26000, 77);
	ASSERT_EQ(r.freeDiagonals, 0) << "nudged posts must leave the triangulation no choice";
	EXPECT_EQ(r.otherTriangles, 0);
	EXPECT_EQ(r.identical, r.compared);
}

// A start or goal exactly on an obstacle's edge or corner stands on the floor beside it:
// point location settles a shared edge by geometry, which may name the obstacle's face, and
// the query looks past it to a face it can cross.
TEST(PatchableMesh, EndpointsOnAnObstacleEdgeStandOnTheFloor) {
	PatchableMesh mesh;
	PatchEdits	  edits;
	edits.adds.push_back(bounds(rect(0, 0, 4000, 4000)));
	edits.adds.push_back(blocked(rect(1000, 1000, 2000, 2000)));
	mesh.applyPatch(edits);
	mesh.refreshWidths();
	const NavView view		  = mesh.view();
	int			  onObstacleFace = 0;
	for (const Vec2i64& p : {Vec2i64{1500, 1000}, Vec2i64{2000, 1500}, Vec2i64{1500, 2000}, Vec2i64{1000, 1500}, Vec2i64{1000, 1000},
							 Vec2i64{2000, 2000}, Vec2i64{2000, 1000}, Vec2i64{1000, 2000}}) {
		onObstacleFace += isFloorFace(mesh.triangles()[static_cast<std::size_t>(mesh.locate(p))]) ? 0 : 1;
		EXPECT_TRUE(pathThrough(view, p, {3500, 3500}, 0).reachable) << "from (" << p.x << ", " << p.y << ")";
		EXPECT_TRUE(pathThrough(view, {3500, 3500}, p, 0).reachable) << "to (" << p.x << ", " << p.y << ")";
	}
	EXPECT_GT(onObstacleFace, 0) << "some of these points must locate to the obstacle's face for the test to bind";
}

// Where the history picks different free diagonals, paths may differ, within the
// tolerance the bounds-split oracle uses.
TEST(PatchableMesh, PathsAcrossFreeDiagonalsAgree) {
	const HistoryComparison r = compareHistories(latticePosts(5000), 31000, 79);
	EXPECT_GT(r.freeDiagonals, 0) << "the lattice must leave diagonals to choose";
	EXPECT_GT(r.otherTriangles, 0) << "the histories must choose some of them differently";
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
	EXPECT_TRUE(r.h.mesh.validate().empty()) << r.h.mesh.validate();
	for (std::size_t t = 0; t < before.size(); ++t) {
		EXPECT_EQ(before[t].v, r.h.mesh.triangles()[t].v);
	}
	r.doorIds[1] = r.h.mesh.addDoor(input.doors[1]);
	EXPECT_TRUE(r.h.mesh.validate().empty()) << r.h.mesh.validate();
	r.h.mesh.refreshWidths();
	const NavMesh fresh = buildNavMesh(freshInput(r, input));
	EXPECT_TRUE(tagsAndWidthsMatch(r.h.mesh, fresh, kDefaultMaxClearanceMm));
}

// A dead slot is never located, never a neighbour, and carries no component label.
TEST(PatchableMesh, DeadSlotsAreUnreachable) {
	Harness h;
	h.add(bounds(rect(0, 0, 10000, 10000)));
	const RingId obstacle = h.add(blocked(rect(4000, 4000, 6000, 6000)));
	PatchEdits	 remove;
	remove.removes.push_back(obstacle);
	h.apply(remove);
	h.mesh.refreshWidths();
	const NavView view = h.mesh.view();
	int			  dead = 0;
	for (std::int32_t t = 0; t < view.triangleSlots(); ++t) {
		if (view.isLive(t)) {
			continue;
		}
		++dead;
		EXPECT_EQ(h.mesh.components().label(t), kNoComponent);
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
	for (std::size_t i = 0; i < fixture.obstacles.size(); i += 37) {
		PatchEdits edits;
		edits.adds.push_back(fixture.obstacles[i]);
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
// with.
TEST(PatchableMesh, OneRefreshServesRemovedGroundAndReplacedTriangles) {
	const Fixture fixture = realRingsFixture();
	PatchableMesh mesh;
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
	base.adds.insert(base.adds.end(), fixture.obstacles.begin(), fixture.obstacles.end());
	base.adds.push_back(bounds(rect(hi.x, lo.y, hi.x + side, hi.y))); // a neighbour square
	const PatchReport first = mesh.applyPatch(base);
	mesh.refreshWidths();
	ASSERT_TRUE(mesh.validate().empty()) << mesh.validate();
	const RingId neighbour = first.added.back();

	// An obstacle beside the shared side, then the same ground re-derived by two more patches.
	PatchEdits addObstacle;
	addObstacle.adds.push_back(blocked(rect(hi.x - 1500, (lo.y + hi.y) / 2, hi.x - 900, (lo.y + hi.y) / 2 + 600)));
	const RingId obstacle = mesh.applyPatch(addObstacle).added.front();
	EXPECT_FALSE(mesh.widthsCurrent());
	PatchEdits moveObstacle;
	moveObstacle.moves.push_back({obstacle, rect(hi.x - 1200, (lo.y + hi.y) / 2 + 300, hi.x - 600, (lo.y + hi.y) / 2 + 900)});
	mesh.applyPatch(moveObstacle);
	PatchEdits removeObstacles;
	removeObstacles.removes.push_back(first.added[fixture.base.size()]);
	removeObstacles.removes.push_back(first.added[fixture.base.size() + 1]);
	mesh.applyPatch(removeObstacles);
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
	edits.adds.insert(edits.adds.end(), fixture.obstacles.begin(), fixture.obstacles.end());
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

// A view taken before a patch reads the patched mesh: no path while widths lag, then
// the same answers as a fresh view.
TEST(PatchableMesh, ViewTakenBeforePatch) {
	const WallsAndDoors input = wallsAndDoors();
	Reached				r;
	reach(r, input, 32);
	const NavView		 early = r.h.mesh.view();
	const std::uint64_t	 epoch = r.h.mesh.epoch();
	const Vec2i64		 a{1000, 1000};
	const Vec2i64		 b{15000, 15000};
	ASSERT_TRUE(pathThrough(early, a, b, 300).reachable);
	PatchEdits moreObstacles;
	moreObstacles.adds.push_back(blocked(rect(4000, 4000, 4600, 4500)));
	r.h.mesh.applyPatch(moreObstacles);
	EXPECT_NE(early.epoch(), epoch);
	EXPECT_FALSE(early.widthsCurrent());
	EXPECT_DEBUG_DEATH(EXPECT_FALSE(pathThrough(early, a, b, 300).reachable), "widths lag");
	r.h.mesh.refreshWidths();
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
// registry sound.
TEST(PatchableMesh, MisuseIsRefusedInRelease) {
	Harness h;
	h.add(bounds(rect(0, 0, 10000, 10000)));
	const RingId obstacle = h.add(blocked(rect(4000, 4000, 4500, 4500)));
	PatchEdits	 twice;
	twice.removes = {obstacle, obstacle};
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
}

// A bounds outline as wide as kMaxGroundExtentMm is refused, added or moved to, and the
// mesh says so from then on.
TEST(PatchableMesh, GroundOutlineTooWideForExactOrderingIsRefused) {
	Harness		 h;
	const RingId square = h.add(bounds(rect(0, 0, 10000, 10000)));
	ASSERT_TRUE(matchesFresh(h.mesh, h.model));
	PatchEdits wide;
	wide.adds.push_back(bounds(rect(0, 20000, kMaxGroundExtentMm, 30000)));
	wide.moves.push_back({square, rect(0, 0, 10000, kMaxGroundExtentMm)});
	EXPECT_DEBUG_DEATH(
		{
			const PatchReport report = h.mesh.applyPatch(wide);
			EXPECT_EQ(report.added, std::vector<RingId>{kNoRing});
			EXPECT_EQ(report.refusedRings, 2);
			EXPECT_EQ(h.mesh.ring(square).polygon.ring, rect(0, 0, 10000, 10000));
		},
		"too wide for exact ordering");
#ifdef NDEBUG
	EXPECT_NE(h.mesh.validate().find("refused"), std::string::npos);
#endif
}

// A goal ringed by placed obstacles (0.6 m squares every 0.55 m round a 3 m circle) in
// wide open ground is out of reach.
TEST(PatchableMesh, RingOfPlacedObstaclesSealsAGoal) {
	const Vec2i64 goal{20000, 20000};
	PatchableMesh mesh;
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
	EXPECT_FALSE(pathThrough(mesh.view(), {10000, 20000}, goal, 300).reachable);
}

namespace {

	// 2 x 2 squares 12 m across.
	std::vector<NavInputPolygon> twoByTwoSquares() {
		std::vector<NavInputPolygon> out;
		for (std::int64_t j = 0; j < 2; ++j) {
			for (std::int64_t i = 0; i < 2; ++i) {
				out.push_back(bounds(rect(i * 12000, j * 12000, (i + 1) * 12000, (j + 1) * 12000)));
			}
		}
		return out;
	}

	// Right-trapezoid posts (never cyclic) on a nudged 3.2 m lattice over the 2 x 2
	// squares; some straddle a shared side.
	std::vector<NavInputPolygon> postsAcrossSides(std::uint32_t seed) {
		std::mt19937				 rng(seed);
		std::vector<NavInputPolygon> out;
		for (std::int64_t j = 0; j < 7; ++j) {
			for (std::int64_t i = 0; i < 7; ++i) {
				const std::int64_t x = 1500 + i * 3200 + between(rng, 0, 600);
				const std::int64_t y = 1500 + j * 3200 + between(rng, 0, 600);
				out.push_back(blocked({{x, y}, {x + 1000, y}, {x + 1000, y + 1000}, {x + 200, y + 1000}}));
			}
		}
		return out;
	}

	// The final mesh with every ring named by its place in the input instead of its id.
	struct NamedMesh {
		std::set<Vec2i64>										  vertices;
		std::map<EdgeKey, std::vector<std::pair<int, std::int32_t>>> constrained;
		std::map<Corners, std::vector<int>>						  covers; // per triangle, kGroundToken kept
	};

	NamedMesh named(const PatchableMesh& mesh, const std::map<RingId, int>& nameOf) {
		NamedMesh out;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (!mesh.triangleLive(t)) {
				continue;
			}
			const NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
			for (int e = 0; e < 3; ++e) {
				const Vec2i64& a = mesh.vertices()[tri.v[static_cast<std::size_t>(e)]];
				out.vertices.insert(a);
				const std::vector<EdgeSource>& sources = mesh.edgeSources(t, e);
				if (sources.empty()) {
					continue;
				}
				std::vector<std::pair<int, std::int32_t>> names;
				for (const EdgeSource& s : sources) {
					names.push_back({nameOf.at(s.ring), s.edge});
				}
				std::sort(names.begin(), names.end());
				out.constrained[edgeKey(a, mesh.vertices()[tri.v[static_cast<std::size_t>((e + 1) % 3)]])] = names;
			}
			std::vector<int> cover;
			for (RingId r : mesh.cover(t)) {
				cover.push_back(r == kGroundToken ? kGroundToken : nameOf.at(r));
			}
			std::sort(cover.begin(), cover.end());
			out.covers[cornersOf(mesh, t)] = cover;
		}
		return out;
	}

} // namespace

// 2 x 2 squares arriving in many orders, posts added between arrivals: the same
// vertices, constrained edges, covers, and (the posts leave no free diagonal) triangles.
TEST(PatchableMesh, SquaresInAnyOrderMeshAlike) {
	const std::vector<NavInputPolygon> squares = twoByTwoSquares();
	const std::vector<NavInputPolygon> posts   = postsAcrossSides(31);
	std::mt19937					   rng(43);
	NamedMesh						   first;
	for (int run = 0; run < 8; ++run) {
		std::vector<int> order{0, 1, 2, 3};
		shuffle(order, rng);
		std::vector<int> postOrder(posts.size());
		for (std::size_t i = 0; i < posts.size(); ++i) {
			postOrder[i] = static_cast<int>(i);
		}
		shuffle(postOrder, rng);
		Harness				h;
		std::map<RingId, int> nameOf;
		std::size_t			nextPost = 0;
		for (std::size_t k = 0; k < order.size(); ++k) {
			nameOf[h.add(squares[static_cast<std::size_t>(order[k])])] = order[k];
			PatchEdits batch;
			std::vector<int> names;
			const std::size_t end = posts.size() * (k + 1) / order.size();
			for (; nextPost < end; ++nextPost) {
				batch.adds.push_back(posts[static_cast<std::size_t>(postOrder[nextPost])]);
				names.push_back(4 + postOrder[nextPost]);
			}
			const PatchReport report = h.apply(batch);
			for (std::size_t i = 0; i < report.added.size(); ++i) {
				nameOf[report.added[i]] = names[i];
			}
		}
		ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "run " << run;
		ASSERT_EQ(freeDiagonals(h.mesh), 0) << "the posts must leave the triangulation no choice";
		const NamedMesh mesh = named(h.mesh, nameOf);
		if (run == 0) {
			first = mesh;
			continue;
		}
		EXPECT_TRUE(mesh.vertices == first.vertices) << "run " << run;
		EXPECT_TRUE(mesh.constrained == first.constrained) << "run " << run;
		EXPECT_TRUE(mesh.covers == first.covers) << "run " << run;
	}
}

namespace {

	// A triangle, a water body, or a box over the 2 x 2 squares and just past them.
	NavInputPolygon boxObstacle(std::mt19937& rng) {
		const std::int64_t x = between(rng, -1000, 24000);
		const std::int64_t y = between(rng, -1000, 24000);
		if (pick(rng, 3) == 0) {
			return blocked({{x, y}, {x + between(rng, 300, 3000), y + between(rng, -1500, 1500)}, {x + between(rng, -1500, 1500), y + between(rng, 300, 3000)}});
		}
		// Height is drawn before width: argument evaluation order is unspecified, and the pinned histories assume this one.
		if (pick(rng, 10) == 0) {
			const std::int64_t h = between(rng, 1000, 5000);
			const std::int64_t w = between(rng, 1000, 5000);
			return blocked(rect(x, y, x + w, y + h), kWater, true);
		}
		const std::int64_t h = between(rng, 200, 3000);
		const std::int64_t w = between(rng, 200, 3000);
		return blocked(rect(x, y, x + w, y + h));
	}

	// Squares arriving among obstacles, leaving, and coming back, the obstacles added,
	// removed, and moved around them, many of them across a shared side; every state
	// checked against a from-scratch build. `fingerprint`, when given, takes an FNV-style hash
	// (a word at a time) of every edit, so a replay can tell that it still meets the history
	// it was written for.
	template <typename MakeObstacle>
	void runSquaresAmongObstacles(std::uint32_t seed, int steps, const std::vector<NavInputPolygon>& squares, MakeObstacle obstacle,
								  std::uint64_t* fingerprint = nullptr) {
		std::mt19937		rng(seed);
		Harness				h;
		std::vector<RingId> squareIds(squares.size(), kNoRing);
		std::vector<int>	arrival(squares.size());
		for (int& step : arrival) {
			step = static_cast<int>(pick(rng, static_cast<std::uint64_t>(steps / 2)));
		}
		std::vector<RingId> placed;
		for (int step = 0; step < steps; ++step) {
			PatchEdits		   edits;
			std::vector<std::size_t> arriving;
			for (std::size_t s = 0; s < squares.size(); ++s) {
				if (squareIds[s] == kNoRing && (arrival[s] == step || (arrival[s] < step && pick(rng, 4) == 0))) {
					edits.adds.push_back(squares[s]);
					arriving.push_back(s);
				} else if (squareIds[s] != kNoRing && pick(rng, 15) == 0) {
					edits.removes.push_back(squareIds[s]);
					squareIds[s] = kNoRing;
				}
			}
			const std::size_t squaresAdded = edits.adds.size();
			const unsigned	  op		   = static_cast<unsigned>(pick(rng, 10));
			if (op < 6 || placed.size() < 4) {
				const std::size_t count = 1 + pick(rng, 5);
				for (std::size_t i = 0; i < count; ++i) {
					edits.adds.push_back(obstacle(rng));
				}
			} else if (op < 8) {
				const std::size_t at = pick(rng, placed.size());
				edits.removes.push_back(placed[at]);
				placed.erase(placed.begin() + static_cast<std::ptrdiff_t>(at));
			} else {
				const RingId r = placed[pick(rng, placed.size())];
				edits.moves.push_back({r, translated(h.model[r].ring, {between(rng, -2000, 2000), between(rng, -2000, 2000)})});
			}
			if (fingerprint != nullptr) {
				auto mix = [&](std::int64_t value) {
					*fingerprint = (*fingerprint ^ static_cast<std::uint64_t>(value)) * 0x100000001B3ULL;
				};
				for (const NavInputPolygon& add : edits.adds) {
					for (const Vec2i64& p : add.ring) {
						mix(p.x);
						mix(p.y);
					}
				}
				for (const RingId r : edits.removes) {
					mix(r);
				}
				for (const RingMove& move : edits.moves) {
					mix(move.ring);
					for (const Vec2i64& p : move.outline) {
						mix(p.x);
						mix(p.y);
					}
				}
			}
			const PatchReport report = h.apply(edits);
			for (std::size_t i = 0; i < report.added.size(); ++i) {
				if (i < squaresAdded) {
					squareIds[arriving[i]] = report.added[i];
				} else {
					placed.push_back(report.added[i]);
				}
			}
			ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "seed " << seed << " step " << step << describe(h.model);
			ASSERT_TRUE(agreesWithTheRingsAsGiven(h.mesh, h.model)) << "seed " << seed << " step " << step << describe(h.model);
		}
	}

} // namespace

TEST(PatchableMesh, SquaresArriveAmongObstaclesMatchFreshBuild) {
	for (std::uint32_t seed = 1; seed <= 6; ++seed) {
		runSquaresAmongObstacles(seed, 40, twoByTwoSquares(), boxObstacle);
		if (HasFatalFailure()) {
			return;
		}
	}
}

// A placed obstacle's corner lies just under a blocked edge's rounded piece: the edge as
// given crosses the obstacle there, so a fresh build splits it at the corner, though the
// piece passes clear of every new side.
TEST(PatchableMesh, CornerUnderARoundedPieceSplitsTheEdge) {
	Harness h;
	PatchEdits first;
	first.adds = {bounds(rect(0, 0, 12000, 24000)), blocked(rect(4999, 11316, 6846, 14288)), blocked({{3503, 13127}, {5164, 12335}, {3679, 14963}})};
	h.apply(first);
	ASSERT_TRUE(matchesFresh(h.mesh, h.model));
	const Vec2i64 corner{4858, 12481};
	ASSERT_EQ(liveVerticesOn(h.mesh, {3503, 13127}, {4999, 12414}), (std::vector<Vec2i64>{{3503, 13127}, {4999, 12414}}))
		<< "the edge is bent where it crosses the rectangle, so its piece runs above the corner";
	h.add(blocked(rect(4650, 10316, corner.x, corner.y)));
	ASSERT_TRUE(matchesFresh(h.mesh, h.model));
	EXPECT_EQ(liveVerticesOn(h.mesh, {3503, 13127}, corner), (std::vector<Vec2i64>{{3503, 13127}, corner}));
}

namespace {

	// Where the arrangement's first round can split ab against cd, under either argument
	// order's rounding: a vertex of ab's chain outside every such set came from a later
	// round.
	void firstRoundCuts(const Vec2i64& a, const Vec2i64& b, const Vec2i64& c, const Vec2i64& d, std::vector<Vec2i64>& out) {
		auto onto = [&](const Vec2i64& p) {
			return Vec2i64{std::clamp(p.x, std::min(a.x, b.x), std::max(a.x, b.x)), std::clamp(p.y, std::min(a.y, b.y), std::max(a.y, b.y))};
		};
		const geometry::SegmentIntersection r = geometry::intersectSegments(a, b, c, d);
		switch (r.relation) {
			case geometry::SegmentRelation::Disjoint:
				return;
			case geometry::SegmentRelation::ProperCrossing:
				out.push_back(onto(r.point));
				out.push_back(onto(geometry::intersectSegments(c, d, a, b).point));
				return;
			case geometry::SegmentRelation::EndpointTouch:
				out.push_back(r.point);
				return;
			case geometry::SegmentRelation::CollinearOverlap:
				out.push_back(r.overlapStart);
				out.push_back(r.overlapEnd);
				return;
		}
	}

	// The smallest blocked ring along ab: a triangle whose third corner is b moved by a
	// unit step not along ab, so its two long edges leave a side by side.
	NavInputPolygon thinRing(const Vec2i64& a, const Vec2i64& b, const Vec2i64& step) {
		const Vec2i64 c = b + step;
		if (geometry::orientation(a, b, c) == geometry::Orientation::Clockwise) {
			return blocked({a, c, b});
		}
		return blocked({a, b, c});
	}

	Vec2i64 unitStepOff(std::mt19937& rng, const Vec2i64& along) {
		const std::array<Vec2i64, 4> steps{Vec2i64{1, 0}, Vec2i64{0, 1}, Vec2i64{-1, 0}, Vec2i64{0, -1}};
		for (;;) {
			const Vec2i64 s = steps[pick(rng, steps.size())];
			if (cross(along, s).sign() != 0) {
				return s;
			}
		}
	}

	// Lattice points within a millimetre both of a blocked edge as given and of its chain
	// on the mesh, off the chain, for every edge whose chain holds a vertex no first-round
	// cut of its input explains: the thin region between an edge's input, its earlier
	// pieces, and its final ones, where a later round bent it.
	std::vector<Vec2i64> laterRoundSlivers(const PatchableMesh& mesh, const Model& model) {
		std::vector<std::array<Vec2i64, 2>> inputs;
		for (const auto& [id, poly] : model) {
			for (std::size_t i = 0; i < poly.ring.size(); ++i) {
				inputs.push_back({poly.ring[i], poly.ring[(i + 1) % poly.ring.size()]});
			}
		}
		std::set<Vec2i64> out;
		for (const auto& [id, poly] : model) {
			if (!poly.blocked) {
				continue;
			}
			for (std::int32_t e = 0; e < static_cast<std::int32_t>(poly.ring.size()); ++e) {
				const Vec2i64						a = poly.ring[static_cast<std::size_t>(e)];
				const Vec2i64						b = poly.ring[(static_cast<std::size_t>(e) + 1) % poly.ring.size()];
				std::vector<std::array<Vec2i64, 2>> chain;
				std::set<Vec2i64>					chainVertices;
				for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
					if (!mesh.triangleLive(t)) {
						continue;
					}
					const NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
					for (int k = 0; k < 3; ++k) {
						const std::vector<EdgeSource>& sources = mesh.edgeSources(t, k);
						if (std::find(sources.begin(), sources.end(), EdgeSource{id, e}) != sources.end()) {
							const Vec2i64 p = mesh.vertices()[tri.v[static_cast<std::size_t>(k)]];
							const Vec2i64 q = mesh.vertices()[tri.v[static_cast<std::size_t>((k + 1) % 3)]];
							chain.push_back({p, q});
							chainVertices.insert(p);
							chainVertices.insert(q);
						}
					}
				}
				std::vector<Vec2i64> cuts{a, b};
				for (const std::array<Vec2i64, 2>& other : inputs) {
					if (!(other[0] == a && other[1] == b)) {
						firstRoundCuts(a, b, other[0], other[1], cuts);
					}
				}
				const bool later = std::any_of(chainVertices.begin(), chainVertices.end(),
											   [&](const Vec2i64& v) { return std::find(cuts.begin(), cuts.end(), v) == cuts.end(); });
				if (!later) {
					continue;
				}
				for (std::int64_t y = std::min(a.y, b.y) - 1; y <= std::max(a.y, b.y) + 1; ++y) {
					for (std::int64_t x = std::min(a.x, b.x) - 1; x <= std::max(a.x, b.x) + 1; ++x) {
						const Vec2i64 p{x, y};
						if (chainVertices.count(p) != 0 || !geometry::withinDistanceOfSegment(p, a, b, 1)) {
							continue;
						}
						bool nearChain = false;
						bool onChain   = false;
						for (const std::array<Vec2i64, 2>& piece : chain) {
							nearChain = nearChain || geometry::withinDistanceOfSegment(p, piece[0], piece[1], 1);
							onChain	  = onChain || geometry::withinDistanceOfSegment(p, piece[0], piece[1], 0);
						}
						if (nearChain && !onChain) {
							out.insert(p);
						}
					}
				}
			}
		}
		return {out.begin(), out.end()};
	}

	struct CascadeProbe {
		std::int64_t side	   = 40;
		int			 segments  = 3;
		int			 farEnds   = 0; // per sliver point; 0 takes every lattice point of the box
		std::int64_t bases	   = 0;
		std::int64_t bentLater = 0; // bases with an edge a later round bent
		std::int64_t cases	   = 0; // added segments, each checked added and removed again
	};

	// `segments` thin blocked rings at random over one ground square, in one patch, so the
	// mesh is the from-scratch arrangement; then, for every lattice point in the thin region
	// beside an edge a later arrangement round bent, a thin ring from it to far ends in the
	// box, added and removed again, each state against a from-scratch build.
	void probeCascade(std::uint32_t seed, CascadeProbe& probe) {
		std::mt19937 rng(seed);
		Harness		 base;
		PatchEdits	 edits;
		edits.adds.push_back(bounds(rect(0, 0, probe.side, probe.side)));
		for (int i = 0; i < probe.segments; ++i) {
			const Vec2i64 a{between(rng, 0, probe.side), between(rng, 0, probe.side)};
			Vec2i64		  b = a;
			while (b == a) {
				b = {between(rng, 0, probe.side), between(rng, 0, probe.side)};
			}
			edits.adds.push_back(thinRing(a, b, unitStepOff(rng, b - a)));
		}
		base.apply(edits);
		++probe.bases;
		ASSERT_TRUE(matchesFresh(base.mesh, base.model)) << "seed " << seed << " base" << describe(base.model);
		const std::vector<Vec2i64> slivers = laterRoundSlivers(base.mesh, base.model);
		if (slivers.empty()) {
			return;
		}
		++probe.bentLater;
		auto tryFrom = [&](const Vec2i64& p, const Vec2i64& q) {
			if (p == q) {
				return;
			}
			const RingId added = base.add(thinRing(p, q, unitStepOff(rng, q - p)));
			++probe.cases;
			ASSERT_TRUE(matchesFresh(base.mesh, base.model)) << "seed " << seed << " added" << describe(base.model);
			PatchEdits leave;
			leave.removes.push_back(added);
			base.apply(leave);
			ASSERT_TRUE(matchesFresh(base.mesh, base.model)) << "seed " << seed << " removed again" << describe(base.model);
		};
		for (const Vec2i64& p : slivers) {
			if (probe.farEnds == 0) {
				for (std::int64_t y = 0; y <= probe.side && !::testing::Test::HasFatalFailure(); ++y) {
					for (std::int64_t x = 0; x <= probe.side && !::testing::Test::HasFatalFailure(); ++x) {
						tryFrom(p, {x, y});
					}
				}
			} else {
				for (int i = 0; i < probe.farEnds && !::testing::Test::HasFatalFailure(); ++i) {
					tryFrom(p, {between(rng, 0, probe.side), between(rng, 0, probe.side)});
				}
			}
			if (::testing::Test::HasFatalFailure()) {
				return;
			}
		}
	}

} // namespace

// Rounding cascades among a few thin blocked rings: a segment added with one end in the
// sub-millimetre region between a bent edge's input and its pieces, where the from-scratch
// build re-runs that edge's rounds from its input and the patch keeps its final pieces.
// Raise the counts for a heavier sweep.
TEST(PatchableMesh, SegmentEndingBesideALaterBendMatchesFreshBuild) {
	constexpr std::uint32_t kSeeds	 = 20;
	constexpr int			kFarEnds = 2;
	for (const auto& [side, segments] : {std::pair{40, 4}, std::pair{60, 4}}) {
		CascadeProbe probe;
		probe.side	   = side;
		probe.segments = segments;
		probe.farEnds  = kFarEnds;
		for (std::uint32_t seed = 1; seed <= kSeeds; ++seed) {
			probeCascade(seed, probe);
			if (HasFatalFailure()) {
				return;
			}
		}
		EXPECT_GT(probe.bentLater, 0) << side << " mm: no base had an edge a later round bent";
	}
}

// A blocked edge bent in three rounds, (23, 4)-(23, 6)-(24, 6)-(25, 10)-(40, 56), its last
// piece passing just left of (26, 13); a thin ring added from there crosses neither the
// edge as given nor its final pieces, but a fresh build re-runs the edge's rounds and an
// earlier piece of it meets the ring's edges, which it splits at (27, 14).
TEST(PatchableMesh, SegmentFromBesideALaterBendMatchesFreshBuild) {
	Harness	   h;
	PatchEdits base;
	base.adds = {bounds(rect(0, 0, 60, 60)), blocked({{21, 1}, {47, 48}, {47, 49}}), blocked({{40, 56}, {22, 4}, {23, 4}}),
				 blocked({{11, 37}, {17, 43}, {16, 43}}), blocked({{12, 35}, {31, 32}, {32, 32}})};
	h.apply(base);
	ASSERT_TRUE(matchesFresh(h.mesh, h.model));
	ASSERT_EQ(liveVerticesOn(h.mesh, {25, 10}, {40, 56}), (std::vector<Vec2i64>{{25, 10}, {40, 56}})) << "the edge's last piece runs clear of (26, 13)";
	const RingId added = h.add(blocked({{26, 13}, {41, 41}, {40, 41}}));
	ASSERT_TRUE(matchesFresh(h.mesh, h.model));
	EXPECT_EQ(liveVerticesOn(h.mesh, {25, 10}, {27, 14}), (std::vector<Vec2i64>{{25, 10}, {27, 14}}));
	PatchEdits leave;
	leave.removes.push_back(added);
	h.apply(leave);
	EXPECT_TRUE(matchesFresh(h.mesh, h.model));
}

// Two edges of one thin ring, meeting only at its corner (18, 21), that rounding lays on one
// line through (28, 24) where their pieces met in a later round: a split with nothing else
// there, which validate takes as two edges off one line both passing near it.
TEST(PatchableMesh, NearlyParallelEdgesLaidOnOneLineKeepTheirSplit) {
	Harness	   h;
	PatchEdits all;
	all.adds = {bounds(rect(0, 0, 40, 40)), blocked({{18, 21}, {40, 26}, {39, 26}}), blocked({{27, 25}, {24, 17}, {25, 17}}),
				blocked({{40, 23}, {19, 10}, {19, 9}}), blocked({{29, 24}, {36, 20}, {36, 21}})};
	const PatchReport report = h.apply(all);
	ASSERT_TRUE(matchesFresh(h.mesh, h.model));
	const RingId thin = report.added[1];
	EXPECT_EQ(sourcesBetween(h.mesh, {27, 23}, {28, 24}), (std::vector<EdgeSource>{{thin, 0}, {thin, 2}}));
	EXPECT_EQ(sourcesBetween(h.mesh, {28, 24}, {29, 24}), (std::vector<EdgeSource>{{thin, 0}, {thin, 2}}));
}


namespace {

	// Obstacles over the 2 x 2 squares, some crossing a shared side, some with a corner on
	// the middle corner or on a point where the side is cut.
	std::vector<NavInputPolygon> obstaclesAtCorners() {
		return {
			blocked({{12000, 12000}, {13000, 12500}, {12500, 13000}}),
			blocked({{11000, 11500}, {12000, 12000}, {11500, 13000}}),
			blocked({{10500, 3000}, {12000, 4000}, {11000, 5000}}),
			blocked(rect(11000, 7000, 13000, 7500)),
			blocked({{4000, 11000}, {6000, 12000}, {5000, 13500}}),
		};
	}

} // namespace

// A square taken out and an identical one added in the same patch: its sides' pairs end
// and form again, and the mesh is the from-scratch build's.
TEST(PatchableMesh, SquareReplacedInOnePatchMatchesFreshBuild) {
	for (const bool withObstacles : {false, true}) {
		Harness								h;
		const std::vector<NavInputPolygon> squares = twoByTwoSquares();
		std::vector<RingId>					ids;
		for (const NavInputPolygon& square : squares) {
			ids.push_back(h.add(square));
		}
		if (withObstacles) {
			PatchEdits obstacles;
			obstacles.adds = obstaclesAtCorners();
			h.apply(obstacles);
		}
		ASSERT_TRUE(matchesFresh(h.mesh, h.model));
		for (std::size_t s = 0; s < squares.size(); ++s) {
			PatchEdits replace;
			replace.removes.push_back(ids[s]);
			replace.adds.push_back(squares[s]);
			ids[s] = h.apply(replace).added.front();
			ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "square " << s << (withObstacles ? " among obstacles" : "");
		}
	}
}

// Two adjacent squares taken out in one patch while a third, and with it the fourth or
// not, shares the corner between them: what stays ends its sides there, so the corner
// is a vertex again.
TEST(PatchableMesh, TwoSquaresLeaveBesideASharedCorner) {
	for (const bool fourth : {false, true}) {
		for (const bool withObstacles : {false, true}) {
			Harness								h;
			const std::vector<NavInputPolygon> squares = twoByTwoSquares();
			std::vector<RingId>					ids;
			for (std::size_t s = 0; s < (fourth ? 4U : 3U); ++s) {
				ids.push_back(h.add(squares[s]));
			}
			if (withObstacles) {
				PatchEdits obstacles;
				obstacles.adds = obstaclesAtCorners();
				h.apply(obstacles);
			}
			ASSERT_TRUE(matchesFresh(h.mesh, h.model));
			PatchEdits leave;
			leave.removes = {ids[0], ids[1]};
			h.apply(leave);
			ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << (fourth ? "four" : "three") << (withObstacles ? " among obstacles" : "");
			EXPECT_EQ(liveVerticesOn(h.mesh, {12000, 12000}, {12000, 12000}).size(), 1U);
			PatchEdits back;
			back.adds = {squares[0], squares[1]};
			h.apply(back);
			ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "back";
		}
	}
}

// The corner the 2 x 2 squares share is a vertex while any square is missing (it ends a
// side of what is there) and not once all four are, in every arrival order and with all
// four arriving in one patch; it comes back when any one square leaves. Nor are the
// points the shared sides are cut at.
TEST(PatchableMesh, FourWayCornerIsAVertexOnlyWhileASquareIsMissing) {
	const std::vector<NavInputPolygon> squares = twoByTwoSquares();
	const Vec2i64					   centre{12000, 12000};
	auto expectOpenCross = [&](const Harness& h, const std::string& when) {
		EXPECT_EQ(liveVerticesOn(h.mesh, {12000, 0}, {12000, 24000}), (std::vector<Vec2i64>{{12000, 0}, {12000, 24000}})) << when;
		EXPECT_EQ(liveVerticesOn(h.mesh, {0, 12000}, {24000, 12000}), (std::vector<Vec2i64>{{0, 12000}, {24000, 12000}})) << when;
	};
	std::vector<int> order{0, 1, 2, 3};
	do {
		Harness				h;
		std::vector<RingId> ids(squares.size(), kNoRing);
		std::string			when	= "order";
		std::size_t			present = 0;
		for (const int s : order) {
			when += " " + std::to_string(s);
			ids[static_cast<std::size_t>(s)] = h.add(squares[static_cast<std::size_t>(s)]);
			ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << when;
			++present;
			EXPECT_EQ(liveVerticesOn(h.mesh, centre, centre).size(), present == squares.size() ? 0U : 1U) << when;
		}
		expectOpenCross(h, when);
		for (std::size_t s = 0; s < squares.size(); ++s) {
			PatchEdits leave;
			leave.removes.push_back(ids[s]);
			h.apply(leave);
			ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << when << ", square " << s << " left";
			EXPECT_EQ(liveVerticesOn(h.mesh, centre, centre).size(), 1U) << when << ", square " << s << " left";
			ids[s] = h.add(squares[s]);
			ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << when << ", square " << s << " back";
			expectOpenCross(h, when + ", square " + std::to_string(s) + " back");
		}
	} while (std::next_permutation(order.begin(), order.end()));
	Harness	   h;
	PatchEdits together;
	together.adds = squares;
	h.apply(together);
	ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "one patch";
	expectOpenCross(h, "one patch");
}

// Obstacles with a corner exactly on the four-way corner or on a point where a shared
// side is cut, arriving before, between, and after the squares, then leaving. Those
// corners are vertices because they are the obstacles'; the obstacles' edges cross the
// dissolved sides without a vertex, and once they leave the shared sides hold none.
TEST(PatchableMesh, ObstacleCornerOnAFourWayCorner) {
	const std::vector<NavInputPolygon> squares	 = twoByTwoSquares();
	const std::vector<NavInputPolygon> obstacles = obstaclesAtCorners();
	for (std::size_t obstaclesAfter = 0; obstaclesAfter <= squares.size(); ++obstaclesAfter) {
		Harness				h;
		std::vector<RingId> placed;
		for (std::size_t s = 0; s <= squares.size(); ++s) {
			if (s == obstaclesAfter) {
				PatchEdits edits;
				edits.adds = obstacles;
				placed	   = h.apply(edits).added;
				ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "obstacles after " << obstaclesAfter << " squares";
			}
			if (s < squares.size()) {
				h.add(squares[s]);
				ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "square " << s << ", obstacles after " << obstaclesAfter;
			}
		}
		EXPECT_EQ(liveVerticesOn(h.mesh, {12000, 0}, {12000, 24000}),
				  (std::vector<Vec2i64>{{12000, 0}, {12000, 4000}, {12000, 12000}, {12000, 24000}}))
			<< "obstacles after " << obstaclesAfter;
		EXPECT_EQ(liveVerticesOn(h.mesh, {0, 12000}, {24000, 12000}),
				  (std::vector<Vec2i64>{{0, 12000}, {6000, 12000}, {12000, 12000}, {24000, 12000}}))
			<< "obstacles after " << obstaclesAfter;
		for (const RingId r : placed) {
			PatchEdits leave;
			leave.removes.push_back(r);
			h.apply(leave);
			ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "obstacle " << r << " left, obstacles after " << obstaclesAfter;
		}
		EXPECT_EQ(liveVerticesOn(h.mesh, {12000, 0}, {12000, 24000}), (std::vector<Vec2i64>{{12000, 0}, {12000, 24000}}))
			<< "obstacles after " << obstaclesAfter;
	}
}

namespace {

	// Two quadrilaterals sharing the slanted side (12000, 0)-(14000, 12000).
	std::vector<NavInputPolygon> slantedPair() {
		return {bounds({{0, 0}, {12000, 0}, {14000, 12000}, {0, 12000}}), bounds({{12000, 0}, {24000, 0}, {24000, 12000}, {14000, 12000}})};
	}

	// 2 x 2 sheared squares: corner (i, j) at (12000 i + 3000 j, 1000 i + 12000 j), so no
	// side is axis-aligned. Their sides cut to 4 m land on lattice points.
	std::vector<NavInputPolygon> shearedSquares() {
		auto corner = [](std::int64_t i, std::int64_t j) { return Vec2i64{12000 * i + 3000 * j, 1000 * i + 12000 * j}; };
		std::vector<NavInputPolygon> out;
		for (std::int64_t j = 0; j < 2; ++j) {
			for (std::int64_t i = 0; i < 2; ++i) {
				out.push_back(bounds({corner(i, j), corner(i + 1, j), corner(i + 1, j + 1), corner(i, j + 1)}));
			}
		}
		return out;
	}

	// a / b rounded to the nearest integer, halves up; b > 0.
	std::int64_t roundedDiv(std::int64_t a, std::int64_t b) {
		const std::int64_t n = 2 * a + b;
		const std::int64_t d = 2 * b;
		return n / d - (n % d < 0 ? 1 : 0);
	}

	// The integer square root of n >= 0, rounded down.
	std::int64_t floorSqrt(std::int64_t n) {
		std::int64_t r = static_cast<std::int64_t>(std::sqrt(static_cast<double>(n)));
		while (r > 0 && r * r > n) {
			--r;
		}
		while ((r + 1) * (r + 1) <= n) {
			++r;
		}
		return r;
	}

	// A thin quadrilateral lying along a random side of `squares` at a slope of 0.009 to
	// 0.14 (about 0.5 to 8 degrees) to it, so its long edges cross the side at a shallow
	// angle; otherwise a box obstacle. Integer arithmetic only, so every toolchain draws
	// the same.
	NavInputPolygon shallowObstacle(std::mt19937& rng, const std::vector<NavInputPolygon>& squares) {
		if (pick(rng, 4) == 0) {
			return boxObstacle(rng);
		}
		const std::vector<Vec2i64>& ring	  = squares[pick(rng, squares.size())].ring;
		const std::size_t			e		  = pick(rng, ring.size());
		const Vec2i64				p		  = ring[e];
		const Vec2i64				d		  = ring[(e + 1) % ring.size()] - p;
		const std::int64_t			alongSide = between(rng, 50, 950); // thousandths of the side
		const std::int64_t			slope	  = between(rng, 9, 140);  // thousandths
		const std::int64_t			sign	  = pick(rng, 2) == 0 ? -1 : 1;
		const std::int64_t			half	  = between(rng, 500, 3000);
		const std::int64_t			width	  = between(rng, 60, 600);
		const std::int64_t			offset	  = between(rng, -400, 400);
		// The obstacle's long axis, r / |r|, is the side turned by the slope.
		const Vec2i64	   r{1000 * d.x - sign * slope * d.y, 1000 * d.y + sign * slope * d.x};
		const std::int64_t norm = floorSqrt(r.x * r.x + r.y * r.y);
		// The side's point at alongSide, then `along` up the axis and `across` off it.
		auto at = [&](std::int64_t along, std::int64_t across) {
			return Vec2i64{p.x + roundedDiv(d.x * alongSide * norm + 1000 * (r.x * along - r.y * (across + offset)), 1000 * norm),
						   p.y + roundedDiv(d.y * alongSide * norm + 1000 * (r.y * along + r.x * (across + offset)), 1000 * norm)};
		};
		return blocked({at(-half, 0), at(half, 0), at(half, width), at(-half, width)});
	}

} // namespace

// The slanted shared side: every point inside the pair's outline, the side and its ends
// included, is ground in exactly one ring, whichever way each ring runs.
TEST(PatchableMesh, BoundsContainSampleHoldsASlantedSideOnce) {
	const std::vector<Vec2i64> a{{0, 0}, {4, 0}, {5, 8}, {0, 8}};
	const std::vector<Vec2i64> b{{4, 0}, {8, 0}, {8, 8}, {5, 8}};
	for (const bool reverseB : {false, true}) {
		const SampleRing ra = sampleRing(bounds(a));
		const SampleRing rb = sampleRing(bounds(reverseB ? std::vector<Vec2i64>(b.rbegin(), b.rend()) : b));
		int				 onSide = 0;
		for (std::int64_t y = -3; y <= 27; ++y) {
			for (std::int64_t x = -3; x <= 27; ++x) {
				const int holding = (boundsContainSample(ra, {x, y}) ? 1 : 0) + (boundsContainSample(rb, {x, y}) ? 1 : 0);
				onSide += 8 * (x - 12) == y && y > 0 && y < 24 ? 1 : 0;
				if (x > 0 && x < 24 && y > 0 && y < 24) {
					EXPECT_EQ(holding, 1) << "(" << x << ", " << y << ") in 3x space";
				} else {
					EXPECT_LE(holding, 1) << "(" << x << ", " << y << ") in 3x space";
				}
			}
		}
		EXPECT_EQ(onSide, 2) << "points on the slanted side itself were probed";
	}
}

// Rings sharing a slanted side, among obstacles crossing it, arriving in both orders,
// one leaving and coming back, and replaced in one patch.
TEST(PatchableMesh, SlantedSharedSideMatchesFreshBuild) {
	const std::vector<NavInputPolygon> pair = slantedPair();
	const std::vector<NavInputPolygon> obstacles{
		blocked({{12200, 1500}, {13500, 1900}, {13600, 2300}, {12300, 1900}}),
		blocked({{12800, 6000}, {13200, 5000}, {13400, 5600}}),
		blocked(rect(13000, 8000, 14000, 8600)),
		blocked({{12500, 3000}, {13200, 3100}, {12600, 3600}}),
	};
	for (const bool firstOrder : {true, false}) {
		Harness		 h;
		h.add(pair[firstOrder ? 0 : 1]);
		PatchEdits edits;
		edits.adds = obstacles;
		h.apply(edits);
		RingId second = h.add(pair[firstOrder ? 1 : 0]);
		ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "both";
		int boundsOnSide = 0;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(h.mesh.triangles().size()); ++t) {
			if (!h.mesh.triangleLive(t)) {
				continue;
			}
			const NavTriangle& tri = h.mesh.triangles()[static_cast<std::size_t>(t)];
			for (int e = 0; e < 3; ++e) {
				const Vec2i64& a = h.mesh.vertices()[tri.v[static_cast<std::size_t>(e)]];
				const Vec2i64& b = h.mesh.vertices()[tri.v[static_cast<std::size_t>((e + 1) % 3)]];
				if (cross(Vec2i64{2000, 12000}, a - Vec2i64{12000, 0}).sign() != 0 ||
					cross(Vec2i64{2000, 12000}, b - Vec2i64{12000, 0}).sign() != 0) {
					continue;
				}
				for (const EdgeSource& s : h.mesh.edgeSources(t, e)) {
					boundsOnSide += h.model.at(s.ring).blocked ? 0 : 1;
				}
			}
		}
		EXPECT_EQ(boundsOnSide, 0) << "the slanted side is dissolved";
		PatchEdits leave;
		leave.removes.push_back(second);
		h.apply(leave);
		ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "one left";
		second = h.add(pair[firstOrder ? 1 : 0]);
		ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "back";
		PatchEdits replace;
		replace.removes.push_back(second);
		replace.adds.push_back(pair[firstOrder ? 1 : 0]);
		h.apply(replace);
		ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "replaced";
	}
}

// Sheared squares (no side axis-aligned) arriving among thin obstacles that cross their
// sides at shallow angles, so snap rounding bends the sides near the crossings; every
// state equals a from-scratch build. Raise kSeeds for a heavier sweep (400 seeds of 40
// steps held).
TEST(PatchableMesh, ShallowCrossingsOfSlantedSidesMatchFreshBuild) {
	constexpr std::uint32_t			   kSeeds  = 16;
	const std::vector<NavInputPolygon> squares = shearedSquares();
	for (std::uint32_t seed = 1; seed <= kSeeds; ++seed) {
		runSquaresAmongObstacles(seed, 40, squares, [&](std::mt19937& rng) { return shallowObstacle(rng, squares); });
		if (HasFatalFailure()) {
			return;
		}
	}
}

// The history of one seed of the sweep above, raised past its count, in which a ground
// sliver beside a bent side is reached only through another whose sample reads off ground.
// Its input is pinned by a fingerprint: a changed generator fails here instead of passing
// on a history that no longer reaches the misread sliver.
TEST(PatchableMesh, SliverReachedThroughAMisreadSliverMatchesFreshBuild) {
	const std::vector<NavInputPolygon> squares	   = shearedSquares();
	std::uint64_t					   fingerprint = 0xCBF29CE484222325ULL;
	runSquaresAmongObstacles(450, 40, squares, [&](std::mt19937& rng) { return shallowObstacle(rng, squares); }, &fingerprint);
	EXPECT_EQ(fingerprint, 2319368059839001361ULL) << "the seed's history changed: find a seed that reaches the misread sliver again";
}

// Ground a millimetre across whose every point lies within the blocked ring's snap band:
// the blocked edge rounds to the ground's base, so the rounded ground triangle is floor
// though its centroid lies inside the blocked ring as given. In one patch, and in both
// orders.
TEST(PatchableMesh, GroundWhollyInTheSnapBandMatchesFreshBuild) {
	const NavInputPolygon ground  = bounds({{0, 0}, {2, 0}, {0, 1}});
	const NavInputPolygon blocker = blocked({{-10, -10}, {20, -10}, {20, 1}, {-10, 0}});
	for (int order = 0; order < 3; ++order) {
		Harness h;
		if (order == 0) {
			PatchEdits edits;
			edits.adds = {ground, blocker};
			h.apply(edits);
		} else {
			h.add(order == 1 ? ground : blocker);
			ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "order " << order << " first ring";
			h.add(order == 1 ? blocker : ground);
		}
		ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << "order " << order;
		EXPECT_EQ(h.mesh.liveTriangleCount(), 1) << "order " << order;
	}
}

// Water hanging over unseen ground wholly round ground that arrives later, no edge of it
// near the ground: the ground comes in under the water, in either order.
TEST(PatchableMesh, GroundArrivingInsideABlockedRingMatchesFreshBuild) {
	const NavInputPolygon water	 = blocked(rect(0, 0, 30000, 30000), kWater, true);
	const NavInputPolygon ground = bounds(rect(9000, 9000, 21000, 21000));
	for (const bool waterFirst : {true, false}) {
		Harness		 h;
		const RingId first = h.add(waterFirst ? water : ground);
		ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << (waterFirst ? "water" : "ground") << " alone";
		const RingId second	 = h.add(waterFirst ? ground : water);
		const RingId waterId = waterFirst ? first : second;
		ASSERT_TRUE(matchesFresh(h.mesh, h.model)) << (waterFirst ? "water" : "ground") << " first";
		ASSERT_GT(h.mesh.liveTriangleCount(), 0);
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(h.mesh.triangles().size()); ++t) {
			if (h.mesh.triangleLive(t)) {
				const std::vector<RingId>& cover = h.mesh.cover(t);
				EXPECT_TRUE(std::find(cover.begin(), cover.end(), waterId) != cover.end()) << "triangle " << t;
			}
		}
	}
}

// --- Overlapping ground --------------------------------------------------------------

namespace {

	// Every live vertex off the ground's pieces is a corner of a blocked ring or a crossing of
	// two blocked edges; one on a ground piece is a corner of a ring, the end of a boundary
	// interval, or within 2 mm of two ring edges (a crossing, or two boundary intervals'
	// pieces rounding laid on one segment): no dissolved stretch leaves a vertex.
	::testing::AssertionResult noVertexInsideTheUnionBelongsToNothing(const PatchableMesh& mesh, const Model& model) {
		std::vector<GroundRing> ground;
		std::set<Vec2i64>		corners;
		std::set<Vec2i64>		blockedCorners;
		for (const auto& [id, poly] : model) {
			corners.insert(poly.ring.begin(), poly.ring.end());
			if (poly.blocked) {
				blockedCorners.insert(poly.ring.begin(), poly.ring.end());
			} else {
				ground.push_back(groundRing(poly.ring, id));
			}
		}
		std::set<Vec2i64>			intervalEnds;
		GroundBoundaryScratch		scratch;
		std::vector<GroundInterval> intervals;
		for (const GroundRing& g : ground) {
			for (std::int32_t e = 0; e < static_cast<std::int32_t>(g.outline->size()); ++e) {
				groundBoundaries(g, e, ground, scratch, intervals);
				for (const GroundInterval& iv : intervals) {
					intervalEnds.insert(iv.from);
					intervalEnds.insert(iv.to);
				}
			}
		}
		std::set<std::uint32_t> onGroundPiece; // the mesh boundary's pieces are all ground pieces
		std::set<std::uint32_t> used;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (!mesh.triangleLive(t)) {
				continue;
			}
			const NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
			for (int e = 0; e < 3; ++e) {
				used.insert(tri.v[static_cast<std::size_t>(e)]);
				for (const EdgeSource& z : mesh.edgeSources(t, e)) {
					if (!mesh.ring(z.ring).polygon.blocked) {
						onGroundPiece.insert(tri.v[static_cast<std::size_t>(e)]);
						onGroundPiece.insert(tri.v[static_cast<std::size_t>((e + 1) % 3)]);
					}
				}
			}
		}
		for (const std::uint32_t v : used) {
			const Vec2i64& p		  = mesh.vertices()[v];
			const bool	   groundPiece = onGroundPiece.count(v) != 0;
			if ((groundPiece && intervalEnds.count(p) != 0) || (groundPiece ? corners : blockedCorners).count(p) != 0) {
				continue;
			}
			int nearBy = 0;
			for (const auto& [id, poly] : model) {
				if (!groundPiece && !poly.blocked) {
					continue;
				}
				for (std::size_t i = 0; i < poly.ring.size() && nearBy < 2; ++i) {
					nearBy += geometry::withinDistanceOfSegment(p, poly.ring[i], poly.ring[(i + 1) % poly.ring.size()], 2) ? 1 : 0;
				}
			}
			if (nearBy < 2) {
				return ::testing::AssertionFailure() << "vertex (" << p.x << ", " << p.y << ") " << (groundPiece ? "on a ground piece" : "inside the ground")
													 << " belongs to nothing";
			}
		}
		return ::testing::AssertionSuccess();
	}

	::testing::AssertionResult overlapHolds(const PatchableMesh& mesh, const Model& model) {
		::testing::AssertionResult fresh = matchesFresh(mesh, model);
		if (!fresh) {
			return fresh;
		}
		::testing::AssertionResult given = agreesWithTheRingsAsGiven(mesh, model);
		if (!given) {
			return given;
		}
		return noVertexInsideTheUnionBelongsToNothing(mesh, model);
	}

	// The rings added one patch each in `order`, every state checked; then each removed and
	// added back in turn.
	void addInOrder(const std::vector<NavInputPolygon>& rings, const std::vector<std::size_t>& order) {
		std::string when = "order";
		for (const std::size_t i : order) {
			when += " " + std::to_string(i);
		}
		Harness				h;
		std::vector<RingId> ids(rings.size(), kNoRing);
		for (const std::size_t i : order) {
			ids[i] = h.add(rings[i]);
			ASSERT_TRUE(overlapHolds(h.mesh, h.model)) << when << ", ring " << i << " added" << describe(h.model);
		}
		for (std::size_t i = 0; i < rings.size(); ++i) {
			PatchEdits leave;
			leave.removes.push_back(ids[i]);
			h.apply(leave);
			ASSERT_TRUE(overlapHolds(h.mesh, h.model)) << when << ", ring " << i << " removed" << describe(h.model);
			ids[i] = h.add(rings[i]);
			ASSERT_TRUE(overlapHolds(h.mesh, h.model)) << when << ", ring " << i << " back" << describe(h.model);
		}
	}

	// Every arrival order (or, past four rings, the given ones), and all in one patch.
	void addInEveryOrder(const std::vector<NavInputPolygon>& rings, std::vector<std::vector<std::size_t>> orders = {}) {
		if (orders.empty()) {
			std::vector<std::size_t> order(rings.size());
			for (std::size_t i = 0; i < order.size(); ++i) {
				order[i] = i;
			}
			do {
				orders.push_back(order);
			} while (std::next_permutation(order.begin(), order.end()));
		}
		for (const std::vector<std::size_t>& order : orders) {
			addInOrder(rings, order);
			if (::testing::Test::HasFatalFailure()) {
				return;
			}
		}
		Harness	   h;
		PatchEdits together;
		together.adds = rings;
		h.apply(together);
		ASSERT_TRUE(overlapHolds(h.mesh, h.model)) << "one patch" << describe(h.model);
	}

	std::int64_t liveArea2(const PatchableMesh& mesh) {
		Int128 area(0);
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (mesh.triangleLive(t)) {
				const NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
				area = area + cross(mesh.vertices()[tri.v[1]] - mesh.vertices()[tri.v[0]], mesh.vertices()[tri.v[2]] - mesh.vertices()[tri.v[0]]);
			}
		}
		return static_cast<std::int64_t>(area.toDouble());
	}

	std::vector<GroundInterval> boundariesOf(const std::vector<std::vector<Vec2i64>>& outlines, std::size_t ring, std::int32_t edge) {
		std::vector<GroundRing> ground;
		for (std::size_t i = 0; i < outlines.size(); ++i) {
			ground.push_back(groundRing(outlines[i], static_cast<RingId>(i)));
		}
		GroundBoundaryScratch		scratch;
		std::vector<GroundInterval> out;
		groundBoundaries(ground[ring], edge, ground, scratch, out);
		return out;
	}

	std::vector<std::array<Vec2i64, 2>> endsOf(const std::vector<GroundInterval>& intervals) {
		std::vector<std::array<Vec2i64, 2>> out;
		for (const GroundInterval& iv : intervals) {
			out.push_back({iv.from, iv.to});
		}
		return out;
	}

	using Ends = std::vector<std::array<Vec2i64, 2>>;

} // namespace

// The boundary intervals of the bottom side (0, 0)-(10, 0) of a square, against each kind
// of contact another outline can make with it.
TEST(PatchableMesh, GroundBoundariesHandleEveryContact) {
	const std::vector<Vec2i64> square = rect(0, 0, 10, 10);
	auto					   bottom = [&](const std::vector<Vec2i64>& other, bool otherFirst = false) {
		  return endsOf(otherFirst ? boundariesOf({other, square}, 1, 0) : boundariesOf({square, other}, 0, 0));
	};
	EXPECT_EQ(bottom(rect(20, 20, 30, 30)), (Ends{{Vec2i64{0, 0}, Vec2i64{10, 0}}})) << "apart";
	EXPECT_EQ(bottom(rect(4, -6, 6, 0)), (Ends{{Vec2i64{0, 0}, Vec2i64{4, 0}}, {Vec2i64{6, 0}, Vec2i64{10, 0}}})) << "sharing a stretch from outside";
	EXPECT_EQ(bottom({{5, 0}, {3, -2}, {5, -4}, {7, -2}}), (Ends{{Vec2i64{0, 0}, Vec2i64{10, 0}}})) << "a corner touching from outside";
	EXPECT_EQ(bottom({{5, 0}, {7, 2}, {5, 4}, {3, 2}}), (Ends{{Vec2i64{0, 0}, Vec2i64{10, 0}}})) << "a corner touching from inside";
	EXPECT_EQ(bottom({{5, 0}, {8, -3}, {8, 3}}), (Ends{{Vec2i64{0, 0}, Vec2i64{5, 0}}, {Vec2i64{8, 0}, Vec2i64{10, 0}}})) << "a corner on the side, crossing it";
	EXPECT_EQ(bottom(rect(-2, -2, 12, 12)), Ends{}) << "nested in another";
	EXPECT_EQ(bottom(rect(-2, -4, 3, 4)), (Ends{{Vec2i64{3, 0}, Vec2i64{10, 0}}})) << "a crossing";
	EXPECT_EQ(bottom({{4, 0}, {6, 4}, {2, 4}}), (Ends{{Vec2i64{0, 0}, Vec2i64{10, 0}}})) << "inside, a corner on the side";
	// Two outlines crossing the side at one point: one leaves ground there as the other
	// arrives.
	EXPECT_EQ(endsOf(boundariesOf({square, {{5, 0}, {5, -4}, {1, -4}, {1, 2}}, {{5, 2}, {9, 2}, {9, -4}, {5, -4}}}, 0, 0)),
			  (Ends{{Vec2i64{0, 0}, Vec2i64{1, 0}}, {Vec2i64{9, 0}, Vec2i64{10, 0}}}))
		<< "two at one point";
	// Running along the side with the inside on the same side: the lower edge takes the
	// shared stretch.
	EXPECT_EQ(bottom(rect(4, 0, 14, 6)), (Ends{{Vec2i64{0, 0}, Vec2i64{10, 0}}})) << "same way, this one lower";
	EXPECT_EQ(bottom(rect(4, 0, 14, 6), true), (Ends{{Vec2i64{0, 0}, Vec2i64{4, 0}}})) << "same way, the other lower";
	// A crossing off the lattice ends both outlines' intervals at one rounded point.
	const std::vector<Vec2i64>	slant{{3, -3}, {6, 4}, {-2, 4}};
	const std::vector<GroundInterval> mine	 = boundariesOf({square, slant}, 0, 0);
	const std::vector<GroundInterval> theirs = boundariesOf({square, slant}, 1, 0);
	ASSERT_EQ(mine.size(), 2U);
	ASSERT_EQ(theirs.size(), 1U);
	EXPECT_EQ(mine.back().from, theirs.front().to) << "the outlines meet where they cross";
}

// Ground from the parity flood over the arranged boundary intervals is "inside any ground
// outline" at every triangle's centroid, read exactly as validate reads it, over outlines
// that touch, nest, share sides either way, and cross only at lattice points (so nothing
// is rounded): axis-aligned rectangles and diamonds on a coarse grid.
TEST(PatchableMesh, GroundBoundariesAgreeWithContainmentAtEverySample) {
	for (std::uint32_t seed = 1; seed <= 60; ++seed) {
		std::mt19937 rng(seed);
		Model		 model;
		const int	 count = static_cast<int>(between(rng, 2, 6));
		for (RingId i = 0; i < count; ++i) {
			const std::int64_t x = 2 * between(rng, 0, 8);
			const std::int64_t y = 2 * between(rng, 0, 8);
			std::vector<Vec2i64> ring;
			if (pick(rng, 3) == 0) {
				const std::int64_t r = 2 * between(rng, 1, 4);
				ring				 = {{x, y - r}, {x + r, y}, {x, y + r}, {x - r, y}};
			} else {
				const std::int64_t h = 2 * between(rng, 1, 5);
				const std::int64_t w = 2 * between(rng, 1, 5);
				ring = rect(x, y, x + w, y + h);
			}
			if (pick(rng, 2) == 0) {
				std::reverse(ring.begin(), ring.end());
			}
			model[i] = bounds(ring);
		}
		std::vector<const NavInputPolygon*> rings;
		for (const auto& [id, poly] : model) {
			rings.push_back(&poly);
		}
		std::vector<InputSegment> segments;
		ringsArrangementInput(rings, [](std::int32_t ring, std::int32_t edge) { return encode(ring, edge); }, segments);
		const Topology topo = topology(buildArrangement(segments), model);
		ASSERT_TRUE(topo.consistent) << "seed " << seed << describe(model);
		std::vector<SampleRing> samples;
		for (const auto& [id, poly] : model) {
			samples.push_back(sampleRing(poly));
		}
		for (std::size_t t = 0; t < topo.cdt.triangles.size(); ++t) {
			const geometry::CdtTriangle& tri   = topo.cdt.triangles[t];
			bool						 frame = false;
			for (const std::uint32_t v : tri.v) {
				frame = frame || v >= topo.cdt.frameStart;
			}
			if (frame) {
				continue;
			}
			const Vec2i64 sample3 = topo.cdt.vertices[tri.v[0]] + topo.cdt.vertices[tri.v[1]] + topo.cdt.vertices[tri.v[2]];
			bool		  any	  = false;
			for (const SampleRing& s : samples) {
				any = any || boundsContainSample(s, sample3);
			}
			const bool flooded = !topo.cover[t].empty() && topo.cover[t].front() == kGroundToken;
			ASSERT_EQ(flooded, any) << "seed " << seed << " sample (" << sample3.x << ", " << sample3.y << ") in 3x space" << describe(model);
		}
	}
}

TEST(PatchableMesh, OverlappingSquaresInEveryOrder) {
	addInEveryOrder({bounds(rect(0, 0, 10000, 10000)), bounds(rect(6000, 3000, 16000, 13000))});
}

TEST(PatchableMesh, OverlappingSlantedPolygonsInEveryOrder) {
	addInEveryOrder({bounds(limitPolygonOfCircumradius({0, 0}, 5000)), bounds(limitPolygonOfCircumradius({6100, 1300}, 4300))});
}

TEST(PatchableMesh, NestedOutlineAddsNoBoundary) {
	const std::vector<NavInputPolygon> rings{bounds(limitPolygonOfCircumradius({0, 0}, 9000)), bounds(limitPolygonOfCircumradius({1000, -700}, 3000)), bounds(rect(-2000, 1000, 2500, 4000))};
	addInEveryOrder(rings);
	Harness h;
	h.add(rings[0]);
	const std::int64_t area = liveArea2(h.mesh);
	PatchEdits		   inner;
	inner.adds = {rings[1], rings[2]};
	const PatchReport report = h.apply(inner);
	EXPECT_EQ(liveArea2(h.mesh), area);
	EXPECT_TRUE(idsWith(report, kTriangleFreed | kTriangleCreated).empty()) << "an outline inside ground re-derives nothing";
}

TEST(PatchableMesh, ThreeOutlinesWithACommonOverlap) {
	addInEveryOrder({bounds(limitPolygonOfCircumradius({0, 0}, 5000)), bounds(limitPolygonOfCircumradius({5200, 600}, 4500)), bounds(limitPolygonOfCircumradius({2300, 4700}, 4800))});
}

// A walked band: five limits in a row, each overlapping the next, in all 120 orders.
TEST(PatchableMesh, LimitsInAnyOrderMatchFreshBuild) {
	std::vector<NavInputPolygon> band;
	for (std::int64_t i = 0; i < 5; ++i) {
		band.push_back(bounds(limitPolygon({i * 3700, (i % 2) * 900}, 5000)));
	}
	addInEveryOrder(band);
}

TEST(PatchableMesh, PolygonBridgingTwoApart) {
	addInEveryOrder({bounds(limitPolygonOfCircumradius({0, 0}, 4000)), bounds(limitPolygonOfCircumradius({11000, 500}, 4000)), bounds(rect(2500, -1500, 8800, 2100))});
}

TEST(PatchableMesh, OutlinesTouchingAtOneVertex) {
	addInEveryOrder({bounds(limitPolygonOfCircumradius({0, 0}, 5000)), bounds(limitPolygonOfCircumradius({10000, 0}, 5000))});
	addInEveryOrder({bounds(rect(0, 0, 6000, 6000)), bounds(rect(6000, 6000, 12000, 12000)), bounds({{6000, 6000}, {9000, 3000}, {12000, 6000}})});
}

// A polygon and its copy moved along one of its own edges: their edges on that line run the
// same way, so the stretch they share is input once.
TEST(PatchableMesh, CollinearSameWayEdges) {
	const std::vector<Vec2i64> hexagon{{0, 0}, {4000, 0}, {6000, 3000}, {4000, 6000}, {0, 6000}, {-2000, 3000}};
	addInEveryOrder({bounds(hexagon), bounds(translated(hexagon, {2000, 0}))});
	addInEveryOrder({bounds(hexagon), bounds(translated(hexagon, {1000, 1500}))});
}

// A square against two half-length squares: the whole shared side dissolves, and only the
// union's corners and the outer sides' meeting points stay on it.
TEST(PatchableMesh, SquareAgainstTwoHalfSquaresDissolves) {
	const std::vector<NavInputPolygon> rings{bounds(rect(0, 0, 10000, 10000)), bounds(rect(10000, 0, 20000, 5000)), bounds(rect(10000, 5000, 20000, 10000))};
	addInEveryOrder(rings);
	Harness	   h;
	PatchEdits all;
	all.adds = rings;
	h.apply(all);
	EXPECT_EQ(liveVerticesOn(h.mesh, {10000, 0}, {10000, 10000}), (std::vector<Vec2i64>{{10000, 0}, {10000, 10000}}));
}

TEST(PatchableMesh, ObstaclesAcrossOverlappingGround) {
	const NavInputPolygon a = bounds(rect(0, 0, 10000, 10000));
	const NavInputPolygon b = bounds(limitPolygonOfCircumradius({12000, 5000}, 5500));
	// Straddling the union's edge, lying across a dissolved stretch of the square's side, and
	// water across the overlap.
	addInEveryOrder({a, b, blocked({{15000, 9000}, {18500, 10500}, {16000, 12000}})});
	addInEveryOrder({a, b, blocked({{9000, 4000}, {11500, 4600}, {11000, 5800}, {8800, 5000}})});
	addInEveryOrder({a, b, blocked(rect(8000, 2000, 13000, 3500), kWater, true)});
}

// Ground a millimetre across beside a dissolved side, wholly in the snap band of a blocked
// edge: the side has no pieces, so its stretch is read as given.
TEST(PatchableMesh, SliverBesideADissolvedSideMatchesFreshBuild) {
	addInEveryOrder({bounds({{0, 0}, {2, 0}, {0, 1}}), bounds({{0, 0}, {0, 1}, {-2, 0}}), blocked({{-10, -10}, {20, -10}, {20, 1}, {-10, 0}})});
}

namespace {

	// How many of the outline's edges are each one constrained mesh edge.
	int constrainedEdgesOf(const PatchableMesh& mesh, const std::vector<Vec2i64>& outline) {
		int count = 0;
		for (std::size_t i = 0; i < outline.size(); ++i) {
			count += sourcesBetween(mesh, outline[i], outline[(i + 1) % outline.size()]).empty() ? 0 : 1;
		}
		return count;
	}

} // namespace

// The middle of three outlines in a row taken out: the stretches of its neighbours it had
// dissolved are constraints again; put back, they dissolve again.
TEST(PatchableMesh, RemovingAnOutlineBringsItsNeighboursEdgesBack) {
	const std::vector<Vec2i64> west	  = limitPolygonOfCircumradius({0, 0}, 5000);
	const std::vector<Vec2i64> middle = limitPolygonOfCircumradius({7000, 600}, 5000);
	const std::vector<Vec2i64> east	  = limitPolygonOfCircumradius({14000, 0}, 5000);
	Harness					   h;
	h.add(bounds(west));
	RingId mid = h.add(bounds(middle));
	h.add(bounds(east));
	ASSERT_TRUE(overlapHolds(h.mesh, h.model));
	const int westKept = constrainedEdgesOf(h.mesh, west);
	const int eastKept = constrainedEdgesOf(h.mesh, east);
	ASSERT_LT(westKept, 16);
	ASSERT_LT(eastKept, 16);
	PatchEdits leave;
	leave.removes.push_back(mid);
	h.apply(leave);
	ASSERT_TRUE(overlapHolds(h.mesh, h.model));
	EXPECT_EQ(constrainedEdgesOf(h.mesh, west), 16);
	EXPECT_EQ(constrainedEdgesOf(h.mesh, east), 16);
	mid = h.add(bounds(middle));
	ASSERT_TRUE(overlapHolds(h.mesh, h.model));
	EXPECT_EQ(constrainedEdgesOf(h.mesh, west), westKept);
	EXPECT_EQ(constrainedEdgesOf(h.mesh, east), eastKept);
}

// An outline moved within the union, partly out of it, wholly out of it, and back, with and
// without a placed obstacle across the ground it leaves.
TEST(PatchableMesh, MovedOutlineMatchesFreshBuild) {
	for (const bool withObstacle : {false, true}) {
		Harness h;
		h.add(bounds(limitPolygonOfCircumradius({0, 0}, 9000)));
		if (withObstacle) {
			h.add(blocked({{2000, -500}, {9500, 300}, {9300, 900}, {1900, 400}}));
		}
		const RingId moving = h.add(bounds(limitPolygonOfCircumradius({3000, 1000}, 3000)));
		ASSERT_TRUE(overlapHolds(h.mesh, h.model));
		for (const Vec2i64& centre : {Vec2i64{4000, -1500}, Vec2i64{8000, 2000}, Vec2i64{20000, 3000}, Vec2i64{9500, 0}, Vec2i64{1000, 1000}}) {
			PatchEdits move;
			move.moves.push_back({moving, limitPolygonOfCircumradius(centre, 3000)});
			h.apply(move);
			ASSERT_TRUE(overlapHolds(h.mesh, h.model)) << "centre (" << centre.x << ", " << centre.y << ")" << (withObstacle ? " with the obstacle" : "");
		}
	}
}

// Two outlines sharing a stretch of one line, both inside above it: the lower ring owns the
// stretch. Taking out the owner passes the stretch to the other, and putting it back takes
// it again; taking out the other leaves the owner's side whole.
TEST(PatchableMesh, RemovingTheOwnerOfASharedStretchPassesItOn) {
	const std::vector<Vec2i64> hexagon{{0, 0}, {4000, 0}, {6000, 3000}, {4000, 6000}, {0, 6000}, {-2000, 3000}};
	const std::vector<Vec2i64> shifted = translated(hexagon, {2000, 0});
	using Sources					   = std::vector<EdgeSource>;
	Harness h;
	RingId	owner = h.add(bounds(hexagon));
	RingId	other = h.add(bounds(shifted));
	ASSERT_LT(owner, other);
	ASSERT_TRUE(overlapHolds(h.mesh, h.model));
	EXPECT_EQ(sourcesBetween(h.mesh, {0, 0}, {4000, 0}), (Sources{{owner, 0}}));
	EXPECT_EQ(sourcesBetween(h.mesh, {4000, 0}, {6000, 0}), (Sources{{other, 0}}));
	EXPECT_EQ(liveVerticesOn(h.mesh, {0, 0}, {6000, 0}), (std::vector<Vec2i64>{{0, 0}, {4000, 0}, {6000, 0}})) << "the shared stretch is kept once";

	PatchEdits leave;
	leave.removes.push_back(owner);
	h.apply(leave);
	ASSERT_TRUE(overlapHolds(h.mesh, h.model));
	EXPECT_EQ(sourcesBetween(h.mesh, {2000, 0}, {6000, 0}), (Sources{{other, 0}}));
	owner = h.add(bounds(hexagon));
	ASSERT_TRUE(overlapHolds(h.mesh, h.model));
	if (owner < other) {
		EXPECT_EQ(sourcesBetween(h.mesh, {0, 0}, {4000, 0}), (Sources{{owner, 0}}));
		EXPECT_EQ(sourcesBetween(h.mesh, {4000, 0}, {6000, 0}), (Sources{{other, 0}}));
	} else {
		EXPECT_EQ(sourcesBetween(h.mesh, {0, 0}, {2000, 0}), (Sources{{owner, 0}}));
		EXPECT_EQ(sourcesBetween(h.mesh, {2000, 0}, {6000, 0}), (Sources{{other, 0}}));
	}

	PatchEdits leaveOther;
	leaveOther.removes.push_back(other);
	h.apply(leaveOther);
	ASSERT_TRUE(overlapHolds(h.mesh, h.model));
	EXPECT_EQ(sourcesBetween(h.mesh, {0, 0}, {4000, 0}), (Sources{{owner, 0}}));
	EXPECT_EQ(constrainedEdgesOf(h.mesh, hexagon), 6);
}

// An outline around another taken out: the inner one's sides are constraints again, and
// the ground is the inner outline alone; put back, they dissolve again.
TEST(PatchableMesh, RemovingAnOutlineAroundAnotherBringsTheInnerEdgesBack) {
	const std::vector<Vec2i64> outer = limitPolygonOfCircumradius({0, 0}, 9000);
	const std::vector<Vec2i64> inner = limitPolygonOfCircumradius({1000, -700}, 3000);
	Harness					   h;
	RingId					   around = h.add(bounds(outer));
	h.add(bounds(inner));
	ASSERT_TRUE(overlapHolds(h.mesh, h.model));
	EXPECT_EQ(constrainedEdgesOf(h.mesh, inner), 0);
	PatchEdits leave;
	leave.removes.push_back(around);
	h.apply(leave);
	ASSERT_TRUE(overlapHolds(h.mesh, h.model));
	EXPECT_EQ(constrainedEdgesOf(h.mesh, inner), 16);
	Harness alone;
	alone.add(bounds(inner));
	EXPECT_EQ(liveArea2(h.mesh), liveArea2(alone.mesh));
	around = h.add(bounds(outer));
	ASSERT_TRUE(overlapHolds(h.mesh, h.model));
	EXPECT_EQ(constrainedEdgesOf(h.mesh, inner), 0);
	EXPECT_EQ(constrainedEdgesOf(h.mesh, outer), 16);
}

// The last ground under a blocked ring taken out: the ring hangs over nothing and the mesh
// is empty; ground put back completes the ring again. Both for a ring straddling the
// ground's edge and one wholly inside it.
TEST(PatchableMesh, RemovingTheLastGroundUnderAHangingRingLeavesNothing) {
	for (const std::vector<Vec2i64>& lake : {rect(6000, 3000, 16000, 7000), rect(3000, 3000, 7000, 7000)}) {
		Harness h;
		RingId	ground = h.add(bounds(rect(0, 0, 10000, 10000)));
		h.add(blocked(lake, kWater, true));
		ASSERT_TRUE(overlapHolds(h.mesh, h.model));
		PatchEdits leave;
		leave.removes.push_back(ground);
		h.apply(leave);
		EXPECT_EQ(h.mesh.liveTriangleCount(), 0);
		ASSERT_TRUE(overlapHolds(h.mesh, h.model));
		ground = h.add(bounds(rect(0, 0, 10000, 10000)));
		ASSERT_TRUE(overlapHolds(h.mesh, h.model));
	}
}

// groundCovers holds exactly when adding the outline as ground changes nothing: inside the
// union, over water inside it, touching its edge from inside, or a copy of a live outline;
// not when it pokes out by a millimetre, lies outside, or spans a gap in the union.
TEST(PatchableMesh, LimitInsideUnionAddsNothing) {
	Harness h;
	EXPECT_FALSE(h.mesh.groundCovers(rect(0, 0, 1000, 1000))) << "no ground";
	h.add(bounds(rect(0, 0, 10000, 10000)));
	h.add(bounds(limitPolygonOfCircumradius({12000, 5000}, 5500)));
	h.add(blocked(rect(3000, 3000, 6000, 6000), kWater, true));
	for (const std::vector<Vec2i64>& frame : {rect(20000, 0, 30000, 3000), rect(20000, 7000, 30000, 10000), rect(20000, 0, 23000, 10000), rect(27000, 0, 30000, 10000)}) {
		h.add(bounds(frame));
	}
	ASSERT_TRUE(overlapHolds(h.mesh, h.model));
	struct Case {
		const char*			 what;
		std::vector<Vec2i64> outline;
		bool				 covered;
	};
	const std::vector<Case> cases{
		{"inside", limitPolygonOfCircumradius({4000, 5000}, 3000), true},
		{"over water inside ground", rect(2500, 2500, 6500, 6500), true},
		{"across the overlap", limitPolygonOfCircumradius({9000, 5000}, 2000), true},
		{"touching the edge from inside", rect(1000, 1000, 9000, 10000), true},
		{"a copy of a live outline", rect(0, 0, 10000, 10000), true},
		{"poking out by a millimetre", rect(1000, 1000, 9000, 10001), false},
		{"outside", limitPolygonOfCircumradius({40000, 5000}, 3000), false},
		{"over a gap in the union", rect(21000, 1000, 29000, 9000), false},
		{"over a gap, sides on its edges", rect(20000, 0, 30000, 10000), false},
	};
	for (const Case& c : cases) {
		EXPECT_EQ(h.mesh.groundCovers(c.outline), c.covered) << c.what;
		Harness				with = h;
		const std::int64_t area = liveArea2(with.mesh);
		with.add(bounds(c.outline));
		ASSERT_TRUE(overlapHolds(with.mesh, with.model)) << c.what;
		EXPECT_EQ(liveArea2(with.mesh) == area, c.covered) << c.what << ": the union's area";
	}
}

namespace {

	struct LiveLimit {
		RingId		 id = kNoRing;
		Vec2i64		 centre;
		std::int64_t range = 0;
	};

	// A limit somewhere in the field, or one built against a live limit: its copy moved
	// along one of its sides (exactly, so the sides run on one line, or a millimetre off
	// it), an exact copy, or another round a nearby centre.
	LiveLimit nextLimit(std::mt19937& rng, const std::vector<LiveLimit>& live) {
		const std::uint64_t kind = live.empty() ? 0 : pick(rng, 10);
		if (kind < 4) {
			return {kNoRing, {between(rng, 0, 24000), between(rng, 0, 24000)}, between(rng, 3000, 12000)};
		}
		const LiveLimit& near = live[pick(rng, live.size())];
		if (kind < 7) {
			const std::vector<Vec2i64> outline = limitPolygon(near.centre, near.range);
			const std::size_t		   k	   = pick(rng, outline.size());
			const Vec2i64			   side	   = outline[(k + 1) % outline.size()] - outline[k];
			const std::int64_t		   g	   = std::gcd(side.x, side.y);
			const Vec2i64			   step{side.x / g, side.y / g};
			const std::int64_t		   times = g > 1 ? between(rng, 1, g - 1) : 1;
			Vec2i64					   by{step.x * times, step.y * times};
			if (pick(rng, 2) == 0) {
				by = by + (pick(rng, 2) == 0 ? Vec2i64{1, 0} : Vec2i64{0, 1});
			}
			return {kNoRing, near.centre + by, near.range};
		}
		if (kind < 8) {
			return {kNoRing, near.centre, near.range};
		}
		return {kNoRing, near.centre + Vec2i64{between(rng, -near.range, near.range), between(rng, -near.range, near.range)}, between(rng, 3000, 12000)};
	}

	// Visibility limits arriving in batches, leaving, and moving, among placed obstacles that
	// cross their sides at shallow angles and blocked rectangles; every state equal to a
	// from-scratch build, with no vertex inside the union that no blocked ring explains.
	void runOverlappingLimits(std::uint32_t seed, int steps) {
		std::mt19937		   rng(seed);
		Harness				   h;
		std::vector<LiveLimit> limits;
		std::vector<RingId>	   placed;
		for (int step = 0; step < steps; ++step) {
			PatchEdits			   edits;
			std::vector<LiveLimit> arriving;
			const std::uint64_t	   op = pick(rng, 10);
			if (limits.size() < 2 || op < 4) {
				const std::uint64_t count = 1 + pick(rng, 3);
				for (std::uint64_t i = 0; i < count; ++i) {
					arriving.push_back(nextLimit(rng, limits));
					edits.adds.push_back(bounds(limitPolygon(arriving.back().centre, arriving.back().range)));
				}
			} else if (op < 5) {
				const std::size_t at = pick(rng, limits.size());
				edits.removes.push_back(limits[at].id);
				limits.erase(limits.begin() + static_cast<std::ptrdiff_t>(at));
			} else if (op < 6) {
				LiveLimit& moving = limits[pick(rng, limits.size())];
				moving.centre	  = moving.centre + Vec2i64{between(rng, -4000, 4000), between(rng, -4000, 4000)};
				edits.moves.push_back({moving.id, limitPolygon(moving.centre, moving.range)});
			} else if (op < 8 || placed.empty()) {
				std::vector<NavInputPolygon> sides;
				for (const LiveLimit& l : limits) {
					sides.push_back(bounds(limitPolygon(l.centre, l.range)));
				}
				const std::uint64_t count = 1 + pick(rng, 3);
				for (std::uint64_t i = 0; i < count; ++i) {
					edits.adds.push_back(shallowObstacle(rng, sides));
				}
			} else if (op < 9) {
				const std::size_t at = pick(rng, placed.size());
				edits.removes.push_back(placed[at]);
				placed.erase(placed.begin() + static_cast<std::ptrdiff_t>(at));
			} else {
				const RingId r = placed[pick(rng, placed.size())];
				edits.moves.push_back({r, translated(h.model[r].ring, {between(rng, -2000, 2000), between(rng, -2000, 2000)})});
			}
			const PatchReport report = h.apply(edits);
			for (std::size_t i = 0; i < report.added.size(); ++i) {
				if (i < arriving.size()) {
					arriving[i].id = report.added[i];
					limits.push_back(arriving[i]);
				} else {
					placed.push_back(report.added[i]);
				}
			}
			ASSERT_TRUE(overlapHolds(h.mesh, h.model)) << "seed " << seed << " step " << step << describe(h.model);
		}
	}

} // namespace

// A lake with an island, hanging over no ground, completed by limits arriving along a walk
// across it one at a time: the island is floor, the water round it is water.
TEST(PatchableMesh, HangingLakeWithIslandCompletedByLimits) {
	Harness	   h;
	PatchEdits water;
	water.adds = {blocked(rect(0, 0, 20000, 12000), kWater, true), blocked(rect(7000, 4000, 13000, 8000), kWater, true)};
	const PatchReport hanging = h.apply(water);
	const RingId	  lake	  = hanging.added[0];
	const RingId	  island  = hanging.added[1];
	EXPECT_EQ(h.mesh.liveTriangleCount(), 0);
	for (std::int64_t i = 0; i < 7; ++i) {
		h.add(bounds(limitPolygon({-3000 + i * 4000, 6000 + (i % 2) * 500}, 5000)));
		ASSERT_TRUE(overlapHolds(h.mesh, h.model)) << "limit " << i;
	}
	// Inside both water rings (so floor, by their parity), and inside the outer one only.
	const std::int32_t onIsland = h.mesh.locate({10000, 6000});
	const std::int32_t onWater	= h.mesh.locate({4000, 6000});
	ASSERT_GE(onIsland, 0);
	ASSERT_GE(onWater, 0);
	EXPECT_EQ(h.mesh.cover(onIsland), (std::vector<RingId>{kGroundToken, lake, island}));
	EXPECT_EQ(h.mesh.cover(onWater), (std::vector<RingId>{kGroundToken, lake}));
}

// A limit and its copy moved along its first side: those sides run on one line the same
// way, as do the opposite sides, and each shared stretch is kept once, by the lower ring,
// with no vertex inside it except where another edge's boundary interval ends.
TEST(PatchableMesh, CollinearSameWayLimitEdgesKeptOnce) {
	const std::vector<Vec2i64> first = limitPolygon({0, 0}, 5021);
	const Vec2i64			   side	 = first[1] - first[0];
	const std::int64_t		   g	 = std::gcd(side.x, side.y);
	ASSERT_GE(g, 2) << "the side's lattice holds a point inside it";
	const Vec2i64			   by{side.x / g * (g / 2), side.y / g * (g / 2)};
	const std::vector<Vec2i64> second = translated(first, by);
	for (const bool firstLower : {true, false}) {
		Harness				h;
		const RingId		lower = h.add(bounds(firstLower ? first : second));
		const RingId		upper = h.add(bounds(firstLower ? second : first));
		ASSERT_TRUE(overlapHolds(h.mesh, h.model));
		const std::vector<Vec2i64>& lowerRing = firstLower ? first : second;
		const std::vector<Vec2i64>& upperRing = firstLower ? second : first;
		for (const std::size_t k : {std::size_t{0}, std::size_t{8}}) {
			// Along side k the lower ring's side is kept whole and the upper's beyond it.
			const Vec2i64 lowerFrom = lowerRing[k];
			const Vec2i64 lowerTo	= lowerRing[k + 1];
			const bool	  upperLeads = dot(upperRing[k] - lowerFrom, lowerTo - lowerFrom).sign() < 0;
			const Vec2i64 restFrom	 = upperLeads ? upperRing[k] : lowerTo;
			const Vec2i64 restTo	 = upperLeads ? lowerFrom : upperRing[k + 1];
			std::vector<Vec2i64> expected{lowerFrom, lowerTo, upperLeads ? upperRing[k] : upperRing[k + 1]};
			std::sort(expected.begin(), expected.end());
			const Vec2i64 runFrom = upperLeads ? upperRing[k] : lowerFrom;
			const Vec2i64 runTo	  = upperLeads ? lowerTo : upperRing[k + 1];
			EXPECT_EQ(liveVerticesOn(h.mesh, runFrom, runTo), expected) << "side " << k << (firstLower ? "" : ", the copy lower");
			EXPECT_EQ(sourcesBetween(h.mesh, lowerFrom, lowerTo), (std::vector<EdgeSource>{{lower, static_cast<std::int32_t>(k)}})) << "side " << k;
			EXPECT_EQ(sourcesBetween(h.mesh, restFrom, restTo), (std::vector<EdgeSource>{{upper, static_cast<std::int32_t>(k)}})) << "side " << k;
		}
	}
}

// Raise kSeeds for a heavier sweep (400 seeds of 40 steps held).
TEST(PatchableMesh, OverlappingSlantedOutlinesMatchFreshBuild) {
	constexpr std::uint32_t kSeeds = 24;
	for (std::uint32_t seed = 1; seed <= kSeeds; ++seed) {
		runOverlappingLimits(seed, 40);
		if (HasFatalFailure()) {
			return;
		}
	}
}

// --- Player visibility's seen record: sight outlines as ground rings -------------------

namespace {

	// Whether no two edges of the ring meet, except neighbours at their shared vertex.
	bool simpleRing(const std::vector<Vec2i64>& ring) {
		const std::size_t count = ring.size();
		for (std::size_t i = 0; i < count; ++i) {
			const Vec2i64& start = ring[i];
			const Vec2i64& end	 = ring[(i + 1) % count];
			for (std::size_t j = i + 1; j < count; ++j) {
				const Vec2i64& otherStart = ring[j];
				const Vec2i64& otherEnd	  = ring[(j + 1) % count];
				if (std::max(start.x, end.x) < std::min(otherStart.x, otherEnd.x) || std::max(otherStart.x, otherEnd.x) < std::min(start.x, end.x) ||
					std::max(start.y, end.y) < std::min(otherStart.y, otherEnd.y) || std::max(otherStart.y, otherEnd.y) < std::min(start.y, end.y)) {
					continue;
				}
				const geometry::SegmentRelation relation = geometry::intersectSegments(start, end, otherStart, otherEnd).relation;
				const bool						adjacent = j == i + 1 || (i == 0 && j == count - 1);
				if (relation == geometry::SegmentRelation::Disjoint || (adjacent && relation == geometry::SegmentRelation::EndpointTouch)) {
					continue;
				}
				return false;
			}
		}
		return true;
	}

	// A 0.2 m wall along y = 0, 40 m long, with a 1 m doorway at x in [-500, 500]: each
	// half's four faces.
	std::vector<geometry::OccluderSegment> doorwayWalls() {
		std::vector<geometry::OccluderSegment> out;
		for (const auto& [x0, x1] : {std::pair<std::int64_t, std::int64_t>{-20000, -500}, std::pair<std::int64_t, std::int64_t>{500, 20000}}) {
			out.push_back({{x0, 0}, {x1, 0}});
			out.push_back({{x1, 0}, {x1, 200}});
			out.push_back({{x1, 200}, {x0, 200}});
			out.push_back({{x0, 200}, {x0, 0}});
		}
		return out;
	}

	std::int64_t cross3(const Vec2i64& origin, const Vec2i64& first, const Vec2i64& second) {
		return (first.x - origin.x) * (second.y - origin.y) - (first.y - origin.y) * (second.x - origin.x);
	}

	// The hull of the limit polygons of `circumradius` at a leg's two ends, counter-clockwise.
	std::vector<Vec2i64> capsule(const Vec2i64& start, const Vec2i64& end, std::int64_t circumradius) {
		std::vector<Vec2i64>	   points = limitPolygonOfCircumradius(start, circumradius);
		const std::vector<Vec2i64> atEnd  = limitPolygonOfCircumradius(end, circumradius);
		points.insert(points.end(), atEnd.begin(), atEnd.end());
		std::sort(points.begin(), points.end());
		points.erase(std::unique(points.begin(), points.end()), points.end());
		std::vector<Vec2i64> hull(2 * points.size());
		std::size_t			 size = 0;
		for (const Vec2i64& point : points) {
			while (size >= 2 && cross3(hull[size - 2], hull[size - 1], point) <= 0) {
				--size;
			}
			hull[size++] = point;
		}
		const std::size_t lower = size + 1;
		for (std::size_t i = points.size() - 1; i-- > 0;) {
			while (size >= lower && cross3(hull[size - 2], hull[size - 1], points[i]) <= 0) {
				--size;
			}
			hull[size++] = points[i];
		}
		hull.resize(size - 1);
		return hull;
	}

} // namespace

// Visibility polygons from viewpoints 0.5 m apart as an observer walks past a doorway, along
// the line of one jamb's face, and through it twice, each added as ground where groundCovers
// says it extends the union: rays grazing the jambs, and nearly parallel edges from
// viewpoints a step apart.
TEST(PatchableMesh, DoorwayVisibilityPolygonsMatchFreshBuild) {
	const std::vector<geometry::OccluderSegment> walls = doorwayWalls();
	std::vector<Vec2i64>						 viewpoints;
	for (std::int64_t x = -3000; x <= 3000; x += 500) {
		viewpoints.push_back({x, -3000});
	}
	for (std::int64_t y = -4000; y <= -500; y += 500) {
		viewpoints.push_back({500, y});
	}
	for (std::int64_t y = -4000; y <= 4000; y += 500) {
		viewpoints.push_back({0, y});
	}
	for (std::int64_t k = -6; k <= 6; ++k) {
		viewpoints.push_back({k * 350, k * 350 + 100});
	}
	Harness harness;
	int		added = 0;
	for (const Vec2i64& viewpoint : viewpoints) {
		const geometry::Ring ring = geometry::computeVisibilityPolygon(viewpoint, 30000, walls);
		ASSERT_GE(ring.size(), 3U);
		ASSERT_TRUE(simpleRing(ring)) << "the polygon from (" << viewpoint.x << ", " << viewpoint.y << ") is not simple";
		if (harness.mesh.groundCovers(ring)) {
			continue;
		}
		harness.add(bounds(ring));
		++added;
		ASSERT_EQ(harness.last.unreadCovers, 0) << "from (" << viewpoint.x << ", " << viewpoint.y << ")";
		ASSERT_EQ(harness.mesh.validate(), "") << "from (" << viewpoint.x << ", " << viewpoint.y << ")";
	}
	EXPECT_GT(added, 10);
	EXPECT_TRUE(matchesFresh(harness.mesh, harness.model));
	EXPECT_TRUE(agreesWithTheRingsAsGiven(harness.mesh, harness.model));
}

// A leg's capsule walked again 1 to 40 mm to either side, each re-walk's far end 3 mm
// further over than its near end: edges nearly coincident and nearly parallel with the
// union's, at a slant and far from the origin.
TEST(PatchableMesh, RewalkedLegsMatchFreshBuild) {
	const Vec2i64	   origin{37123456, -18456789};
	const Vec2i64	   along{95424, 29737}; // 100 m at 17.3 degrees
	const std::int64_t length = 99950;		// |along|, rounded
	Harness			   harness;
	harness.add(bounds(capsule(origin, origin + along, 30000)));
	for (const std::int64_t unit : {std::int64_t{1}, std::int64_t{10}}) {
		for (std::int64_t i = 1; i <= 4; ++i) {
			const std::int64_t offset = (i % 2 == 0 ? 1 : -1) * i * unit;
			const Vec2i64	   start{origin.x + roundedAway(-along.y * offset, length), origin.y + roundedAway(along.x * offset, length)};
			const Vec2i64	   end{origin.x + along.x + roundedAway(-along.y * (offset + 3), length), origin.y + along.y + roundedAway(along.x * (offset + 3), length)};
			const std::vector<Vec2i64> ring = capsule(start, end, 30000);
			if (harness.mesh.groundCovers(ring)) {
				continue;
			}
			harness.add(bounds(ring));
			ASSERT_EQ(harness.last.unreadCovers, 0) << "offset " << offset;
			ASSERT_EQ(harness.mesh.validate(), "") << "offset " << offset;
		}
	}
	EXPECT_TRUE(matchesFresh(harness.mesh, harness.model));
}

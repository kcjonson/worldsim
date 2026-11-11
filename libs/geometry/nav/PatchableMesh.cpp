#include "PatchableMesh.h"

#include "../arrangement/Arrangement.h"
#include "../predicates/Predicates.h"
#include "../triangulation/ConstrainedDelaunay.h"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <set>
#include <sstream>
#include <utility>

namespace geometry::nav {

	namespace {

		// How far snap rounding can leave a ring's edges from its original lines, with
		// margin: each rounding moves a crossing under a millimetre.
		constexpr std::int64_t kSnapReachMm = 2;

		int next3(int i) {
			return i == 2 ? 0 : i + 1;
		}

		int prev3(int i) {
			return i == 0 ? 2 : i - 1;
		}

		std::int64_t floorDiv(std::int64_t a, std::int64_t b) {
			std::int64_t q = a / b;
			if ((a % b != 0) && ((a < 0) != (b < 0))) {
				--q;
			}
			return q;
		}

		bool boxesOverlap(const Vec2i64& aLo, const Vec2i64& aHi, const Vec2i64& bLo, const Vec2i64& bHi) {
			return aLo.x <= bHi.x && bLo.x <= aHi.x && aLo.y <= bHi.y && bLo.y <= aHi.y;
		}

		void growBox(Vec2i64& lo, Vec2i64& hi, const Vec2i64& p) {
			lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
			hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
		}

		void outlineBox(const std::vector<Vec2i64>& outline, Vec2i64& lo, Vec2i64& hi) {
			lo = outline.front();
			hi = outline.front();
			for (const Vec2i64& p : outline) {
				growBox(lo, hi, p);
			}
		}

		bool insideOrOn(const Vec2i64& a, const Vec2i64& b, const Vec2i64& c, const Vec2i64& p) {
			return orientation(a, b, p) != Orientation::Clockwise && orientation(b, c, p) != Orientation::Clockwise &&
				   orientation(c, a, p) != Orientation::Clockwise;
		}

		bool strictlyInside(const Vec2i64& a, const Vec2i64& b, const Vec2i64& c, const Vec2i64& p) {
			return orientation(a, b, p) == Orientation::CounterClockwise &&
				   orientation(b, c, p) == Orientation::CounterClockwise &&
				   orientation(c, a, p) == Orientation::CounterClockwise;
		}

		bool onClosedSegment(const Vec2i64& p, const Vec2i64& a, const Vec2i64& b) {
			return orientation(a, b, p) == Orientation::Collinear && p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x) &&
				   p.y >= std::min(a.y, b.y) && p.y <= std::max(a.y, b.y);
		}

		// Does closed segment ab meet the closed triangle?
		bool segmentTouchesTriangle(const Vec2i64& a, const Vec2i64& b, const std::array<Vec2i64, 3>& t) {
			if (insideOrOn(t[0], t[1], t[2], a) || insideOrOn(t[0], t[1], t[2], b)) {
				return true;
			}
			for (int i = 0; i < 3; ++i) {
				if (intersectSegments(a, b, t[i], t[next3(i)]).relation != SegmentRelation::Disjoint) {
					return true;
				}
			}
			return false;
		}

		// Does closed segment ab meet the closed triangle anywhere but at its corners?
		bool segmentEntersTriangle(const Vec2i64& a, const Vec2i64& b, const std::array<Vec2i64, 3>& t) {
			if (strictlyInside(t[0], t[1], t[2], a) || strictlyInside(t[0], t[1], t[2], b)) {
				return true;
			}
			for (int i = 0; i < 3; ++i) {
				const SegmentIntersection r = intersectSegments(a, b, t[i], t[next3(i)]);
				switch (r.relation) {
					case SegmentRelation::Disjoint:
						break;
					case SegmentRelation::ProperCrossing:
					case SegmentRelation::CollinearOverlap:
						return true;
					case SegmentRelation::EndpointTouch:
						if (r.point != t[0] && r.point != t[1] && r.point != t[2]) {
							return true;
						}
						break;
				}
			}
			return false;
		}

		// Do closed segments ab and pq come within `reach` of each other?
		bool segmentsWithin(const Vec2i64& a, const Vec2i64& b, const Vec2i64& p, const Vec2i64& q, std::int64_t reach) {
			if (intersectSegments(a, b, p, q).relation != SegmentRelation::Disjoint) {
				return true;
			}
			return withinDistanceOfSegment(a, p, q, reach) || withinDistanceOfSegment(b, p, q, reach) ||
				   withinDistanceOfSegment(p, a, b, reach) || withinDistanceOfSegment(q, a, b, reach);
		}

		bool isOriginalEndpoint(const std::vector<Vec2i64>& outline, std::int32_t edge, const Vec2i64& p) {
			const std::size_t i = static_cast<std::size_t>(edge);
			return outline[i] == p || outline[(i + 1) % outline.size()] == p;
		}

		Vec2i64 edgeStart(const std::vector<Vec2i64>& outline, std::int32_t edge) {
			return outline[static_cast<std::size_t>(edge)];
		}

		Vec2i64 edgeEnd(const std::vector<Vec2i64>& outline, std::int32_t edge) {
			return outline[(static_cast<std::size_t>(edge) + 1) % outline.size()];
		}

		// cover xor the rings the sources name an odd number of times; ascending.
		std::vector<RingId> toggled(const std::vector<RingId>& cover, const std::vector<EdgeSource>& sources) {
			std::vector<RingId> flips;
			flips.reserve(sources.size());
			for (const EdgeSource& s : sources) {
				flips.push_back(s.ring);
			}
			std::sort(flips.begin(), flips.end());
			std::vector<RingId> odd;
			for (std::size_t i = 0; i < flips.size();) {
				std::size_t j = i;
				while (j < flips.size() && flips[j] == flips[i]) {
					++j;
				}
				if ((j - i) % 2 == 1) {
					odd.push_back(flips[i]);
				}
				i = j;
			}
			std::vector<RingId> out;
			std::set_symmetric_difference(cover.begin(), cover.end(), odd.begin(), odd.end(), std::back_inserter(out));
			return out;
		}

	} // namespace

	// --- InternTable -------------------------------------------------------------

	template <typename Value> std::int32_t PatchableMesh::InternTable<Value>::acquire(const Value& value) {
		auto it = ids.find(value);
		if (it != ids.end()) {
			++refs[static_cast<std::size_t>(it->second)];
			return it->second;
		}
		std::int32_t id = 0;
		if (!freeIds.empty()) {
			id = freeIds.back();
			freeIds.pop_back();
		} else {
			id = static_cast<std::int32_t>(values.size());
			values.emplace_back();
			refs.push_back(0);
		}
		values[static_cast<std::size_t>(id)] = value;
		refs[static_cast<std::size_t>(id)]	  = 1;
		ids.emplace(value, id);
		return id;
	}

	template <typename Value> void PatchableMesh::InternTable<Value>::release(std::int32_t id) {
		assert(refs[static_cast<std::size_t>(id)] > 0);
		if (--refs[static_cast<std::size_t>(id)] == 0) {
			ids.erase(values[static_cast<std::size_t>(id)]);
			values[static_cast<std::size_t>(id)] = {};
			freeIds.push_back(id);
		}
	}

	// --- LocateGrid --------------------------------------------------------------

	std::size_t PatchableMesh::LocateGrid::CellHash::operator()(const CellKey& k) const {
		const std::uint64_t h = static_cast<std::uint64_t>(k.x) * 0x9E3779B97F4A7C15ULL ^ static_cast<std::uint64_t>(k.y);
		return static_cast<std::size_t>(h ^ (h >> 29));
	}

	int PatchableMesh::LocateGrid::levelFor(const Vec2i64& lo, const Vec2i64& hi) {
		const std::int64_t side	 = std::max(hi.x - lo.x, hi.y - lo.y);
		int				   level = 0;
		while (level < kLevels - 1 && (kBaseCell << level) < side) {
			++level;
		}
		return level;
	}

	void PatchableMesh::LocateGrid::insert(std::int32_t tri, const Vec2i64& lo, const Vec2i64& hi) {
		const int		   level = levelFor(lo, hi);
		const std::int64_t cell	 = kBaseCell << level;
		for (std::int64_t cy = floorDiv(lo.y, cell); cy <= floorDiv(hi.y, cell); ++cy) {
			for (std::int64_t cx = floorDiv(lo.x, cell); cx <= floorDiv(hi.x, cell); ++cx) {
				levels[static_cast<std::size_t>(level)][{cx, cy}].push_back(tri);
			}
		}
		++levelCounts[static_cast<std::size_t>(level)];
	}

	void PatchableMesh::LocateGrid::erase(std::int32_t tri, const Vec2i64& lo, const Vec2i64& hi) {
		const int		   level = levelFor(lo, hi);
		const std::int64_t cell	 = kBaseCell << level;
		Level&			   map	 = levels[static_cast<std::size_t>(level)];
		for (std::int64_t cy = floorDiv(lo.y, cell); cy <= floorDiv(hi.y, cell); ++cy) {
			for (std::int64_t cx = floorDiv(lo.x, cell); cx <= floorDiv(hi.x, cell); ++cx) {
				auto it = map.find({cx, cy});
				assert(it != map.end());
				std::vector<std::int32_t>& list = it->second;
				auto					   pos	= std::find(list.begin(), list.end(), tri);
				assert(pos != list.end());
				*pos = list.back();
				list.pop_back();
				if (list.empty()) {
					map.erase(it);
				}
			}
		}
		--levelCounts[static_cast<std::size_t>(level)];
	}

	template <typename Visit>
	void PatchableMesh::LocateGrid::forEachInBox(const Vec2i64& lo, const Vec2i64& hi, std::int64_t scale, Visit visit) const {
		for (int level = 0; level < kLevels; ++level) {
			if (levelCounts[static_cast<std::size_t>(level)] == 0) {
				continue;
			}
			const Level&	   map	= levels[static_cast<std::size_t>(level)];
			const std::int64_t cell = (kBaseCell << level) * scale;
			const std::int64_t x0	= floorDiv(lo.x, cell);
			const std::int64_t x1	= floorDiv(hi.x, cell);
			const std::int64_t y0	= floorDiv(lo.y, cell);
			const std::int64_t y1	= floorDiv(hi.y, cell);
			const double	   span = static_cast<double>(x1 - x0 + 1) * static_cast<double>(y1 - y0 + 1);
			if (span > static_cast<double>(map.size())) {
				for (const auto& [key, list] : map) {
					if (key.x >= x0 && key.x <= x1 && key.y >= y0 && key.y <= y1) {
						for (std::int32_t t : list) {
							visit(t);
						}
					}
				}
				continue;
			}
			for (std::int64_t cy = y0; cy <= y1; ++cy) {
				for (std::int64_t cx = x0; cx <= x1; ++cx) {
					auto it = map.find({cx, cy});
					if (it == map.end()) {
						continue;
					}
					for (std::int32_t t : it->second) {
						visit(t);
					}
				}
			}
		}
	}

	template <typename Visit> void PatchableMesh::LocateGrid::forEachEntry(Visit visit) const {
		for (const Level& map : levels) {
			for (const auto& [key, list] : map) {
				for (std::int32_t t : list) {
					visit(t);
				}
			}
		}
	}

	// --- PatchableMesh helpers -------------------------------------------------------

	std::size_t PatchableMesh::VertexHash::operator()(const Vec2i64& p) const {
		const std::uint64_t h = static_cast<std::uint64_t>(p.x) * 0x9E3779B97F4A7C15ULL ^ static_cast<std::uint64_t>(p.y);
		return static_cast<std::size_t>(h ^ (h >> 31));
	}

	std::uint64_t PatchableMesh::sourceKey(const EdgeSource& s) {
		return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(s.ring)) << 32) | static_cast<std::uint32_t>(s.edge);
	}

	void PatchableMesh::triangleBox(std::int32_t t, Vec2i64& lo, Vec2i64& hi) const {
		const NavTriangle& tri = tris[static_cast<std::size_t>(t)];
		lo					   = verts[tri.v[0]];
		hi					   = lo;
		growBox(lo, hi, verts[tri.v[1]]);
		growBox(lo, hi, verts[tri.v[2]]);
	}

	std::int32_t PatchableMesh::locateScaled(const Vec2i64& p, std::int64_t scale) const {
		std::int32_t best = -1;
		grid.forEachInBox(p, p, scale, [&](std::int32_t t) {
			if (best >= 0 && t >= best) {
				return;
			}
			const NavTriangle& tri = tris[static_cast<std::size_t>(t)];
			if (insideOrOn(verts[tri.v[0]] * scale, verts[tri.v[1]] * scale, verts[tri.v[2]] * scale, p)) {
				best = t;
			}
		});
		return best;
	}

	std::int32_t PatchableMesh::locate(const Vec2i64& p) const {
		return locateScaled(p, 1);
	}

	template <typename Visit> void PatchableMesh::forEachLiveInBox(const Vec2i64& lo, const Vec2i64& hi, Visit visit) const {
		std::vector<std::int32_t> found;
		grid.forEachInBox(lo, hi, 1, [&](std::int32_t t) { found.push_back(t); });
		std::sort(found.begin(), found.end());
		found.erase(std::unique(found.begin(), found.end()), found.end());
		for (std::int32_t t : found) {
			Vec2i64 tLo;
			Vec2i64 tHi;
			triangleBox(t, tLo, tHi);
			if (boxesOverlap(lo, hi, tLo, tHi)) {
				visit(t);
			}
		}
	}

	template <typename Visit> void PatchableMesh::forEachAroundVertex(std::uint32_t v, Visit visit) const {
		const Vec2i64& p = verts[v];
		forEachLiveInBox(p, p, [&](std::int32_t t) {
			const NavTriangle& tri = tris[static_cast<std::size_t>(t)];
			for (int i = 0; i < 3; ++i) {
				if (tri.v[i] == v) {
					visit(t, i);
				}
			}
		});
	}

	const std::vector<EdgeSource>& PatchableMesh::edgeSources(std::int32_t tri, int edge) const {
		static const std::vector<EdgeSource> kNone;
		const std::int32_t					 id = edgeSourceIds[static_cast<std::size_t>(tri)][static_cast<std::size_t>(edge)];
		return id < 0 ? kNone : sourceLists.get(id);
	}

	const std::vector<RingId>& PatchableMesh::cover(std::int32_t tri) const {
		return covers.get(coverIds[static_cast<std::size_t>(tri)]);
	}

	std::int32_t PatchableMesh::acquireCover(const std::vector<RingId>& ringSet) {
		const std::int32_t id = covers.acquire(ringSet);
		if (covers.refCount(id) == 1) {
			if (coverTags.size() <= static_cast<std::size_t>(id)) {
				coverTags.resize(static_cast<std::size_t>(id) + 1);
			}
			std::vector<const SampleRing*> containing;
			for (RingId r : ringSet) {
				const RegisteredRing& reg = rings[static_cast<std::size_t>(r)];
				if (reg.polygon.blocked) {
					containing.push_back(&reg.sample);
				}
			}
			coverTags[static_cast<std::size_t>(id)] = classifyCover(containing);
		}
		return id;
	}

	void PatchableMesh::setEdgeSources(std::int32_t t, int e, std::int32_t sourceId) {
		edgeSourceIds[static_cast<std::size_t>(t)][static_cast<std::size_t>(e)] = sourceId;
		if (sourceId < 0) {
			return;
		}
		for (const EdgeSource& s : sourceLists.get(sourceId)) {
			halfEdgesOf[sourceKey(s)].push_back(t * 3 + e);
		}
	}

	void PatchableMesh::clearEdgeSources(std::int32_t t, int e) {
		const std::int32_t sourceId = edgeSourceIds[static_cast<std::size_t>(t)][static_cast<std::size_t>(e)];
		if (sourceId < 0) {
			return;
		}
		unregisterEdgeSources(t, e);
		sourceLists.release(sourceId);
	}

	void PatchableMesh::unregisterEdgeSources(std::int32_t t, int e) {
		const std::int32_t sourceId = edgeSourceIds[static_cast<std::size_t>(t)][static_cast<std::size_t>(e)];
		if (sourceId < 0) {
			return;
		}
		for (const EdgeSource& s : sourceLists.get(sourceId)) {
			auto					   it	= halfEdgesOf.find(sourceKey(s));
			std::vector<std::int32_t>& list = it->second;
			auto					   pos	= std::find(list.begin(), list.end(), t * 3 + e);
			assert(pos != list.end());
			*pos = list.back();
			list.pop_back();
			if (list.empty()) {
				halfEdgesOf.erase(it);
			}
		}
		edgeSourceIds[static_cast<std::size_t>(t)][static_cast<std::size_t>(e)] = -1;
	}

	// Flip edge i of t = (x, y, p) against u = (y, x, d): t becomes (p, x, d), u becomes
	// (p, d, y). Only an unconstrained edge flips, so both share one cover.
	void PatchableMesh::flip(std::int32_t t, int i) {
		NavTriangle&	   tt = tris[static_cast<std::size_t>(t)];
		const std::int32_t u  = tt.neighbor[static_cast<std::size_t>(i)];
		NavTriangle&	   uu = tris[static_cast<std::size_t>(u)];
		int				   j  = 0;
		while (uu.neighbor[static_cast<std::size_t>(j)] != t) {
			++j;
		}
		assert(coverIds[static_cast<std::size_t>(t)] == coverIds[static_cast<std::size_t>(u)]);

		Vec2i64 lo;
		Vec2i64 hi;
		triangleBox(t, lo, hi);
		grid.erase(t, lo, hi);
		triangleBox(u, lo, hi);
		grid.erase(u, lo, hi);

		struct Side {
			std::int32_t neighbor;
			std::int32_t sources;
			std::int64_t provenance;
		};
		auto take = [&](std::int32_t tri, int e) {
			NavTriangle& x = tris[static_cast<std::size_t>(tri)];
			const Side	 side{x.neighbor[static_cast<std::size_t>(e)], edgeSourceIds[static_cast<std::size_t>(tri)][static_cast<std::size_t>(e)],
							  x.edgeProvenance[static_cast<std::size_t>(e)]};
			unregisterEdgeSources(tri, e);
			return side;
		};
		const std::uint32_t x	= tt.v[static_cast<std::size_t>(i)];
		const std::uint32_t y	= tt.v[static_cast<std::size_t>(next3(i))];
		const std::uint32_t p	= tt.v[static_cast<std::size_t>(prev3(i))];
		const std::uint32_t d	= uu.v[static_cast<std::size_t>(prev3(j))];
		const Side			sYP = take(t, next3(i));
		const Side			sPX = take(t, prev3(i));
		const Side			sXD = take(u, next3(j));
		const Side			sDY = take(u, prev3(j));

		auto place = [&](std::int32_t tri, int e, const Side& side) {
			NavTriangle& x								   = tris[static_cast<std::size_t>(tri)];
			x.neighbor[static_cast<std::size_t>(e)]		   = side.neighbor;
			x.edgeProvenance[static_cast<std::size_t>(e)] = side.provenance;
			setEdgeSources(tri, e, side.sources);
		};
		tt.v = {p, x, d};
		uu.v = {p, d, y};
		--vertexUses[x];
		--vertexUses[y];
		++vertexUses[p];
		++vertexUses[d];
		place(t, 0, sPX);
		place(t, 1, sXD);
		place(t, 2, {u, -1, kNoProvenance});
		place(u, 0, {t, -1, kNoProvenance});
		place(u, 1, sDY);
		place(u, 2, sYP);
		if (sXD.neighbor >= 0) {
			NavTriangle& n = tris[static_cast<std::size_t>(sXD.neighbor)];
			for (int k = 0; k < 3; ++k) {
				if (n.neighbor[static_cast<std::size_t>(k)] == u) {
					n.neighbor[static_cast<std::size_t>(k)] = t;
				}
			}
		}
		if (sYP.neighbor >= 0) {
			NavTriangle& n = tris[static_cast<std::size_t>(sYP.neighbor)];
			for (int k = 0; k < 3; ++k) {
				if (n.neighbor[static_cast<std::size_t>(k)] == t) {
					n.neighbor[static_cast<std::size_t>(k)] = u;
				}
			}
		}

		triangleBox(t, lo, hi);
		grid.insert(t, lo, hi);
		triangleBox(u, lo, hi);
		grid.insert(u, lo, hi);
	}

	// Lawson: flip every unconstrained edge whose far apex is strictly inside the
	// near triangle's circumcircle, until none is.
	void PatchableMesh::legalize(std::vector<std::pair<std::int32_t, int>>& stack, std::vector<std::int32_t>& flipped) {
		while (!stack.empty()) {
			const auto [t, e] = stack.back();
			stack.pop_back();
			const NavTriangle& tt = tris[static_cast<std::size_t>(t)];
			const std::int32_t u  = tt.neighbor[static_cast<std::size_t>(e)];
			if (u < 0 || edgeSourceIds[static_cast<std::size_t>(t)][static_cast<std::size_t>(e)] >= 0) {
				continue;
			}
			const NavTriangle& uu = tris[static_cast<std::size_t>(u)];
			int				   j  = 0;
			while (uu.neighbor[static_cast<std::size_t>(j)] != t) {
				++j;
			}
			const Vec2i64& apex = verts[uu.v[static_cast<std::size_t>(prev3(j))]];
			if (inCircle(verts[tt.v[0]], verts[tt.v[1]], verts[tt.v[2]], apex) != InCircle::Inside) {
				continue;
			}
			flip(t, e);
			flipped.push_back(t);
			flipped.push_back(u);
			stack.push_back({t, 0});
			stack.push_back({t, 1});
			stack.push_back({u, 1});
			stack.push_back({u, 2});
		}
	}

	// --- The patch ---------------------------------------------------------------

	class PatchableMesh::Patch {
	  public:
		Patch(PatchableMesh& owner, const PatchEdits& edits) : m(owner) { begin(edits); }

		PatchReport run();

	  private:
		struct Change {
			bool			hadOld = false;
			bool			hasNew = false;
			NavInputPolygon polygon;
			SampleRing		sample;
			Vec2i64			lo;
			Vec2i64			hi;
		};

		// A mesh edge in or around the hole that goes into the arrangement unchanged.
		struct Fixed {
			std::uint32_t			a = 0;
			std::uint32_t			b = 0;
			std::vector<EdgeSource> sources; // all of them outside s
			bool					boundary	= false; // between the hole and a kept triangle
			std::int32_t			outside		= -1;
			int						outsideEdge = -1;
		};

		struct LocalSegment {
			EdgeSource	 source;
			std::int32_t fixed = -1;
		};

		enum class FaceKind { Frame, Hole, Discard, NewGround, Sink };

		struct LocalFace {
			FaceKind				  kind		= FaceKind::Frame;
			std::int32_t			  sampleTri = -1;
			Int128					  area2		= Int128(0);
			std::int32_t			  oldTri	= -1;
			std::vector<std::int32_t> tris;
			bool					  assigned = false;
			std::vector<RingId>		  cover;
		};

		void begin(const PatchEdits& edits);
		RingId allocateRing();

		const std::vector<Vec2i64>& oldOutline(RingId r) const { return m.rings[static_cast<std::size_t>(r)].polygon.ring; }
		const NavInputPolygon&		newPolygon(RingId r) const;
		const SampleRing&			newSample(RingId r) const;
		bool						removed(RingId r) const;

		void addToS(const EdgeSource& z);
		void pullOffGround(const Vec2i64& a, const Vec2i64& b);
		void pullAtVertex(std::uint32_t v, const Vec2i64& p, const std::vector<std::pair<EdgeSource, bool>>& xs);
		void walkNewOutlines();
		void closeS();
		bool deriveOnce();
		void arrange();
		bool arrangementHeld();
		void triangulate();
		void assignCovers();
		bool coversHeld();
		std::vector<EdgeSource> sourcesOf(std::int32_t constraint) const;
		void collectFixed();
		void splice();

		PatchableMesh& m;
		PatchReport	   out;

		std::map<RingId, Change> changes;
		std::set<EdgeSource>	 s;
		std::vector<EdgeSource>	 queue;
		std::set<RingId>		 testRings;
		std::set<std::int32_t>	 hole;

		std::vector<std::uint8_t> inHole;
		std::vector<Fixed>		  fixed;
		std::vector<LocalSegment> segments;
		std::size_t				  fixedStart = 0;
		Arrangement				  arrangement;
		ConstrainedDelaunay		  cdt;
		std::vector<std::int32_t> faceOf;
		std::vector<LocalFace>	  faces;
		std::vector<std::int32_t> boundaryFixedOf; // per arrangement edge: boundary Fixed index, or -1
	};

	RingId PatchableMesh::Patch::allocateRing() {
		if (!m.freeRings.empty()) {
			const RingId id = m.freeRings.back();
			m.freeRings.pop_back();
			return id;
		}
		m.rings.emplace_back();
		return static_cast<RingId>(m.rings.size()) - 1;
	}

	const NavInputPolygon& PatchableMesh::Patch::newPolygon(RingId r) const {
		auto it = changes.find(r);
		return it != changes.end() ? it->second.polygon : m.rings[static_cast<std::size_t>(r)].polygon;
	}

	const SampleRing& PatchableMesh::Patch::newSample(RingId r) const {
		auto it = changes.find(r);
		return it != changes.end() ? it->second.sample : m.rings[static_cast<std::size_t>(r)].sample;
	}

	bool PatchableMesh::Patch::removed(RingId r) const {
		auto it = changes.find(r);
		return it != changes.end() && !it->second.hasNew;
	}

	void PatchableMesh::Patch::addToS(const EdgeSource& z) {
		if (s.insert(z).second) {
			queue.push_back(z);
			testRings.insert(z.ring);
		}
	}

	// The mesh shows every interaction on kept ground, but a segment that reaches off
	// ground can meet a hanging ring there, and the rounded crossing can still land on
	// ground. Off ground and near its edge the registry stands in: every blocked ring
	// edge whose original comes within two snap reaches joins s. Bounds edges never
	// need it, since every point of one borders its own ground.
	void PatchableMesh::Patch::pullOffGround(const Vec2i64& a, const Vec2i64& b) {
		const Vec2i64 reach{kSnapReachMm, kSnapReachMm};
		Vec2i64		  lo = a;
		Vec2i64		  hi = a;
		growBox(lo, hi, b);
		lo				  = lo - reach;
		hi				  = hi + reach;
		bool nearBoundary = false;
		m.forEachLiveInBox(lo, hi, [&](std::int32_t t) {
			const NavTriangle& tri = m.tris[static_cast<std::size_t>(t)];
			for (int e = 0; e < 3; ++e) {
				if (tri.neighbor[static_cast<std::size_t>(e)] >= 0) {
					continue;
				}
				const Vec2i64& p   = m.verts[tri.v[static_cast<std::size_t>(e)]];
				const Vec2i64& q   = m.verts[tri.v[static_cast<std::size_t>(next3(e))]];
				Vec2i64		   eLo = p;
				Vec2i64		   eHi = p;
				growBox(eLo, eHi, q);
				if (!boxesOverlap(lo, hi, eLo, eHi)) {
					continue;
				}
				nearBoundary = true;
				// Off ground a segment can still end on the boundary, at a vertex with
				// no piece of it on the mesh.
				if (segmentsWithin(a, b, p, q, 2 * kSnapReachMm)) {
					for (const EdgeSource& z : m.edgeSources(t, e)) {
						addToS(z);
					}
				}
			}
		});
		if (!nearBoundary && m.locateScaled(a + b, 2) >= 0) {
			return;
		}
		lo = lo - reach;
		hi = hi + reach;
		for (RingId r = 0; r < static_cast<RingId>(m.rings.size()); ++r) {
			const RegisteredRing& reg = m.rings[static_cast<std::size_t>(r)];
			if (!reg.live || !reg.polygon.blocked || changes.count(r) != 0 || !boxesOverlap(reg.lo, reg.hi, lo, hi)) {
				continue;
			}
			const std::vector<Vec2i64>& outline = reg.polygon.ring;
			for (std::int32_t e = 0; e < static_cast<std::int32_t>(outline.size()); ++e) {
				const Vec2i64 p = edgeStart(outline, e);
				const Vec2i64 q = edgeEnd(outline, e);
				if (p != q && s.count({r, e}) == 0 && segmentsWithin(a, b, p, q, 2 * kSnapReachMm)) {
					addToS({r, e});
				}
			}
		}
	}

	void PatchableMesh::Patch::begin(const PatchEdits& edits) {
		auto setNew = [](Change& c, NavInputPolygon polygon) {
			assert(polygon.ring.size() >= 3 && "PatchableMesh: a ring needs three vertices");
			c.hasNew  = true;
			c.polygon = std::move(polygon);
			c.sample  = sampleRing(c.polygon);
			outlineBox(c.polygon.ring, c.lo, c.hi);
		};
		for (const NavInputPolygon& add : edits.adds) {
			const RingId id = allocateRing();
			out.added.push_back(id);
			setNew(changes[id], add);
		}
		for (RingId id : edits.removes) {
			assert(m.rings[static_cast<std::size_t>(id)].live && changes.count(id) == 0);
			changes[id].hadOld = true;
		}
		for (const RingMove& move : edits.moves) {
			assert(m.rings[static_cast<std::size_t>(move.ring)].live && changes.count(move.ring) == 0);
			Change& c	   = changes[move.ring];
			c.hadOld	   = true;
			NavInputPolygon moved = m.rings[static_cast<std::size_t>(move.ring)].polygon;
			moved.ring			  = move.outline;
			setNew(c, std::move(moved));
		}

		for (const auto& [id, c] : changes) {
			if (c.hasNew) {
				testRings.insert(id);
			}
			if (c.hadOld) {
				testRings.insert(id);
				const std::int32_t n = static_cast<std::int32_t>(oldOutline(id).size());
				for (std::int32_t e = 0; e < n; ++e) {
					addToS({id, e});
				}
			}
		}

		// New ground completes every ring hanging over it: its edges over the new bounds
		// re-derive there, and a ring around the whole new bounds still gets tested.
		for (const auto& [id, c] : changes) {
			if (!c.hasNew || c.polygon.blocked) {
				continue;
			}
			for (RingId r = 0; r < static_cast<RingId>(m.rings.size()); ++r) {
				const RegisteredRing& reg = m.rings[static_cast<std::size_t>(r)];
				if (!reg.live || changes.count(r) != 0 || !boxesOverlap(reg.lo, reg.hi, c.lo, c.hi)) {
					continue;
				}
				testRings.insert(r);
				const std::vector<Vec2i64>& outline = reg.polygon.ring;
				for (std::int32_t e = 0; e < static_cast<std::int32_t>(outline.size()); ++e) {
					Vec2i64 lo = edgeStart(outline, e);
					Vec2i64 hi = lo;
					growBox(lo, hi, edgeEnd(outline, e));
					if (boxesOverlap(lo, hi, c.lo, c.hi)) {
						addToS({r, e});
					}
				}
			}
		}
	}

	// Every ring edge on the mesh at vertex v joins s unless v is an original corner of
	// both it and each x touching v there: a corner of neither, or of one only, means one
	// split the other at v.
	void PatchableMesh::Patch::pullAtVertex(
		std::uint32_t v, const Vec2i64& p, const std::vector<std::pair<EdgeSource, bool>>& xs) {
		m.forEachAroundVertex(v, [&](std::int32_t t, int i) {
			for (int e : {i, prev3(i)}) {
				for (const EdgeSource& z : m.edgeSources(t, e)) {
					if (s.count(z) != 0) {
						continue;
					}
					const bool zCorner = isOriginalEndpoint(oldOutline(z.ring), z.edge, p);
					for (const auto& [x, xCorner] : xs) {
						if (!xCorner || !zCorner) {
							addToS(z);
							break;
						}
					}
				}
			}
		});
	}

	// The triangles a new outline overlaps join the hole, and every ring edge its
	// segments cross or touch joins s.
	void PatchableMesh::Patch::walkNewOutlines() {
		for (const auto& [id, c] : changes) {
			if (!c.hasNew) {
				continue;
			}
			const std::vector<Vec2i64>& outline = c.polygon.ring;
			for (std::int32_t e = 0; e < static_cast<std::int32_t>(outline.size()); ++e) {
				const Vec2i64 a = edgeStart(outline, e);
				const Vec2i64 b = edgeEnd(outline, e);
				if (a == b) {
					continue;
				}
				pullOffGround(a, b);
				Vec2i64 lo = a;
				Vec2i64 hi = a;
				growBox(lo, hi, b);
				m.forEachLiveInBox(lo, hi, [&](std::int32_t t) {
					const NavTriangle&			 tri = m.tris[static_cast<std::size_t>(t)];
					const std::array<Vec2i64, 3> pts{m.verts[tri.v[0]], m.verts[tri.v[1]], m.verts[tri.v[2]]};
					if (!segmentTouchesTriangle(a, b, pts)) {
						return;
					}
					hole.insert(t);
					for (int k = 0; k < 3; ++k) {
						const std::vector<EdgeSource>& sources = m.edgeSources(t, k);
						if (sources.empty()) {
							continue;
						}
						const Vec2i64&			  p = pts[static_cast<std::size_t>(k)];
						const Vec2i64&			  q = pts[static_cast<std::size_t>(next3(k))];
						const SegmentIntersection r = intersectSegments(a, b, p, q);
						if (r.relation == SegmentRelation::Disjoint ||
							(r.relation == SegmentRelation::EndpointTouch && (r.point == p || r.point == q))) {
							continue;
						}
						for (const EdgeSource& z : sources) {
							addToS(z);
						}
					}
					for (int k = 0; k < 3; ++k) {
						const Vec2i64& p = pts[static_cast<std::size_t>(k)];
						if (onClosedSegment(p, a, b)) {
							pullAtVertex(tri.v[static_cast<std::size_t>(k)], p, {{{id, e}, p == a || p == b}});
						}
					}
				});
			}
			m.forEachLiveInBox(c.lo, c.hi, [&](std::int32_t t) {
				const NavTriangle& tri = m.tris[static_cast<std::size_t>(t)];
				if (ringContainsSample(c.sample, m.verts[tri.v[0]] + m.verts[tri.v[1]] + m.verts[tri.v[2]])) {
					hole.insert(t);
				}
			});
		}
		for (const auto& [id, c] : changes) {
			if (!c.hadOld) {
				continue;
			}
			const RegisteredRing& reg = m.rings[static_cast<std::size_t>(id)];
			m.forEachLiveInBox(reg.lo, reg.hi, [&](std::int32_t t) {
				const std::vector<RingId>& cov = m.cover(t);
				if (std::binary_search(cov.begin(), cov.end(), id)) {
					hole.insert(t);
				}
			});
		}
	}

	// Close s over shared vertices: the triangles along every piece join the hole, and
	// every vertex on a piece pulls in the edges it splits or that split it.
	void PatchableMesh::Patch::closeS() {
		while (!queue.empty()) {
			const EdgeSource x = queue.back();
			queue.pop_back();
			const Vec2i64 a = edgeStart(oldOutline(x.ring), x.edge);
			const Vec2i64 b = edgeEnd(oldOutline(x.ring), x.edge);
			if (a != b) {
				pullOffGround(a, b);
			}
			auto it = m.halfEdgesOf.find(sourceKey(x));
			if (it == m.halfEdgesOf.end()) {
				continue;
			}
			const std::vector<std::int32_t> halfEdges = it->second;
			std::vector<std::uint32_t>		ends;
			for (std::int32_t h : halfEdges) {
				const std::int32_t t   = h / 3;
				const int		   e   = h % 3;
				const NavTriangle& tri = m.tris[static_cast<std::size_t>(t)];
				hole.insert(t);
				ends.push_back(tri.v[static_cast<std::size_t>(e)]);
				ends.push_back(tri.v[static_cast<std::size_t>(next3(e))]);
			}
			std::sort(ends.begin(), ends.end());
			ends.erase(std::unique(ends.begin(), ends.end()), ends.end());
			// The whole star of each vertex: one that goes must not stay on the boundary.
			for (std::uint32_t v : ends) {
				const Vec2i64& p = m.verts[v];
				m.forEachAroundVertex(v, [&](std::int32_t t, int) { hole.insert(t); });
				pullAtVertex(v, p, {{x, isOriginalEndpoint(oldOutline(x.ring), x.edge, p)}});
			}
		}
	}

	void PatchableMesh::Patch::collectFixed() {
		inHole.assign(m.tris.size(), 0);
		for (std::int32_t t : hole) {
			inHole[static_cast<std::size_t>(t)] = 1;
		}
		fixed.clear();
		auto outsideS = [&](const std::vector<EdgeSource>& sources) {
			std::vector<EdgeSource> kept;
			for (const EdgeSource& z : sources) {
				if (s.count(z) == 0) {
					kept.push_back(z);
				}
			}
			return kept;
		};
		for (std::int32_t t : hole) {
			const NavTriangle& tri = m.tris[static_cast<std::size_t>(t)];
			for (int e = 0; e < 3; ++e) {
				const std::int32_t			   u	   = tri.neighbor[static_cast<std::size_t>(e)];
				const std::vector<EdgeSource>& sources = m.edgeSources(t, e);
				Fixed						   f;
				f.a = tri.v[static_cast<std::size_t>(e)];
				f.b = tri.v[static_cast<std::size_t>(next3(e))];
				if (u >= 0 && inHole[static_cast<std::size_t>(u)] != 0) {
					if (sources.empty() || u < t) {
						continue;
					}
					f.sources = outsideS(sources);
					if (f.sources.empty()) {
						continue;
					}
				} else if (u >= 0) {
					f.sources = outsideS(sources);
					assert(f.sources.size() == sources.size() && "PatchableMesh: a piece of s on the hole boundary");
					f.boundary = true;
					f.outside  = u;
					const NavTriangle& ut = m.tris[static_cast<std::size_t>(u)];
					for (int k = 0; k < 3; ++k) {
						if (ut.neighbor[static_cast<std::size_t>(k)] == t) {
							f.outsideEdge = k;
						}
					}
				} else {
					assert(!sources.empty() && "PatchableMesh: an unconstrained mesh boundary edge");
					f.sources = outsideS(sources);
					if (f.sources.empty()) {
						continue;
					}
				}
				fixed.push_back(std::move(f));
			}
		}
	}

	// One re-derivation of the hole. False when the boundary did not hold and the hole
	// grew; true when the arrangement, triangulation, and covers are ready to splice.
	bool PatchableMesh::Patch::deriveOnce() {
		++out.passes;
		collectFixed();
		arrange();
		if (!arrangementHeld()) {
			closeS();
			return false;
		}
		triangulate();
		assignCovers();
		return coversHeld();
	}

	// s's original edges (the new outline for a changed ring) and the fixed edges.
	void PatchableMesh::Patch::arrange() {
		segments.clear();
		std::vector<InputSegment> input;
		for (const EdgeSource& x : s) {
			if (changes.count(x.ring) != 0) {
				continue;
			}
			const std::vector<Vec2i64>& outline = oldOutline(x.ring);
			const Vec2i64				a		= edgeStart(outline, x.edge);
			const Vec2i64				b		= edgeEnd(outline, x.edge);
			if (a != b) {
				input.push_back({a, b, static_cast<std::int64_t>(segments.size())});
				segments.push_back({x, -1});
			}
		}
		for (const auto& [id, c] : changes) {
			if (!c.hasNew) {
				continue;
			}
			for (std::int32_t e = 0; e < static_cast<std::int32_t>(c.polygon.ring.size()); ++e) {
				const Vec2i64 a = edgeStart(c.polygon.ring, e);
				const Vec2i64 b = edgeEnd(c.polygon.ring, e);
				if (a != b) {
					input.push_back({a, b, static_cast<std::int64_t>(segments.size())});
					segments.push_back({{id, e}, -1});
				}
			}
		}
		fixedStart = segments.size();
		for (std::size_t f = 0; f < fixed.size(); ++f) {
			input.push_back({m.verts[fixed[f].a], m.verts[fixed[f].b], static_cast<std::int64_t>(segments.size())});
			segments.push_back({{}, static_cast<std::int32_t>(f)});
		}
		arrangement = buildArrangement(input);
	}

	// Every fixed edge came out whole and alone, every re-derived edge stayed in the hole,
	// and every old vertex one reached pulled in what it must. False when s or the hole
	// grew.
	bool PatchableMesh::Patch::arrangementHeld() {
		auto vertexIndex = [&](const Vec2i64& p) -> std::int64_t {
			auto it = std::lower_bound(arrangement.vertices.begin(), arrangement.vertices.end(), p);
			return it != arrangement.vertices.end() && *it == p ? it - arrangement.vertices.begin() : -1;
		};
		auto edgeIndex = [&](std::int64_t a, std::int64_t b) -> std::int64_t {
			if (a < 0 || b < 0) {
				return -1;
			}
			if (a > b) {
				std::swap(a, b);
			}
			auto it = std::lower_bound(arrangement.edges.begin(), arrangement.edges.end(), std::make_pair(a, b),
									   [](const ArrangementEdge& e, const std::pair<std::int64_t, std::int64_t>& key) {
										   return std::make_pair(static_cast<std::int64_t>(e.from), static_cast<std::int64_t>(e.to)) < key;
									   });
			return it != arrangement.edges.end() && static_cast<std::int64_t>(it->from) == a && static_cast<std::int64_t>(it->to) == b
					   ? it - arrangement.edges.begin()
					   : -1;
		};
		auto fromS = [&](std::int64_t local) { return static_cast<std::size_t>(local) < fixedStart; };

		bool grew = false;
		auto growFixed = [&](const Fixed& f) {
			for (const EdgeSource& z : f.sources) {
				addToS(z);
			}
			if (f.boundary) {
				hole.insert(f.outside);
			}
			grew = true;
		};

		// Each fixed edge must come out whole and alone.
		boundaryFixedOf.assign(arrangement.edges.size(), -1);
		for (std::size_t f = 0; f < fixed.size(); ++f) {
			const std::int64_t edge = edgeIndex(vertexIndex(m.verts[fixed[f].a]), vertexIndex(m.verts[fixed[f].b]));
			bool			   whole = edge >= 0;
			if (whole) {
				for (std::int64_t p : arrangement.edges[static_cast<std::size_t>(edge)].provenance) {
					whole = whole && !fromS(p);
				}
			}
			if (!whole) {
				growFixed(fixed[f]);
			} else if (fixed[f].boundary) {
				boundaryFixedOf[static_cast<std::size_t>(edge)] = static_cast<std::int32_t>(f);
			}
		}

		// Old vertices the re-derived edges reach, and the old triangles they enter.
		std::map<std::size_t, std::vector<std::pair<EdgeSource, bool>>> touching;
		for (const ArrangementEdge& ae : arrangement.edges) {
			const Vec2i64& p = arrangement.vertices[ae.from];
			const Vec2i64& q = arrangement.vertices[ae.to];
			bool		   derived = false;
			for (std::int64_t local : ae.provenance) {
				if (!fromS(local)) {
					continue;
				}
				derived						 = true;
				const EdgeSource&			 x		 = segments[static_cast<std::size_t>(local)].source;
				const std::vector<Vec2i64>& outline = newPolygon(x.ring).ring;
				touching[ae.from].push_back({x, isOriginalEndpoint(outline, x.edge, p)});
				touching[ae.to].push_back({x, isOriginalEndpoint(outline, x.edge, q)});
			}
			if (!derived) {
				continue;
			}
			Vec2i64 lo = p;
			Vec2i64 hi = p;
			growBox(lo, hi, q);
			m.forEachLiveInBox(lo, hi, [&](std::int32_t t) {
				if (inHole[static_cast<std::size_t>(t)] != 0) {
					return;
				}
				const NavTriangle& tri = m.tris[static_cast<std::size_t>(t)];
				if (segmentEntersTriangle(p, q, {m.verts[tri.v[0]], m.verts[tri.v[1]], m.verts[tri.v[2]]})) {
					hole.insert(t);
					grew = true;
				}
			});
		}
		for (const auto& [vi, xs] : touching) {
			const Vec2i64& p  = arrangement.vertices[vi];
			auto		   it = m.vertexAt.find(p);
			if (it == m.vertexAt.end()) {
				continue;
			}
			const std::size_t before = s.size();
			pullAtVertex(it->second, p, xs);
			grew = grew || s.size() != before;
		}
		return !grew;
	}

	// The hole arrangement triangulated, its faces found, and each face placed: in the
	// hole, on kept ground outside it, on new ground, or off every mesh.
	void PatchableMesh::Patch::triangulate() {
		std::vector<std::array<std::uint32_t, 2>> constraints;
		constraints.reserve(arrangement.edges.size());
		for (const ArrangementEdge& ae : arrangement.edges) {
			constraints.push_back({static_cast<std::uint32_t>(ae.from), static_cast<std::uint32_t>(ae.to)});
		}
		cdt = buildConstrainedDelaunay(arrangement.vertices, constraints);

		// Faces of the hole triangulation: flood across unconstrained edges.
		const std::int32_t cdtCount = static_cast<std::int32_t>(cdt.triangles.size());
		faceOf.assign(static_cast<std::size_t>(cdtCount), -1);
		faces.clear();
		std::vector<std::int32_t> fill;
		for (std::int32_t seed = 0; seed < cdtCount; ++seed) {
			if (faceOf[static_cast<std::size_t>(seed)] >= 0) {
				continue;
			}
			const std::int32_t f = static_cast<std::int32_t>(faces.size());
			faces.emplace_back();
			LocalFace& face = faces.back();
			bool	   frame = false;
			faceOf[static_cast<std::size_t>(seed)] = f;
			fill.push_back(seed);
			while (!fill.empty()) {
				const std::int32_t t = fill.back();
				fill.pop_back();
				face.tris.push_back(t);
				const CdtTriangle& tri	 = cdt.triangles[static_cast<std::size_t>(t)];
				const Int128	   area2 = cross(cdt.vertices[tri.v[1]] - cdt.vertices[tri.v[0]], cdt.vertices[tri.v[2]] - cdt.vertices[tri.v[0]]);
				if (face.sampleTri < 0 || face.area2 < area2 || (area2 == face.area2 && t < face.sampleTri)) {
					face.sampleTri = t;
					face.area2	   = area2;
				}
				for (int e = 0; e < 3; ++e) {
					frame				   = frame || tri.v[static_cast<std::size_t>(e)] >= cdt.frameStart;
					const std::int32_t nb = tri.neighbor[static_cast<std::size_t>(e)];
					if (nb >= 0 && tri.constraint[static_cast<std::size_t>(e)] < 0 && faceOf[static_cast<std::size_t>(nb)] < 0) {
						faceOf[static_cast<std::size_t>(nb)] = f;
						fill.push_back(nb);
					}
				}
			}
			if (frame) {
				continue;
			}
			const CdtTriangle& st	   = cdt.triangles[static_cast<std::size_t>(face.sampleTri)];
			const Vec2i64	   sample3 = cdt.vertices[st.v[0]] + cdt.vertices[st.v[1]] + cdt.vertices[st.v[2]];
			const std::int32_t old	   = m.locateScaled(sample3, 3);
			if (old >= 0) {
				face.kind	= inHole[static_cast<std::size_t>(old)] != 0 ? FaceKind::Hole : FaceKind::Discard;
				face.oldTri = old;
				continue;
			}
			face.kind = FaceKind::Sink;
			for (const auto& [id, c] : changes) {
				if (c.hasNew && !c.polygon.blocked && ringContainsSample(c.sample, sample3)) {
					face.kind = FaceKind::NewGround;
				}
			}
		}
	}

	// Covers: one exact seed per component, then toggle across each ring edge by the
	// rings on it. A sink (off the old mesh, outside every new bounds ring) takes a cover
	// but passes none on.
	void PatchableMesh::Patch::assignCovers() {
		const std::int32_t cdtCount = static_cast<std::int32_t>(cdt.triangles.size());
		bool conflict = false;
		auto flood	  = [&](std::int32_t seed, std::vector<RingId> seedCover) {
			   LocalFace& sf = faces[static_cast<std::size_t>(seed)];
			   sf.cover		 = std::move(seedCover);
			   sf.assigned	 = true;
			   std::vector<std::int32_t> stack{seed};
			while (!stack.empty()) {
				const std::int32_t f = stack.back();
				stack.pop_back();
				const LocalFace& face = faces[static_cast<std::size_t>(f)];
				if (face.kind == FaceKind::Sink) {
					continue;
				}
				for (std::int32_t t : face.tris) {
					const CdtTriangle& tri = cdt.triangles[static_cast<std::size_t>(t)];
					for (int e = 0; e < 3; ++e) {
						const std::int32_t c  = tri.constraint[static_cast<std::size_t>(e)];
						const std::int32_t nb = tri.neighbor[static_cast<std::size_t>(e)];
						if (c < 0 || nb < 0 || boundaryFixedOf[static_cast<std::size_t>(c)] >= 0) {
							continue;
						}
						const std::int32_t g	= faceOf[static_cast<std::size_t>(nb)];
						LocalFace&		   next = faces[static_cast<std::size_t>(g)];
						if (g == f || next.kind == FaceKind::Frame || next.kind == FaceKind::Discard) {
							continue;
						}
						std::vector<RingId> across = toggled(face.cover, sourcesOf(c));
						if (!next.assigned) {
							next.cover	  = std::move(across);
							next.assigned = true;
							stack.push_back(g);
						} else if (next.cover != across && next.kind != FaceKind::Sink) {
							conflict = true;
						}
					}
				}
			}
		};

		// Seeds, best first. Across the fixed boundary the kept triangle's cover carries
		// in exactly, with no sample. A component that touches no boundary (new ground,
		// or ground whose every triangle is in the hole) takes its largest triangle's
		// centroid: the old cover there for rings the patch leaves alone, and the exact
		// test for every ring it touches.
		for (std::int32_t t = 0; t < cdtCount; ++t) {
			const CdtTriangle& tri	= cdt.triangles[static_cast<std::size_t>(t)];
			const std::int32_t f	= faceOf[static_cast<std::size_t>(t)];
			LocalFace&		   face = faces[static_cast<std::size_t>(f)];
			if (face.assigned || (face.kind != FaceKind::Hole && face.kind != FaceKind::NewGround)) {
				continue;
			}
			for (int e = 0; e < 3 && !face.assigned; ++e) {
				const std::int32_t c = tri.constraint[static_cast<std::size_t>(e)];
				if (c >= 0 && boundaryFixedOf[static_cast<std::size_t>(c)] >= 0) {
					const Fixed& fx = fixed[static_cast<std::size_t>(boundaryFixedOf[static_cast<std::size_t>(c)])];
					flood(f, toggled(m.cover(fx.outside), fx.sources));
				}
			}
		}
		std::vector<std::int32_t> order;
		for (std::int32_t f = 0; f < static_cast<std::int32_t>(faces.size()); ++f) {
			const FaceKind kind = faces[static_cast<std::size_t>(f)].kind;
			if (kind == FaceKind::Hole || kind == FaceKind::NewGround) {
				order.push_back(f);
			}
		}
		std::sort(order.begin(), order.end(), [&](std::int32_t a, std::int32_t b) {
			const LocalFace& fa = faces[static_cast<std::size_t>(a)];
			const LocalFace& fb = faces[static_cast<std::size_t>(b)];
			const bool		 ha = fa.kind == FaceKind::Hole;
			const bool		 hb = fb.kind == FaceKind::Hole;
			if (ha != hb) {
				return ha;
			}
			if (fa.area2 != fb.area2) {
				return fb.area2 < fa.area2;
			}
			return a < b;
		});
		for (std::int32_t seed : order) {
			const LocalFace& sf = faces[static_cast<std::size_t>(seed)];
			if (sf.assigned) {
				continue;
			}
			const CdtTriangle&	st		= cdt.triangles[static_cast<std::size_t>(sf.sampleTri)];
			const Vec2i64		sample3 = cdt.vertices[st.v[0]] + cdt.vertices[st.v[1]] + cdt.vertices[st.v[2]];
			std::vector<RingId> start;
			if (sf.kind == FaceKind::Hole) {
				for (RingId r : m.cover(sf.oldTri)) {
					if (testRings.count(r) == 0 && !removed(r)) {
						start.push_back(r);
					}
				}
			}
			for (RingId r : testRings) {
				if (!removed(r) && ringContainsSample(newSample(r), sample3)) {
					start.push_back(r);
				}
			}
			std::sort(start.begin(), start.end());
			flood(seed, std::move(start));
		}
		assert(!conflict && "PatchableMesh: ring parity disagrees around a face");
	}

	// The boundary held only if every hole face on it agrees with the kept triangle
	// across it. False when the hole grew.
	bool PatchableMesh::Patch::coversHeld() {
		const std::int32_t cdtCount = static_cast<std::int32_t>(cdt.triangles.size());
		bool grew = false;
		for (std::int32_t t = 0; t < cdtCount; ++t) {
			const CdtTriangle& tri = cdt.triangles[static_cast<std::size_t>(t)];
			for (int e = 0; e < 3; ++e) {
				const std::int32_t c = tri.constraint[static_cast<std::size_t>(e)];
				if (c < 0 || boundaryFixedOf[static_cast<std::size_t>(c)] < 0) {
					continue;
				}
				const LocalFace& face = faces[static_cast<std::size_t>(faceOf[static_cast<std::size_t>(t)])];
				if (face.kind == FaceKind::Discard || face.kind == FaceKind::Frame) {
					continue;
				}
				const Fixed&			  f		   = fixed[static_cast<std::size_t>(boundaryFixedOf[static_cast<std::size_t>(c)])];
				const std::vector<RingId> expected = toggled(m.cover(f.outside), f.sources);
				if (!face.assigned || face.cover != expected) {
					hole.insert(f.outside);
					grew = true;
				}
			}
		}
		return !grew;
	}

	// The ring edges an edge of the hole arrangement lies on, ascending.
	std::vector<EdgeSource> PatchableMesh::Patch::sourcesOf(std::int32_t constraint) const {
		std::vector<EdgeSource> list;
		for (std::int64_t local : arrangement.edges[static_cast<std::size_t>(constraint)].provenance) {
			const LocalSegment& seg = segments[static_cast<std::size_t>(local)];
			if (seg.fixed < 0) {
				list.push_back(seg.source);
			} else {
				const std::vector<EdgeSource>& more = fixed[static_cast<std::size_t>(seg.fixed)].sources;
				list.insert(list.end(), more.begin(), more.end());
			}
		}
		std::sort(list.begin(), list.end());
		return list;
	}

	PatchReport PatchableMesh::Patch::run() {
		walkNewOutlines();
		closeS();
		while (!deriveOnce()) {
		}
		splice();
		return std::move(out);
	}

	void PatchableMesh::Patch::splice() {
		auto keptFace = [&](const LocalFace& face) {
			if (!face.assigned || face.kind == FaceKind::Frame || face.kind == FaceKind::Discard) {
				return false;
			}
			for (RingId r : face.cover) {
				if (!newPolygon(r).blocked) {
					return true;
				}
			}
			return false;
		};
		const std::int32_t		  cdtCount = static_cast<std::int32_t>(cdt.triangles.size());
		std::vector<std::uint8_t> kept(static_cast<std::size_t>(cdtCount), 0);
		std::vector<std::uint8_t> faceKept(faces.size(), 0);
		for (std::size_t f = 0; f < faces.size(); ++f) {
			faceKept[f] = keptFace(faces[f]) ? 1 : 0;
		}
		for (std::int32_t t = 0; t < cdtCount; ++t) {
			kept[static_cast<std::size_t>(t)] = faceKept[static_cast<std::size_t>(faceOf[static_cast<std::size_t>(t)])];
		}

		// Commit the registry; covers acquired below read the new tags.
		for (auto& [id, c] : changes) {
			RegisteredRing& reg = m.rings[static_cast<std::size_t>(id)];
			if (!c.hasNew) {
				reg = {};
				m.freeRings.push_back(id);
				continue;
			}
			reg.polygon = std::move(c.polygon);
			reg.sample	= std::move(c.sample);
			reg.lo		= c.lo;
			reg.hi		= c.hi;
			reg.live	= true;
		}

		// Vertices: an old one at the same point keeps its id.
		std::vector<std::uint32_t> meshVertex(cdt.vertices.size(), UINT32_MAX);
		for (std::int32_t t = 0; t < cdtCount; ++t) {
			if (kept[static_cast<std::size_t>(t)] == 0) {
				continue;
			}
			for (std::uint32_t lv : cdt.triangles[static_cast<std::size_t>(t)].v) {
				if (meshVertex[lv] != UINT32_MAX) {
					continue;
				}
				const Vec2i64& p  = cdt.vertices[lv];
				auto		   it = m.vertexAt.find(p);
				if (it != m.vertexAt.end()) {
					meshVertex[lv] = it->second;
					continue;
				}
				std::uint32_t id = 0;
				if (!m.freeVertices.empty()) {
					id = m.freeVertices.back();
					m.freeVertices.pop_back();
					m.verts[id] = p;
				} else {
					id = static_cast<std::uint32_t>(m.verts.size());
					m.verts.push_back(p);
					m.vertexUses.push_back(0);
				}
				m.vertexAt.emplace(p, id);
				meshVertex[lv] = id;
			}
		}

		bool	boxSet = false;
		Vec2i64 lo;
		Vec2i64 hi;
		auto	reportBox = [&](std::int32_t t) {
			   Vec2i64 tLo;
			   Vec2i64 tHi;
			   m.triangleBox(t, tLo, tHi);
			   if (!boxSet) {
				   lo	  = tLo;
				   hi	  = tHi;
				   boxSet = true;
			   }
			   growBox(lo, hi, tLo);
			   growBox(lo, hi, tHi);
		};

		// Free the hole.
		std::vector<std::uint32_t> oldVertices;
		std::vector<std::int32_t>  reuse(hole.begin(), hole.end());
		for (std::int32_t t : reuse) {
			reportBox(t);
			Vec2i64 tLo;
			Vec2i64 tHi;
			m.triangleBox(t, tLo, tHi);
			m.grid.erase(t, tLo, tHi);
			for (int e = 0; e < 3; ++e) {
				m.clearEdgeSources(t, e);
			}
			m.covers.release(m.coverIds[static_cast<std::size_t>(t)]);
			m.coverIds[static_cast<std::size_t>(t)] = -1;
			for (std::uint32_t v : m.tris[static_cast<std::size_t>(t)].v) {
				--m.vertexUses[v];
				oldVertices.push_back(v);
			}
			m.live[static_cast<std::size_t>(t)] = 0;
			--m.liveTriangles;
		}
		out.freedTriangles = reuse;

		// New triangles take the hole's ids first.
		std::vector<std::int32_t> newId(static_cast<std::size_t>(cdtCount), -1);
		std::size_t				  nextReuse = 0;
		for (std::int32_t t = 0; t < cdtCount; ++t) {
			if (kept[static_cast<std::size_t>(t)] == 0) {
				continue;
			}
			std::int32_t id = 0;
			if (nextReuse < reuse.size()) {
				id = reuse[nextReuse++];
			} else if (!m.freeTriangles.empty()) {
				id = m.freeTriangles.back();
				m.freeTriangles.pop_back();
			} else {
				id = static_cast<std::int32_t>(m.tris.size());
				m.tris.emplace_back();
				m.live.push_back(0);
				m.edgeSourceIds.push_back({-1, -1, -1});
				m.coverIds.push_back(-1);
			}
			newId[static_cast<std::size_t>(t)] = id;
			const CdtTriangle& ct			   = cdt.triangles[static_cast<std::size_t>(t)];
			NavTriangle		   nt;
			nt.v			  = {meshVertex[ct.v[0]], meshVertex[ct.v[1]], meshVertex[ct.v[2]]};
			nt.neighbor		  = {-1, -1, -1};
			nt.edgeProvenance = {kNoProvenance, kNoProvenance, kNoProvenance};
			nt.edgeOpening	  = {kNoOpening, kNoOpening, kNoOpening};
			const LocalFace& face = faces[static_cast<std::size_t>(faceOf[static_cast<std::size_t>(t)])];
			const std::int32_t coverId = m.acquireCover(face.cover);
			nt.faceBlocker			   = m.coverTags[static_cast<std::size_t>(coverId)].blocker;
			nt.faceOpening			   = m.coverTags[static_cast<std::size_t>(coverId)].opening;
			m.tris[static_cast<std::size_t>(id)]		  = nt;
			m.coverIds[static_cast<std::size_t>(id)]	  = coverId;
			m.edgeSourceIds[static_cast<std::size_t>(id)] = {-1, -1, -1};
			m.live[static_cast<std::size_t>(id)]		  = 1;
			++m.liveTriangles;
			for (std::uint32_t v : nt.v) {
				++m.vertexUses[v];
			}
			out.newTriangles.push_back(id);
		}
		for (std::size_t r = nextReuse; r < reuse.size(); ++r) {
			m.freeTriangles.push_back(reuse[r]);
		}

		// Adjacency and ring sources.
		std::vector<std::uint8_t> linked(fixed.size(), 0);
		for (std::int32_t t = 0; t < cdtCount; ++t) {
			const std::int32_t id = newId[static_cast<std::size_t>(t)];
			if (id < 0) {
				continue;
			}
			const CdtTriangle& ct = cdt.triangles[static_cast<std::size_t>(t)];
			NavTriangle&	   nt = m.tris[static_cast<std::size_t>(id)];
			for (int e = 0; e < 3; ++e) {
				const std::int32_t c  = ct.constraint[static_cast<std::size_t>(e)];
				const std::int32_t nb = ct.neighbor[static_cast<std::size_t>(e)];
				if (nb >= 0 && kept[static_cast<std::size_t>(nb)] != 0) {
					nt.neighbor[static_cast<std::size_t>(e)] = newId[static_cast<std::size_t>(nb)];
				} else if (c >= 0 && boundaryFixedOf[static_cast<std::size_t>(c)] >= 0) {
					const std::int32_t f						   = boundaryFixedOf[static_cast<std::size_t>(c)];
					const Fixed&	   fx						   = fixed[static_cast<std::size_t>(f)];
					nt.neighbor[static_cast<std::size_t>(e)]	   = fx.outside;
					m.tris[static_cast<std::size_t>(fx.outside)].neighbor[static_cast<std::size_t>(fx.outsideEdge)] = id;
					linked[static_cast<std::size_t>(f)]			   = 1;
				} else {
					assert(c >= 0 && "PatchableMesh: kept ground ends at an unconstrained edge");
				}
				if (c < 0) {
					continue;
				}
				const std::vector<EdgeSource> list = sourcesOf(c);
				if (list.empty()) {
					continue;
				}
				std::int64_t provenance = m.rings[static_cast<std::size_t>(list.front().ring)].polygon.provenanceId;
				for (const EdgeSource& z : list) {
					provenance = std::min(provenance, m.rings[static_cast<std::size_t>(z.ring)].polygon.provenanceId);
				}
				nt.edgeProvenance[static_cast<std::size_t>(e)] = provenance;
				m.setEdgeSources(id, e, m.sourceLists.acquire(list));
			}
		}
		for (std::size_t f = 0; f < fixed.size(); ++f) {
			if (fixed[f].boundary && linked[f] == 0) {
				m.tris[static_cast<std::size_t>(fixed[f].outside)].neighbor[static_cast<std::size_t>(fixed[f].outsideEdge)] = -1;
			}
		}

		std::sort(oldVertices.begin(), oldVertices.end());
		oldVertices.erase(std::unique(oldVertices.begin(), oldVertices.end()), oldVertices.end());
		for (std::uint32_t v : oldVertices) {
			if (m.vertexUses[v] == 0) {
				m.vertexAt.erase(m.verts[v]);
				m.freeVertices.push_back(v);
			}
		}

		std::vector<std::pair<std::int32_t, int>> stack;
		for (std::int32_t id : out.newTriangles) {
			Vec2i64 tLo;
			Vec2i64 tHi;
			m.triangleBox(id, tLo, tHi);
			m.grid.insert(id, tLo, tHi);
			reportBox(id);
			for (int e = 0; e < 3; ++e) {
				stack.push_back({id, e});
			}
		}
		std::vector<std::int32_t> flipped;
		m.legalize(stack, flipped);
		for (std::int32_t t : flipped) {
			reportBox(t);
		}
		out.freedTriangles.insert(out.freedTriangles.end(), flipped.begin(), flipped.end());
		out.newTriangles.insert(out.newTriangles.end(), flipped.begin(), flipped.end());
		for (std::vector<std::int32_t>* list : {&out.freedTriangles, &out.newTriangles}) {
			std::sort(list->begin(), list->end());
			list->erase(std::unique(list->begin(), list->end()), list->end());
		}
		out.lo = lo;
		out.hi = hi;
	}

	// --- Public edits ---------------------------------------------------------------

	PatchReport PatchableMesh::applyPatch(const PatchEdits& edits) {
		Patch patch(*this, edits);
		report = patch.run();
		return report;
	}

	RingId PatchableMesh::addRing(const NavInputPolygon& ring) {
		PatchEdits edits;
		edits.adds.push_back(ring);
		return applyPatch(edits).added.front();
	}

	void PatchableMesh::removeRing(RingId ring) {
		PatchEdits edits;
		edits.removes.push_back(ring);
		applyPatch(edits);
	}

	void PatchableMesh::moveRing(RingId ring, const std::vector<Vec2i64>& outline) {
		PatchEdits edits;
		edits.moves.push_back({ring, outline});
		applyPatch(edits);
	}

	// --- Validator -------------------------------------------------------------------

	std::string PatchableMesh::validate() const {
		std::ostringstream err;
		auto			   fail = [&]() -> std::ostringstream& { return err; };
		auto			   failed = [&]() { return !err.str().empty(); };

		const std::int32_t triCount = static_cast<std::int32_t>(tris.size());

		// Free lists and counts.
		{
			std::vector<std::uint8_t> seen(tris.size(), 0);
			for (std::int32_t t : freeTriangles) {
				if (t < 0 || t >= triCount || live[static_cast<std::size_t>(t)] != 0 || seen[static_cast<std::size_t>(t)] != 0) {
					fail() << "free triangle list holds " << t;
					return err.str();
				}
				seen[static_cast<std::size_t>(t)] = 1;
			}
			std::int32_t liveCount = 0;
			for (std::int32_t t = 0; t < triCount; ++t) {
				liveCount += live[static_cast<std::size_t>(t)] != 0 ? 1 : 0;
			}
			if (liveCount != liveTriangles || liveCount + static_cast<std::int32_t>(freeTriangles.size()) != triCount) {
				fail() << "live triangle count " << liveCount << " vs " << liveTriangles << ", free " << freeTriangles.size();
				return err.str();
			}
			std::vector<std::int32_t> uses(verts.size(), 0);
			for (std::int32_t t = 0; t < triCount; ++t) {
				if (live[static_cast<std::size_t>(t)] == 0) {
					continue;
				}
				for (std::uint32_t v : tris[static_cast<std::size_t>(t)].v) {
					if (v >= verts.size()) {
						fail() << "triangle " << t << " names vertex " << v;
						return err.str();
					}
					++uses[v];
				}
			}
			std::size_t liveVertices = 0;
			for (std::size_t v = 0; v < verts.size(); ++v) {
				if (uses[v] != vertexUses[v]) {
					fail() << "vertex " << v << " uses " << vertexUses[v] << ", counted " << uses[v];
					return err.str();
				}
				if (uses[v] > 0) {
					++liveVertices;
					auto it = vertexAt.find(verts[v]);
					if (it == vertexAt.end() || it->second != v) {
						fail() << "vertex " << v << " missing from the position lookup";
						return err.str();
					}
				}
			}
			for (std::uint32_t v : freeVertices) {
				if (uses[v] != 0) {
					fail() << "free vertex " << v << " is used";
					return err.str();
				}
			}
			if (vertexAt.size() != liveVertices || liveVertices + freeVertices.size() != verts.size()) {
				fail() << "vertex lookup " << vertexAt.size() << " vs live " << liveVertices << ", free " << freeVertices.size();
				return err.str();
			}
		}

		// Per triangle: shape, links, ring edges, Delaunay, cover, tags, locate.
		std::vector<std::int32_t> coverRefs(static_cast<std::size_t>(covers.slots()), 0);
		std::vector<std::int32_t> sourceRefs(static_cast<std::size_t>(sourceLists.slots()), 0);
		std::size_t				  halfEdgeCount = 0;
		for (std::int32_t t = 0; t < triCount && !failed(); ++t) {
			if (live[static_cast<std::size_t>(t)] == 0) {
				continue;
			}
			const NavTriangle& tri = tris[static_cast<std::size_t>(t)];
			const Vec2i64&	   a   = verts[tri.v[0]];
			const Vec2i64&	   b   = verts[tri.v[1]];
			const Vec2i64&	   c   = verts[tri.v[2]];
			if (orientation(a, b, c) != Orientation::CounterClockwise) {
				fail() << "triangle " << t << " is not counterclockwise";
				break;
			}
			const std::int32_t coverId = coverIds[static_cast<std::size_t>(t)];
			if (coverId < 0 || coverId >= covers.slots() || covers.refCount(coverId) <= 0) {
				fail() << "triangle " << t << " has a dead cover";
				break;
			}
			++coverRefs[static_cast<std::size_t>(coverId)];
			const std::vector<RingId>&	   ringSet = covers.get(coverId);
			std::vector<const SampleRing*> containing;
			int							   boundsCount = 0;
			for (RingId r : ringSet) {
				if (r < 0 || r >= ringSlots() || !rings[static_cast<std::size_t>(r)].live) {
					fail() << "triangle " << t << " covered by dead ring " << r;
					break;
				}
				if (rings[static_cast<std::size_t>(r)].polygon.blocked) {
					containing.push_back(&rings[static_cast<std::size_t>(r)].sample);
				} else {
					++boundsCount;
				}
			}
			if (failed()) {
				break;
			}
			if (boundsCount != 1) {
				fail() << "triangle " << t << " lies in " << boundsCount << " bounds rings";
				break;
			}
			const FaceTags tags = classifyCover(containing);
			if (tags.blocker != tri.faceBlocker || tags.opening != tri.faceOpening) {
				fail() << "triangle " << t << " tags disagree with its cover";
				break;
			}
			const Vec2i64 sample3 = a + b + c;
			if (locateScaled(sample3, 3) != t) {
				fail() << "locate misses triangle " << t;
				break;
			}

			for (int e = 0; e < 3 && !failed(); ++e) {
				const std::int32_t u		= tri.neighbor[static_cast<std::size_t>(e)];
				const std::int32_t sourceId = edgeSourceIds[static_cast<std::size_t>(t)][static_cast<std::size_t>(e)];
				const Vec2i64&	   p		= verts[tri.v[static_cast<std::size_t>(e)]];
				const Vec2i64&	   q		= verts[tri.v[static_cast<std::size_t>(next3(e))]];
				std::int64_t	   provenance = kNoProvenance;
				if (sourceId >= 0) {
					if (sourceId >= sourceLists.slots() || sourceLists.refCount(sourceId) <= 0 || sourceLists.get(sourceId).empty()) {
						fail() << "triangle " << t << " edge " << e << " has a dead source list";
						break;
					}
					++sourceRefs[static_cast<std::size_t>(sourceId)];
					for (const EdgeSource& z : sourceLists.get(sourceId)) {
						if (z.ring < 0 || z.ring >= ringSlots() || !rings[static_cast<std::size_t>(z.ring)].live) {
							fail() << "triangle " << t << " edge " << e << " names dead ring " << z.ring;
							break;
						}
						const std::vector<Vec2i64>& outline = rings[static_cast<std::size_t>(z.ring)].polygon.ring;
						if (z.edge < 0 || z.edge >= static_cast<std::int32_t>(outline.size())) {
							fail() << "triangle " << t << " edge " << e << " names edge " << z.edge << " of ring " << z.ring;
							break;
						}
						Vec2i64 lo = edgeStart(outline, z.edge);
						Vec2i64 hi = lo;
						growBox(lo, hi, edgeEnd(outline, z.edge));
						if (!boxesOverlap(p, p, lo, hi) || !boxesOverlap(q, q, lo, hi)) {
							fail() << "triangle " << t << " edge " << e << " leaves ring " << z.ring << " edge " << z.edge;
							break;
						}
						provenance = std::min(provenance == kNoProvenance ? INT64_MAX : provenance,
											  rings[static_cast<std::size_t>(z.ring)].polygon.provenanceId);
						auto it = halfEdgesOf.find(sourceKey(z));
						if (it == halfEdgesOf.end() || std::find(it->second.begin(), it->second.end(), t * 3 + e) == it->second.end()) {
							fail() << "triangle " << t << " edge " << e << " missing from the ring edge index";
							break;
						}
						++halfEdgeCount;
					}
					if (failed()) {
						break;
					}
				}
				if (tri.edgeProvenance[static_cast<std::size_t>(e)] != provenance) {
					fail() << "triangle " << t << " edge " << e << " provenance disagrees with its sources";
					break;
				}
				if (u < 0) {
					if (sourceId < 0) {
						fail() << "triangle " << t << " edge " << e << " is an unconstrained mesh boundary";
					}
					continue;
				}
				if (u >= triCount || live[static_cast<std::size_t>(u)] == 0) {
					fail() << "triangle " << t << " links dead triangle " << u;
					break;
				}
				const NavTriangle& ut	= tris[static_cast<std::size_t>(u)];
				int				   back = -1;
				for (int k = 0; k < 3; ++k) {
					if (ut.neighbor[static_cast<std::size_t>(k)] == t && ut.v[static_cast<std::size_t>(k)] == tri.v[static_cast<std::size_t>(next3(e))] &&
						ut.v[static_cast<std::size_t>(next3(k))] == tri.v[static_cast<std::size_t>(e)]) {
						back = k;
					}
				}
				if (back < 0) {
					fail() << "triangles " << t << " and " << u << " disagree on their shared edge";
					break;
				}
				if (edgeSourceIds[static_cast<std::size_t>(u)][static_cast<std::size_t>(back)] != sourceId) {
					fail() << "triangles " << t << " and " << u << " disagree on the sources of their shared edge";
					break;
				}
				const std::vector<RingId>& other = covers.get(coverIds[static_cast<std::size_t>(u)]);
				if (sourceId < 0) {
					if (coverIds[static_cast<std::size_t>(u)] != coverId) {
						fail() << "triangles " << t << " and " << u << " differ in cover across an unconstrained edge";
						break;
					}
					const Vec2i64& apex = verts[ut.v[static_cast<std::size_t>(prev3(back))]];
					if (inCircle(a, b, c, apex) == InCircle::Inside) {
						fail() << "edge " << e << " of triangle " << t << " is not locally Delaunay";
						break;
					}
				} else if (toggled(ringSet, sourceLists.get(sourceId)) != other) {
					fail() << "covers of " << t << " and " << u << " do not differ by the rings on their edge (" << p.x << ", " << p.y << ")-(" << q.x
						   << ", " << q.y << ")";
					break;
				}
			}
		}
		if (failed()) {
			return err.str();
		}
		for (std::int32_t id = 0; id < covers.slots(); ++id) {
			if (covers.refCount(id) != coverRefs[static_cast<std::size_t>(id)]) {
				fail() << "cover " << id << " refs " << covers.refCount(id) << ", counted " << coverRefs[static_cast<std::size_t>(id)];
				return err.str();
			}
		}
		for (std::int32_t id = 0; id < sourceLists.slots(); ++id) {
			if (sourceLists.refCount(id) != sourceRefs[static_cast<std::size_t>(id)]) {
				fail() << "source list " << id << " refs " << sourceLists.refCount(id) << ", counted " << sourceRefs[static_cast<std::size_t>(id)];
				return err.str();
			}
		}
		std::size_t indexed = 0;
		for (const auto& [key, list] : halfEdgesOf) {
			indexed += list.size();
		}
		if (indexed != halfEdgeCount) {
			fail() << "ring edge index holds " << indexed << " half-edges, mesh has " << halfEdgeCount;
			return err.str();
		}
		bool deadInGrid = false;
		grid.forEachEntry([&](std::int32_t t) { deadInGrid = deadInGrid || t < 0 || t >= triCount || live[static_cast<std::size_t>(t)] == 0; });
		if (deadInGrid) {
			return "locate index holds a dead triangle";
		}

		// Each ring edge is a chain of pieces: no fork, and an end that is not an original
		// corner is on the mesh boundary.
		std::vector<std::uint8_t> onBoundary(verts.size(), 0);
		for (std::int32_t t = 0; t < triCount; ++t) {
			if (live[static_cast<std::size_t>(t)] == 0) {
				continue;
			}
			const NavTriangle& tri = tris[static_cast<std::size_t>(t)];
			for (int e = 0; e < 3; ++e) {
				if (tri.neighbor[static_cast<std::size_t>(e)] < 0) {
					onBoundary[tri.v[static_cast<std::size_t>(e)]]				   = 1;
					onBoundary[tri.v[static_cast<std::size_t>(next3(e))]] = 1;
				}
			}
		}
		for (const auto& [key, list] : halfEdgesOf) {
			const EdgeSource z{static_cast<RingId>(key >> 32), static_cast<std::int32_t>(key & 0xFFFFFFFFULL)};
			std::map<std::uint32_t, std::set<std::uint32_t>> adjacent;
			for (std::int32_t h : list) {
				const NavTriangle& tri = tris[static_cast<std::size_t>(h / 3)];
				const std::uint32_t p = tri.v[static_cast<std::size_t>(h % 3)];
				const std::uint32_t q = tri.v[static_cast<std::size_t>(next3(h % 3))];
				adjacent[p].insert(q);
				adjacent[q].insert(p);
			}
			const std::vector<Vec2i64>& outline = rings[static_cast<std::size_t>(z.ring)].polygon.ring;
			for (const auto& [v, nbs] : adjacent) {
				if (nbs.size() > 2) {
					fail() << "ring " << z.ring << " edge " << z.edge << " forks at vertex " << v;
					return err.str();
				}
				if (nbs.size() == 1 && !isOriginalEndpoint(outline, z.edge, verts[v]) && onBoundary[v] == 0) {
					fail() << "ring " << z.ring << " edge " << z.edge << " stops short at vertex " << v;
					return err.str();
				}
			}
		}
		// An original corner over kept ground is a vertex with both its edges on it.
		for (RingId r = 0; r < ringSlots(); ++r) {
			const RegisteredRing& reg = rings[static_cast<std::size_t>(r)];
			if (!reg.live) {
				continue;
			}
			const std::vector<Vec2i64>& outline = reg.polygon.ring;
			const std::int32_t			n		= static_cast<std::int32_t>(outline.size());
			for (std::int32_t i = 0; i < n; ++i) {
				const Vec2i64& p = outline[static_cast<std::size_t>(i)];
				if (locate(p) < 0) {
					continue;
				}
				auto it = vertexAt.find(p);
				if (it == vertexAt.end()) {
					fail() << "corner " << i << " of ring " << r << " is over ground but not a vertex";
					return err.str();
				}
				if (onBoundary[it->second] != 0) {
					continue;
				}
				for (const std::int32_t edge : {i, (i + n - 1) % n}) {
					if (edgeStart(outline, edge) == edgeEnd(outline, edge)) {
						continue;
					}
					bool present = false;
					auto pieces	 = halfEdgesOf.find(sourceKey({r, edge}));
					if (pieces != halfEdgesOf.end()) {
						for (std::int32_t h : pieces->second) {
							const NavTriangle& tri = tris[static_cast<std::size_t>(h / 3)];
							present = present || tri.v[static_cast<std::size_t>(h % 3)] == it->second ||
									  tri.v[static_cast<std::size_t>(next3(h % 3))] == it->second;
						}
					}
					if (!present) {
						fail() << "ring " << r << " edge " << edge << " is missing at its corner " << i;
						return err.str();
					}
				}
			}
		}

		// Each face's cover is the exact containment of its largest triangle's centroid,
		// the sample buildNavMesh classifies a face by.
		std::vector<std::int32_t> faceOf(tris.size(), -1);
		std::vector<std::int32_t> fill;
		for (std::int32_t seed = 0; seed < triCount; ++seed) {
			if (live[static_cast<std::size_t>(seed)] == 0 || faceOf[static_cast<std::size_t>(seed)] >= 0) {
				continue;
			}
			std::int32_t best = seed;
			Int128		 bestArea(0);
			bool		 first = true;
			faceOf[static_cast<std::size_t>(seed)] = seed;
			fill.push_back(seed);
			while (!fill.empty()) {
				const std::int32_t t = fill.back();
				fill.pop_back();
				const NavTriangle& tri	 = tris[static_cast<std::size_t>(t)];
				const Int128	   area2 = cross(verts[tri.v[1]] - verts[tri.v[0]], verts[tri.v[2]] - verts[tri.v[0]]);
				if (first || bestArea < area2 || (area2 == bestArea && t < best)) {
					best	 = t;
					bestArea = area2;
					first	 = false;
				}
				for (int e = 0; e < 3; ++e) {
					const std::int32_t u = tri.neighbor[static_cast<std::size_t>(e)];
					if (u >= 0 && edgeSourceIds[static_cast<std::size_t>(t)][static_cast<std::size_t>(e)] < 0 &&
						faceOf[static_cast<std::size_t>(u)] < 0) {
						faceOf[static_cast<std::size_t>(u)] = seed;
						fill.push_back(u);
					}
				}
			}
			const NavTriangle&	tri		= tris[static_cast<std::size_t>(best)];
			const Vec2i64		sample3 = verts[tri.v[0]] + verts[tri.v[1]] + verts[tri.v[2]];
			std::vector<RingId> exact;
			for (RingId r = 0; r < ringSlots(); ++r) {
				const RegisteredRing& reg = rings[static_cast<std::size_t>(r)];
				if (reg.live && ringContainsSample(reg.sample, sample3)) {
					exact.push_back(r);
				}
			}
			std::vector<RingId> differ;
			std::set_symmetric_difference(exact.begin(), exact.end(), cover(best).begin(), cover(best).end(), std::back_inserter(differ));
			for (RingId r : differ) {
				// Snap rounding bends a ring's edges off its original lines, so a face can
				// straddle one; within that reach the original outline does not decide.
				const std::vector<Vec2i64>& ring3 = rings[static_cast<std::size_t>(r)].sample.ring3;
				bool						near  = false;
				for (std::size_t i = 0; i < ring3.size() && !near; ++i) {
					near = withinDistanceOfSegment(sample3, ring3[i], ring3[(i + 1) % ring3.size()], 3 * kSnapReachMm);
				}
				if (!near) {
					fail() << "face of triangle " << best << ": cover disagrees with exact containment of its sample in ring " << r;
					return err.str();
				}
			}
		}
		return err.str();
	}

} // namespace geometry::nav

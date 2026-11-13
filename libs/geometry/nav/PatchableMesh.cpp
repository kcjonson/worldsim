#include "PatchableMesh.h"

#include "../arrangement/Arrangement.h"
#include "../predicates/Predicates.h"
#include "../triangulation/ConstrainedDelaunay.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <tuple>
#include <utility>

namespace geometry::nav {

	namespace {

		// How close two original edges must come for the off-ground pull to treat them as
		// interacting. buildArrangement's rounding has no proven drift bound: each cascade
		// level can move a piece another 0.71 mm, and the six segments in the arrangement
		// test RepeatedRoundingDriftsPastTwoMillimetres drift 2.98 mm. Pulling too much
		// into s is only slower; pulling too little loses exactness.
		constexpr std::int64_t kOffGroundReachMm = 8;

		// How many times as far as a ring's pieces stray from its outline a seed sample
		// must lie from it, so the exact test and the bent pieces agree on its side.
		constexpr std::int64_t kSeedClearance = 3;

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

		// Are all three corners strictly on one side of the line through a and b? Then
		// the closed triangle misses the line, and the segment with it.
		bool triangleOffLine(const Vec2i64& a, const Vec2i64& b, const std::array<Vec2i64, 3>& t) {
			const Orientation o = orientation(a, b, t[0]);
			return o != Orientation::Collinear && orientation(a, b, t[1]) == o && orientation(a, b, t[2]) == o;
		}

		// Does closed segment ab meet the closed triangle?
		bool segmentTouchesTriangle(const Vec2i64& a, const Vec2i64& b, const std::array<Vec2i64, 3>& t) {
			if (triangleOffLine(a, b, t)) {
				return false;
			}
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
			if (triangleOffLine(a, b, t)) {
				return false;
			}
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

		// Does closed segment ab meet the closed area of the ring?
		bool segmentMeetsArea(const Vec2i64& a, const Vec2i64& b, const std::vector<Vec2i64>& ring) {
			if (pointInPolygon(a, ring) != PointInPolygon::Outside || pointInPolygon(b, ring) != PointInPolygon::Outside) {
				return true;
			}
			for (std::size_t i = 0; i < ring.size(); ++i) {
				if (intersectSegments(a, b, ring[i], ring[(i + 1) % ring.size()]).relation != SegmentRelation::Disjoint) {
					return true;
				}
			}
			return false;
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

		// How far v lies from the line through a along dir (len2 = |dir|^2 > 0), rounded up
		// to a millimetre: the least w with w^2 len2 >= cross(dir, v - a)^2. A square root
		// in doubles guesses it; exact comparisons settle it.
		std::int64_t offLineMm(const Vec2i64& a, const Vec2i64& dir, const Int128& len2, const Vec2i64& v) {
			assert(len2.sign() > 0 && "PatchableMesh: distance from the line of a zero-length edge");
			const Int128 off   = cross(dir, v - a);
			const double guess = std::ceil(std::fabs(off.toDouble()) / std::sqrt(len2.toDouble()));
			std::int64_t w	   = guess < 0x1p62 ? static_cast<std::int64_t>(guess) : std::int64_t{1} << 62;
			while (w > 0 && Int128::compareSquareToProduct(off, Int128::product(w - 1, w - 1), len2) <= 0) {
				--w;
			}
			while (Int128::compareSquareToProduct(off, Int128::product(w, w), len2) > 0) {
				++w;
			}
			return w;
		}

		// Whether the ray from p3 along +x crosses the segment (a3, b3), all in 3x space:
		// one end above p3.y and the other not, the crossing strictly right of p3. A
		// segment through p3 itself returns nullopt.
		std::optional<bool> rayCrosses(const Vec2i64& p3, const Vec2i64& a3, const Vec2i64& b3) {
			if ((a3.y > p3.y) == (b3.y > p3.y)) {
				if (a3.y == p3.y && b3.y == p3.y && std::min(a3.x, b3.x) <= p3.x && p3.x <= std::max(a3.x, b3.x)) {
					return std::nullopt;
				}
				return false;
			}
			const int side = cross(b3 - a3, p3 - a3).sign();
			if (side == 0) {
				return std::nullopt;
			}
			return b3.y > a3.y ? side > 0 : side < 0;
		}

		// out = cover xor the cover entries the sources name an odd number of times, every
		// bounds ring counting as kGroundToken; ascending. isBounds(ring) says whether a
		// ring is a bounds ring; flips is scratch.
		template <typename IsBounds>
		void toggledInto(const std::vector<RingId>& cover, std::span<const EdgeSource> sources, IsBounds isBounds, std::vector<RingId>& flips,
						 std::vector<RingId>& out) {
			flips.clear();
			for (const EdgeSource& s : sources) {
				flips.push_back(isBounds(s.ring) ? kGroundToken : s.ring);
			}
			std::sort(flips.begin(), flips.end());
			std::size_t odd = 0;
			for (std::size_t i = 0; i < flips.size();) {
				std::size_t j = i;
				while (j < flips.size() && flips[j] == flips[i]) {
					++j;
				}
				if ((j - i) % 2 == 1) {
					flips[odd++] = flips[i];
				}
				i = j;
			}
			out.clear();
			std::set_symmetric_difference(cover.begin(), cover.end(), flips.begin(), flips.begin() + static_cast<std::ptrdiff_t>(odd), std::back_inserter(out));
		}

	} // namespace

	// --- InternTable -------------------------------------------------------------

	namespace {

		std::uint64_t mixHash(std::uint64_t h, std::uint64_t v) {
			h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
			return h;
		}

		std::uint64_t hashValue(const std::vector<EdgeSource>& list) {
			std::uint64_t h = list.size();
			for (const EdgeSource& s : list) {
				h = mixHash(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(s.ring)) << 32 | static_cast<std::uint32_t>(s.edge));
			}
			return h;
		}

		std::uint64_t hashValue(const std::vector<RingId>& list) {
			std::uint64_t h = list.size();
			for (const RingId r : list) {
				h = mixHash(h, static_cast<std::uint32_t>(r));
			}
			return h;
		}

	} // namespace

	template <typename Value> std::int32_t PatchableMesh::InternTable<Value>::acquire(const Value& value) {
		const std::uint64_t hash  = hashValue(value);
		std::int32_t*		first = byHash.find(hash);
		for (std::int32_t at = first != nullptr ? *first : -1; at >= 0; at = sameHash[static_cast<std::size_t>(at)]) {
			if (values[static_cast<std::size_t>(at)] == value) {
				++refs[static_cast<std::size_t>(at)];
				return at;
			}
		}
		std::int32_t id = 0;
		if (!freeIds.empty()) {
			id = freeIds.back();
			freeIds.pop_back();
		} else {
			id = static_cast<std::int32_t>(values.size());
			values.emplace_back();
			hashes.push_back(0);
			refs.push_back(0);
			sameHash.push_back(-1);
		}
		values[static_cast<std::size_t>(id)].assign(value.begin(), value.end());
		hashes[static_cast<std::size_t>(id)] = hash;
		refs[static_cast<std::size_t>(id)]	 = 1;
		if (first != nullptr) {
			sameHash[static_cast<std::size_t>(id)] = *first;
			*first								   = id;
		} else {
			sameHash[static_cast<std::size_t>(id)] = -1;
			byHash.insert(hash, id);
		}
		return id;
	}

	template <typename Value> void PatchableMesh::InternTable<Value>::release(std::int32_t id) {
		assert(refs[static_cast<std::size_t>(id)] > 0 && "InternTable: releasing a dead entry");
		if (--refs[static_cast<std::size_t>(id)] != 0) {
			return;
		}
		const std::uint64_t hash  = hashes[static_cast<std::size_t>(id)];
		std::int32_t*		first = byHash.find(hash);
		if (*first == id) {
			if (sameHash[static_cast<std::size_t>(id)] >= 0) {
				*first = sameHash[static_cast<std::size_t>(id)];
			} else {
				byHash.erase(hash);
			}
		} else {
			std::int32_t at = *first;
			while (sameHash[static_cast<std::size_t>(at)] != id) {
				at = sameHash[static_cast<std::size_t>(at)];
			}
			sameHash[static_cast<std::size_t>(at)] = sameHash[static_cast<std::size_t>(id)];
		}
		values[static_cast<std::size_t>(id)].clear();
		freeIds.push_back(id);
	}

	// --- BoxGrid -----------------------------------------------------------------

	namespace {

		std::uint64_t mixKey(std::uint64_t k) {
			k ^= k >> 33;
			k *= 0xFF51AFD7ED558CCDULL;
			k ^= k >> 33;
			k *= 0xC4CEB9FE1A85EC53ULL;
			return k ^ (k >> 33);
		}

	} // namespace

	int PatchableMesh::BoxGrid::levelFor(const Vec2i64& lo, const Vec2i64& hi) {
		const std::int64_t side	 = std::max(hi.x - lo.x, hi.y - lo.y);
		int				   level = 0;
		while (level < kLevels - 1 && (kBaseCell << level) < side) {
			++level;
		}
		return level;
	}

	std::uint64_t PatchableMesh::BoxGrid::CellHash::operator()(const CellKey& key) const {
		return mixKey(static_cast<std::uint64_t>(key.x) ^ mixKey(static_cast<std::uint64_t>(key.y) ^ (static_cast<std::uint64_t>(key.level) << 58)));
	}

	std::int32_t PatchableMesh::BoxGrid::find(const CellKey& key) const {
		const std::int32_t* cell = table.find(key);
		return cell != nullptr ? *cell : -1;
	}

	std::int32_t PatchableMesh::BoxGrid::findOrAdd(const CellKey& key) {
		if (const std::int32_t* found = table.find(key)) {
			return *found;
		}
		const int	 level = key.level;
		std::int32_t cell  = 0;
		if (!freeCells.empty()) {
			cell = freeCells.back();
			freeCells.pop_back();
		} else {
			cell = static_cast<std::int32_t>(cells.size());
			cells.emplace_back();
		}
		Cell& c		= cells[static_cast<std::size_t>(cell)];
		c.key		= key;
		c.levelSlot = static_cast<std::int32_t>(levelCells[static_cast<std::size_t>(level)].size());
		levelCells[static_cast<std::size_t>(level)].push_back(cell);
		levelMask |= std::uint64_t{1} << level;
		table.insert(key, cell);
		return cell;
	}

	void PatchableMesh::BoxGrid::dropCell(std::int32_t cell, int level) {
		table.erase(cells[static_cast<std::size_t>(cell)].key);
		PagedVector<std::int32_t>& onLevel = levelCells[static_cast<std::size_t>(level)];
		const std::int32_t		   slot	   = cells[static_cast<std::size_t>(cell)].levelSlot;
		onLevel[static_cast<std::size_t>(slot)]					  = onLevel.back();
		cells[static_cast<std::size_t>(onLevel.back())].levelSlot = slot;
		onLevel.pop_back();
		if (onLevel.empty()) {
			levelMask &= ~(std::uint64_t{1} << level);
		}
		cells[static_cast<std::size_t>(cell)].levelSlot = -1;
		freeCells.push_back(cell);
	}

	void PatchableMesh::BoxGrid::insert(std::int32_t id, const Vec2i64& lo, const Vec2i64& hi) {
		const int		   level = levelFor(lo, hi);
		const std::int64_t side	 = kBaseCell << level;
		for (std::int64_t cy = floorDiv(lo.y, side); cy <= floorDiv(hi.y, side); ++cy) {
			for (std::int64_t cx = floorDiv(lo.x, side); cx <= floorDiv(hi.x, side); ++cx) {
				const std::int32_t cell = findOrAdd({cx, cy, level});
				cells[static_cast<std::size_t>(cell)].entries.push_back({id, lo, hi});
			}
		}
	}

	void PatchableMesh::BoxGrid::erase(std::int32_t id, const Vec2i64& lo, const Vec2i64& hi) {
		const int		   level = levelFor(lo, hi);
		const std::int64_t side	 = kBaseCell << level;
		for (std::int64_t cy = floorDiv(lo.y, side); cy <= floorDiv(hi.y, side); ++cy) {
			for (std::int64_t cx = floorDiv(lo.x, side); cx <= floorDiv(hi.x, side); ++cx) {
				const std::int32_t cell = find({cx, cy, level});
				assert(cell >= 0 && "BoxGrid: erasing from a cell that does not exist");
				std::vector<Entry>& entries = cells[static_cast<std::size_t>(cell)].entries;
				auto				pos		= std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.id == id; });
				assert(pos != entries.end() && "BoxGrid: erasing an id the cell does not hold");
				*pos = entries.back();
				entries.pop_back();
				if (entries.empty()) {
					dropCell(cell, level);
				}
			}
		}
	}

	namespace {

		template <typename Entries, typename Visit>
		void visitOverlapping(const Entries& entries, const Vec2i64& lo, const Vec2i64& hi, std::int64_t scale, Visit& visit) {
			for (const auto& e : entries) {
				if (e.lo.x * scale <= hi.x && lo.x <= e.hi.x * scale && e.lo.y * scale <= hi.y && lo.y <= e.hi.y * scale) {
					visit(e.id);
				}
			}
		}

	} // namespace

	template <typename Visit>
	void PatchableMesh::BoxGrid::forEachInBox(const Vec2i64& lo, const Vec2i64& hi, std::int64_t scale, Visit visit) const {
		for (int level = 0; level < kLevels; ++level) {
			if ((levelMask >> level & 1) == 0) {
				continue;
			}
			const PagedVector<std::int32_t>& onLevel = levelCells[static_cast<std::size_t>(level)];
			const std::int64_t				 side	 = (kBaseCell << level) * scale;
			const std::int64_t				 x0		 = floorDiv(lo.x, side);
			const std::int64_t				 x1		 = floorDiv(hi.x, side);
			const std::int64_t				 y0		 = floorDiv(lo.y, side);
			const std::int64_t				 y1		 = floorDiv(hi.y, side);
			const double					 span	 = static_cast<double>(x1 - x0 + 1) * static_cast<double>(y1 - y0 + 1);
			if (span > static_cast<double>(onLevel.size())) {
				for (std::int32_t cell : onLevel) {
					const Cell& c = cells[static_cast<std::size_t>(cell)];
					if (c.key.x >= x0 && c.key.x <= x1 && c.key.y >= y0 && c.key.y <= y1) {
						visitOverlapping(c.entries, lo, hi, scale, visit);
					}
				}
				continue;
			}
			for (std::int64_t cy = y0; cy <= y1; ++cy) {
				for (std::int64_t cx = x0; cx <= x1; ++cx) {
					const std::int32_t cell = find({cx, cy, level});
					if (cell >= 0) {
						visitOverlapping(cells[static_cast<std::size_t>(cell)].entries, lo, hi, scale, visit);
					}
				}
			}
		}
	}

	template <typename Visit> void PatchableMesh::BoxGrid::forEachAtPoint(const Vec2i64& p, std::int64_t scale, Visit visit) const {
		for (int level = 0; level < kLevels; ++level) {
			if ((levelMask >> level & 1) == 0) {
				continue;
			}
			const std::int64_t side = (kBaseCell << level) * scale;
			const std::int32_t cell = find({floorDiv(p.x, side), floorDiv(p.y, side), level});
			if (cell < 0) {
				continue;
			}
			for (const Entry& e : cells[static_cast<std::size_t>(cell)].entries) {
				if (e.lo.x * scale <= p.x && p.x <= e.hi.x * scale && e.lo.y * scale <= p.y && p.y <= e.hi.y * scale && visit(e.id)) {
					return;
				}
			}
		}
	}

	template <typename Visit> void PatchableMesh::BoxGrid::forEachEntry(Visit visit) const {
		for (const PagedVector<std::int32_t>& onLevel : levelCells) {
			for (std::int32_t cell : onLevel) {
				for (const Entry& e : cells[static_cast<std::size_t>(cell)].entries) {
					visit(e.id);
				}
			}
		}
	}

	// --- PatchableMesh helpers -------------------------------------------------------

	std::size_t PatchableMesh::VertexHash::operator()(const Vec2i64& p) const {
		return static_cast<std::size_t>(mixKey(static_cast<std::uint64_t>(p.x) ^ mixKey(static_cast<std::uint64_t>(p.y))));
	}

	const std::vector<std::int32_t>& PatchableMesh::piecesOf(const EdgeSource& s) const {
		return pieces[static_cast<std::size_t>(rings[static_cast<std::size_t>(s.ring)].edgeIds[static_cast<std::size_t>(s.edge)])];
	}

	void PatchableMesh::groundRingsInBox(const Vec2i64& lo, const Vec2i64& hi, std::vector<GroundRing>& out) const {
		out.clear();
		ringGrid.forEachInBox(lo, hi, 1, [&](std::int32_t r) {
			const RegisteredRing& reg = rings[static_cast<std::size_t>(r)];
			if (reg.polygon.blocked) {
				return;
			}
			for (const GroundRing& g : out) {
				if (g.id == r) {
					return;
				}
			}
			out.push_back({&reg.polygon.ring, reg.lo, reg.hi, reg.sample.counterClockwise, r});
		});
	}

	// The union is unchanged by the outline exactly when no live ground edge's intervals
	// change and the outline has none: an edge of it outside the union would be boundary,
	// and a gap in the union under it would dissolve the edges around the gap, which meet
	// its area.
	bool PatchableMesh::groundCovers(const std::vector<Vec2i64>& outline) const {
		assert(outline.size() >= 3 && "PatchableMesh: an outline needs three vertices");
		// Past every live ring, so a stretch it shares with a live edge stays that edge's.
		const GroundRing			probe = groundRing(outline, static_cast<RingId>(rings.size()));
		std::vector<GroundRing>		near;
		GroundBoundaryScratch		scratch;
		std::vector<GroundInterval> before;
		std::vector<GroundInterval> after;
		for (std::int32_t e = 0; e < static_cast<std::int32_t>(outline.size()); ++e) {
			Vec2i64 lo = edgeStart(outline, e);
			Vec2i64 hi = lo;
			growBox(lo, hi, edgeEnd(outline, e));
			groundRingsInBox(lo, hi, near);
			near.push_back(probe);
			groundBoundaries(probe, e, near, scratch, after);
			if (!after.empty()) {
				return false;
			}
		}
		std::vector<std::int32_t> ids;
		std::vector<EdgeSource>	  edges;
		ringEdgesInBox(probe.lo, probe.hi, ids, edges);
		for (const EdgeSource& z : edges) {
			const RegisteredRing& reg = rings[static_cast<std::size_t>(z.ring)];
			const Vec2i64		  p	  = edgeStart(reg.polygon.ring, z.edge);
			const Vec2i64		  q	  = edgeEnd(reg.polygon.ring, z.edge);
			if (reg.polygon.blocked || p == q || !segmentMeetsArea(p, q, outline)) {
				continue;
			}
			Vec2i64 lo = p;
			Vec2i64 hi = p;
			growBox(lo, hi, q);
			groundRingsInBox(lo, hi, near);
			const GroundRing self{&reg.polygon.ring, reg.lo, reg.hi, reg.sample.counterClockwise, z.ring};
			groundBoundaries(self, z.edge, near, scratch, before);
			near.push_back(probe);
			groundBoundaries(self, z.edge, near, scratch, after);
			if (before != after) {
				return false;
			}
		}
		return true;
	}

	bool PatchableMesh::piecesRunBetween(const EdgeSource& s, const std::vector<Vec2i64>& ends, std::vector<std::pair<std::uint32_t, std::uint32_t>>& pieceList,
										 std::vector<std::uint32_t>& endList) const {
		pieceList.clear();
		for (std::int32_t h : piecesOf(s)) {
			const NavTriangle&	tri = tris[static_cast<std::size_t>(h / 3)];
			const std::uint32_t u	= tri.v[static_cast<std::size_t>(h % 3)];
			const std::uint32_t v	= tri.v[static_cast<std::size_t>(next3(h % 3))];
			pieceList.push_back(std::minmax(u, v));
		}
		std::sort(pieceList.begin(), pieceList.end());
		pieceList.erase(std::unique(pieceList.begin(), pieceList.end()), pieceList.end());
		endList.clear();
		for (const auto& [u, v] : pieceList) {
			endList.push_back(u);
			endList.push_back(v);
		}
		std::sort(endList.begin(), endList.end());
		std::size_t endsMet = 0;
		for (std::size_t i = 0; i < endList.size();) {
			std::size_t j = i;
			while (j < endList.size() && endList[j] == endList[i]) {
				++j;
			}
			const bool end = std::find(ends.begin(), ends.end(), verts[endList[i]]) != ends.end();
			if (((j - i) % 2 == 1) != end) {
				return false;
			}
			endsMet += end ? 1 : 0;
			i = j;
		}
		return endsMet == ends.size();
	}

	void PatchableMesh::triangleBox(std::int32_t t, Vec2i64& lo, Vec2i64& hi) const {
		const NavTriangle& tri = tris[static_cast<std::size_t>(t)];
		lo					   = verts[tri.v[0]];
		hi					   = lo;
		growBox(lo, hi, verts[tri.v[1]]);
		growBox(lo, hi, verts[tri.v[2]]);
	}

	std::int32_t PatchableMesh::locateScaled(const Vec2i64& p, std::int64_t scale) const {
		// A point strictly inside a triangle is in no other, so that one answers at once;
		// a point on an edge or vertex takes, of every triangle holding it, the one whose
		// corners sort first: geometry, not slot ids, which follow the patch history.
		std::int32_t			  best = -1;
		std::array<Vec2i64, 3> bestCorners;
		grid.forEachAtPoint(p, scale, [&](std::int32_t t) {
			const NavTriangle& tri = tris[static_cast<std::size_t>(t)];
			const Vec2i64	   a   = verts[tri.v[0]] * scale;
			const Vec2i64	   b   = verts[tri.v[1]] * scale;
			const Vec2i64	   c   = verts[tri.v[2]] * scale;
			const Orientation  ab  = orientation(a, b, p);
			const Orientation  bc  = orientation(b, c, p);
			const Orientation  ca  = orientation(c, a, p);
			if (ab == Orientation::Clockwise || bc == Orientation::Clockwise || ca == Orientation::Clockwise) {
				return false;
			}
			if (ab == Orientation::CounterClockwise && bc == Orientation::CounterClockwise && ca == Orientation::CounterClockwise) {
				best = t;
				return true;
			}
			std::array<Vec2i64, 3> corners{a, b, c};
			std::sort(corners.begin(), corners.end());
			if (best < 0 || corners < bestCorners) {
				best		= t;
				bestCorners = corners;
			}
			return false;
		});
		return best;
	}

	std::int32_t PatchableMesh::locate(const Vec2i64& p) const {
		return locateScaled(p, 1);
	}

	std::int32_t PatchableMesh::locateSample(const Vec2i64& p3) const {
		std::int32_t found = -1;
		grid.forEachAtPoint(p3, 3, [&](std::int32_t t) {
			const NavTriangle& tri = tris[static_cast<std::size_t>(t)];
			for (int i = 0; i < 3; ++i) {
				const Vec2i64	  a = verts[tri.v[static_cast<std::size_t>(i)]] * 3;
				const Vec2i64	  b = verts[tri.v[static_cast<std::size_t>(next3(i))]] * 3;
				const Orientation o = orientation(a, b, p3);
				if (o == Orientation::Clockwise || (o == Orientation::Collinear && !(b.y != a.y ? b.y < a.y : b.x > a.x))) {
					return false;
				}
			}
			found = t;
			return true;
		});
		return found;
	}

	template <typename Visit> void PatchableMesh::forEachLiveInBox(const Vec2i64& lo, const Vec2i64& hi, Visit visit) {
		// A triangle sits in up to four cells; the stamp visits it once. Callers only
		// gather into sets, so the order (the grid's) does not reach any result.
		assert(!boxQueryOpen && "PatchableMesh: nested box query");
		boxQueryOpen = true;
		visitStamp.growTo(tris.size());
		if (++stamp == 0) {
			visitStamp.zero();
			stamp = 1;
		}
		grid.forEachInBox(lo, hi, 1, [&](std::int32_t t) {
			if (visitStamp[static_cast<std::size_t>(t)] == stamp) {
				return;
			}
			visitStamp[static_cast<std::size_t>(t)] = stamp;
			visit(t);
		});
		boxQueryOpen = false;
	}

	namespace {

		int cornerOf(const NavTriangle& tri, std::uint32_t v) {
			return tri.v[0] == v ? 0 : (tri.v[1] == v ? 1 : (tri.v[2] == v ? 2 : -1));
		}

	} // namespace

	// The fan around v is walked across the edges at v from `seed`, a live triangle on
	// v (-1: the locate grid finds one). The grid alone would scan every triangle whose
	// box holds the point, and a long sliver's box holds many. Ground pinched at v (two
	// fans meeting at one point) leaves corners the walk can't reach; the grid finds them.
	template <typename Visit> void PatchableMesh::forEachAroundVertex(std::uint32_t v, std::int32_t seed, Visit visit) const {
		if (seed < 0) {
			grid.forEachAtPoint(verts[v], 1, [&](std::int32_t t) {
				seed = cornerOf(tris[static_cast<std::size_t>(t)], v) >= 0 ? t : -1;
				return seed >= 0;
			});
			if (seed < 0) {
				return;
			}
		}
		// `from`, then the triangles across v's edges each way until the fan closes or
		// meets the mesh boundary.
		auto fan = [&](std::int32_t from, auto&& each) {
			each(from);
			for (const bool forward : {true, false}) {
				std::int32_t t = from;
				for (;;) {
					const NavTriangle& tri	= tris[static_cast<std::size_t>(t)];
					const int		   i	= cornerOf(tri, v);
					const std::int32_t next = tri.neighbor[static_cast<std::size_t>(forward ? i : prev3(i))];
					if (next < 0) {
						break;
					}
					if (next == from) {
						return;
					}
					each(next);
					t = next;
				}
			}
		};
		std::int32_t found = 0;
		fan(seed, [&](std::int32_t t) {
			++found;
			visit(t, cornerOf(tris[static_cast<std::size_t>(t)], v));
		});
		if (found == vertexUses[v]) {
			return;
		}
		std::vector<std::int32_t> seen;
		fan(seed, [&](std::int32_t t) { seen.push_back(t); });
		grid.forEachAtPoint(verts[v], 1, [&](std::int32_t t) {
			if (cornerOf(tris[static_cast<std::size_t>(t)], v) >= 0 && std::find(seen.begin(), seen.end(), t) == seen.end()) {
				fan(t, [&](std::int32_t u) {
					seen.push_back(u);
					visit(u, cornerOf(tris[static_cast<std::size_t>(u)], v));
				});
			}
			return false;
		});
	}

	// The mesh boundary runs along the pieces of bounds edges, so a box clear of every
	// bounds edge with pieces is clear of it (the margin covers how far snap rounding bends
	// them).
	bool PatchableMesh::nearBoundsEdge(const Vec2i64& lo, const Vec2i64& hi) const {
		bool near = false;
		edgeGrid.forEachInBox(lo, hi, 1, [&](std::int32_t id) {
			const EdgeSource& z = edgeOf[static_cast<std::size_t>(id)];
			near = near || (!rings[static_cast<std::size_t>(z.ring)].polygon.blocked && !pieces[static_cast<std::size_t>(id)].empty());
		});
		return near;
	}

	void PatchableMesh::ringEdgesInBox(const Vec2i64& lo, const Vec2i64& hi, std::vector<std::int32_t>& ids, std::vector<EdgeSource>& found) const {
		ids.clear();
		edgeGrid.forEachInBox(lo, hi, 1, [&](std::int32_t id) { ids.push_back(id); });
		std::sort(ids.begin(), ids.end());
		ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
		found.clear();
		for (std::int32_t id : ids) {
			found.push_back(edgeOf[static_cast<std::size_t>(id)]);
		}
		std::sort(found.begin(), found.end());
	}

	void PatchableMesh::indexRingEdges(RingId ring) {
		RegisteredRing&				reg		= rings[static_cast<std::size_t>(ring)];
		const std::vector<Vec2i64>& outline = reg.polygon.ring;
		reg.edgeIds.clear();
		for (std::int32_t e = 0; e < static_cast<std::int32_t>(outline.size()); ++e) {
			std::int32_t id = 0;
			if (!freeEdgeIds.empty()) {
				id = freeEdgeIds.back();
				freeEdgeIds.pop_back();
				edgeOf[static_cast<std::size_t>(id)] = {ring, e};
			} else {
				id = static_cast<std::int32_t>(edgeOf.size());
				edgeOf.push_back({ring, e});
				pieces.emplace_back();
			}
			Vec2i64 lo = edgeStart(outline, e);
			Vec2i64 hi = lo;
			growBox(lo, hi, edgeEnd(outline, e));
			edgeGrid.insert(id, lo, hi);
			reg.edgeIds.push_back(id);
		}
	}

	void PatchableMesh::unindexRingEdges(RingId ring) {
		RegisteredRing&				reg		= rings[static_cast<std::size_t>(ring)];
		const std::vector<Vec2i64>& outline = reg.polygon.ring;
		for (std::int32_t e = 0; e < static_cast<std::int32_t>(outline.size()); ++e) {
			Vec2i64 lo = edgeStart(outline, e);
			Vec2i64 hi = lo;
			growBox(lo, hi, edgeEnd(outline, e));
			edgeGrid.erase(reg.edgeIds[static_cast<std::size_t>(e)], lo, hi);
			assert(pieces[static_cast<std::size_t>(reg.edgeIds[static_cast<std::size_t>(e)])].empty() &&
				   "PatchableMesh: a ring edge leaves the index with pieces on the mesh");
			freeEdgeIds.push_back(reg.edgeIds[static_cast<std::size_t>(e)]);
		}
		reg.edgeIds.clear();
	}

	// Live rings whose outline box overlaps the box, ascending.
	std::vector<RingId> PatchableMesh::ringsInBox(const Vec2i64& lo, const Vec2i64& hi) const {
		std::vector<RingId> found;
		ringGrid.forEachInBox(lo, hi, 1, [&](std::int32_t r) { found.push_back(r); });
		std::sort(found.begin(), found.end());
		found.erase(std::unique(found.begin(), found.end()), found.end());
		return found;
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
				if (r != kGroundToken) {
					containing.push_back(&rings[static_cast<std::size_t>(r)].sample);
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
			pieces[static_cast<std::size_t>(rings[static_cast<std::size_t>(s.ring)].edgeIds[static_cast<std::size_t>(s.edge)])].push_back(t * 3 + e);
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
			std::vector<std::int32_t>& list = pieces[static_cast<std::size_t>(rings[static_cast<std::size_t>(s.ring)].edgeIds[static_cast<std::size_t>(s.edge)])];
			auto					   pos	= std::find(list.begin(), list.end(), t * 3 + e);
			assert(pos != list.end() && "PatchableMesh: a half-edge missing from the ring edge index");
			*pos = list.back();
			list.pop_back();
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
		assert(coverIds[static_cast<std::size_t>(t)] == coverIds[static_cast<std::size_t>(u)] && "PatchableMesh: flipping between two covers");

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

	struct PatchableMesh::PatchScratch {
		struct Change {
			bool			hadOld = false;
			bool			hasNew = false;
			NavInputPolygon polygon;
			SampleRing		sample;
			Vec2i64			lo;
			Vec2i64			hi;
			std::vector<EdgeSource> band; // new bounds: blocked edges near it not in s, see begin
			// New bounds: per outline edge, its boundary intervals after the patch, a run of
			// intervalPool.
			std::vector<std::pair<std::uint32_t, std::uint32_t>> intervals;
		};

		// A mesh edge in or around the hole that goes into the arrangement unchanged. Its
		// sources (all of them outside s) are a run of fixedSources.
		struct Fixed {
			std::uint32_t a			  = 0;
			std::uint32_t b			  = 0;
			std::uint32_t sourceBegin = 0;
			std::uint32_t sourceEnd	  = 0;
			bool		  boundary	  = false; // between the hole and a kept triangle
			std::int32_t  outside	  = -1;
			int			  outsideEdge = -1;
		};

		struct LocalSegment {
			EdgeSource	 source;
			std::int32_t fixed = -1;
		};

		// An old vertex a re-derived edge reaches.
		struct Touch {
			std::uint32_t vertex = 0;
			EdgeSource	  source;
			bool		  corner = false;
			auto		  operator<=>(const Touch&) const = default;
		};

		enum class FaceKind { Frame, Hole, Discard, NewGround, Sink };

		// Its triangles are a run of faceTris.
		struct LocalFace {
			FaceKind			kind	  = FaceKind::Frame;
			std::int32_t		sampleTri = -1;
			Int128				area2	  = Int128(0);
			std::uint32_t		trisBegin = 0;
			std::uint32_t		trisEnd	  = 0;
			bool				assigned  = false;
			std::vector<RingId> cover;
		};

		// Changed rings ascending by id; changeSlot finds one by ring id.
		std::vector<std::pair<RingId, Change>> changes;
		std::vector<std::int32_t>			   changeSlot;

		// s is the ring edges whose flags inS (by edge index id) holds.
		std::vector<std::uint8_t>  inS;
		std::vector<std::int32_t>  sIds;
		std::vector<EdgeSource>	   sList;
		std::vector<EdgeSource>	   sSorted;
		std::vector<EdgeSource>	   queue;
		std::vector<std::uint8_t>  inHole; // per triangle slot
		std::vector<std::int32_t>  hole;
		std::vector<std::pair<std::uint32_t, std::int32_t>> ends;
		std::vector<EdgeSource>	   edgesNear;
		std::vector<std::int32_t>  edgeIdsNear;
		std::vector<EdgeSource>	   edgesMet; // touchSegment's own, so its callers' lists stand
		std::vector<std::int32_t>  edgeIdsMet;
		std::vector<RingId>		   ringsNear; // live after the patch, around a seed sample

		// Boundary intervals after the patch: a new bounds edge's in its Change, an unchanged
		// one's, once asked for, at afterRange[edge index id] when afterStamp there is
		// intervalStamp.
		std::vector<GroundInterval>							 intervalPool;
		std::vector<std::uint32_t>							 afterStamp;
		std::vector<std::pair<std::uint32_t, std::uint32_t>> afterRange;
		std::uint32_t										 intervalStamp = 0;
		std::vector<GroundRing>								 groundNear;
		GroundBoundaryScratch								 boundaryScratch;
		std::vector<GroundInterval>							 intervals;
		std::vector<GroundInterval>							 intervalsBefore;
		std::vector<std::uint8_t>							 classified; // per edge index id, this patch
		std::vector<std::int32_t>							 classifiedIds;
		std::vector<Vec2i64>								 runEnds;
		std::vector<std::uint32_t>							 runEndList;
		bool												 coversGrew = false; // the cover read added to s

		std::vector<Fixed>				  fixed;
		std::vector<EdgeSource>			  fixedSources;
		std::vector<LocalSegment>		  segments;
		std::vector<InputSegment>		  input;
		ArrangementWorkspace			  arrangementWork;
		Arrangement						  arrangement;
		std::vector<std::int32_t>		  boundaryFixedOf; // per arrangement edge: boundary Fixed index, or -1
		std::vector<Touch>				  touching;
		std::vector<std::pair<EdgeSource, bool>> xs;
		std::vector<std::array<std::uint32_t, 2>> constraints;
		CdtWorkspace					  cdtWork;
		ConstrainedDelaunay				  cdt;
		std::vector<std::int32_t>		  faceOf;
		std::vector<LocalFace>			  faces; // the first faceCount are this pass's
		std::size_t						  faceCount = 0;
		std::vector<std::int32_t>		  faceTris;
		std::vector<std::int32_t>		  fill;
		std::vector<const SampleRing*>	  newBounds;
		std::vector<EdgeSource>			  sources;
		std::vector<RingId>				  flips;
		std::vector<RingId>				  across;
		std::vector<RingId>				  start;
		std::vector<std::int32_t>		  order;
		std::vector<std::int32_t>		  stack;
		std::vector<std::uint8_t>		  inGroup; // per face
		std::vector<std::int32_t>		  group;   // faces
		std::vector<std::pair<Int128, std::int32_t>> bySize;	// area, -triangle
		std::vector<std::pair<RingId, std::int64_t>> deviation; // ascending by ring, this pass
		std::vector<std::pair<RingId, std::int64_t>> strays;
		std::vector<std::pair<EdgeSource, std::int32_t>> arrangedPieces; // ring edge arranged whole, arrangement edge
		std::vector<std::pair<std::uint32_t, std::uint32_t>> meshPieces;
		std::vector<std::uint32_t>						 chainEnds;

		std::vector<std::uint8_t>				  kept;
		std::vector<std::uint8_t>				  faceKept;
		std::vector<std::uint32_t>				  meshVertex;
		std::vector<std::uint32_t>				  oldVertices;
		std::vector<std::int32_t>				  faceCover;
		std::vector<std::int32_t>				  edgeList;
		std::vector<std::int64_t>				  edgeProvenance;
		std::vector<std::int32_t>				  newId;
		std::vector<std::uint8_t>				  linked;
		std::vector<std::pair<std::int32_t, int>> boundaryEdges;
		std::vector<std::int32_t>				  flipped;
		std::vector<std::int32_t>				  unlinked; // kept triangles whose edge on the hole now borders nothing
		std::vector<std::int32_t>				  freed;	// ids whose old triangle is gone
		std::vector<std::int32_t>				  created;	// ids holding a new triangle
	};

	PatchableMesh::PatchScratchSlot::PatchScratchSlot() : scratch(std::make_unique<PatchScratch>()) {}
	PatchableMesh::PatchScratchSlot::PatchScratchSlot(const PatchScratchSlot&) : scratch(std::make_unique<PatchScratch>()) {}
	PatchableMesh::PatchScratchSlot::PatchScratchSlot(PatchScratchSlot&& other) noexcept = default;
	PatchableMesh::PatchScratchSlot& PatchableMesh::PatchScratchSlot::operator=(const PatchScratchSlot&) {
		scratch = std::make_unique<PatchScratch>();
		return *this;
	}
	PatchableMesh::PatchScratchSlot& PatchableMesh::PatchScratchSlot::operator=(PatchScratchSlot&& other) noexcept = default;
	PatchableMesh::PatchScratchSlot::~PatchScratchSlot() = default;

	class PatchableMesh::Patch {
	  public:
		Patch(PatchableMesh& owner, const PatchEdits& edits) : m(owner), work(*owner.patchScratch.scratch) { begin(edits); }

		PatchReport run();

	  private:
		using Change	   = PatchScratch::Change;
		using Fixed		   = PatchScratch::Fixed;
		using LocalSegment = PatchScratch::LocalSegment;
		using FaceKind	   = PatchScratch::FaceKind;
		using LocalFace	   = PatchScratch::LocalFace;

		void begin(const PatchEdits& edits);
		RingId allocateRing();

		const std::vector<Vec2i64>& oldOutline(RingId r) const { return m.rings[static_cast<std::size_t>(r)].polygon.ring; }
		const Change*				changeOf(RingId r) const;
		Change&						changeFor(RingId r);
		bool						changed(RingId r) const { return changeOf(r) != nullptr; }
		const NavInputPolygon&		newPolygon(RingId r) const;
		const SampleRing&			newSample(RingId r) const;
		bool						removed(RingId r) const;
		// A bounds ring, by its new polygon, or its old one when the patch removes it.
		bool						isBounds(RingId r) const;
		bool						inS(const EdgeSource& z) const;
		std::span<const EdgeSource> sourcesOf(const Fixed& f) const;
		std::span<const std::int32_t> trisOf(const LocalFace& face) const;
		std::vector<RingId>&		toggle(const std::vector<RingId>& cover, std::span<const EdgeSource> sources, std::vector<RingId>& out);

		// The bounds rings live after the patch whose boxes meet the box.
		void groundAfter(const Vec2i64& lo, const Vec2i64& hi, std::vector<GroundRing>& out) const;
		// A bounds edge's boundary intervals after the patch (an unchanged ring's edge, or a
		// changed ring's new outline edge), as a run of work.intervalPool.
		std::pair<std::uint32_t, std::uint32_t> intervalsAfter(const EdgeSource& x);
		std::span<const GroundInterval>			intervalRun(const std::pair<std::uint32_t, std::uint32_t>& run) const {
			return std::span<const GroundInterval>(work.intervalPool).subspan(run.first, run.second - run.first);
		}
		void classifyGroundEdges(const Vec2i64& lo, const Vec2i64& hi);

		void addToS(const EdgeSource& z);
		void addToHole(std::int32_t t);
		void pullOffGround(const Vec2i64& a, const Vec2i64& b, bool boundsEdge, const std::vector<EdgeSource>* band = nullptr);
		void pullAtVertex(std::uint32_t v, std::int32_t seed, std::span<const std::pair<EdgeSource, bool>> xs, bool takeStar);
		void touchSegment(const Vec2i64& a, const Vec2i64& b, const EdgeSource& x);
		void walkNewOutlines();
		void closeS();
		bool deriveOnce();
		void arrange();
		bool arrangementHeld();
		void triangulate();
		void assignCovers();
		bool coversHeld();
		const std::vector<EdgeSource>& sourcesOf(std::int32_t constraint);
		void collectFixed();
		void splice();

		PatchableMesh& m;
		PatchScratch&  work;
		PatchReport	   out;

		std::vector<std::pair<RingId, Change>>& changes		= work.changes;
		std::vector<EdgeSource>&				queue		= work.queue;
		std::vector<std::int32_t>&				hole		= work.hole; // work.inHole marks the same slots
		std::vector<Fixed>&						fixed		= work.fixed;
		std::vector<LocalSegment>&				segments	= work.segments;
		std::size_t								fixedStart	= 0;
		Arrangement&							arrangement = work.arrangement;
		ConstrainedDelaunay&					cdt			= work.cdt;
		std::vector<std::int32_t>&				faceOf		= work.faceOf;
		std::vector<LocalFace>&					faces		= work.faces;
		std::vector<std::int32_t>&				boundaryFixedOf = work.boundaryFixedOf;
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

	const PatchableMesh::Patch::Change* PatchableMesh::Patch::changeOf(RingId r) const {
		if (static_cast<std::size_t>(r) >= work.changeSlot.size() || work.changeSlot[static_cast<std::size_t>(r)] < 0) {
			return nullptr;
		}
		return &changes[static_cast<std::size_t>(work.changeSlot[static_cast<std::size_t>(r)])].second;
	}

	PatchableMesh::Patch::Change& PatchableMesh::Patch::changeFor(RingId r) {
		if (static_cast<std::size_t>(r) >= work.changeSlot.size()) {
			work.changeSlot.resize(static_cast<std::size_t>(r) + 1, -1);
		}
		std::int32_t& slot = work.changeSlot[static_cast<std::size_t>(r)];
		if (slot < 0) {
			slot = static_cast<std::int32_t>(changes.size());
			changes.emplace_back(r, Change{});
		}
		return changes[static_cast<std::size_t>(slot)].second;
	}

	const NavInputPolygon& PatchableMesh::Patch::newPolygon(RingId r) const {
		const Change* c = changeOf(r);
		return c != nullptr ? c->polygon : m.rings[static_cast<std::size_t>(r)].polygon;
	}

	const SampleRing& PatchableMesh::Patch::newSample(RingId r) const {
		const Change* c = changeOf(r);
		return c != nullptr ? c->sample : m.rings[static_cast<std::size_t>(r)].sample;
	}

	bool PatchableMesh::Patch::removed(RingId r) const {
		const Change* c = changeOf(r);
		return c != nullptr && !c->hasNew;
	}

	bool PatchableMesh::Patch::inS(const EdgeSource& z) const {
		const std::int32_t id = m.rings[static_cast<std::size_t>(z.ring)].edgeIds[static_cast<std::size_t>(z.edge)];
		return static_cast<std::size_t>(id) < work.inS.size() && work.inS[static_cast<std::size_t>(id)] != 0;
	}

	std::span<const EdgeSource> PatchableMesh::Patch::sourcesOf(const Fixed& f) const {
		return std::span<const EdgeSource>(work.fixedSources).subspan(f.sourceBegin, f.sourceEnd - f.sourceBegin);
	}

	std::span<const std::int32_t> PatchableMesh::Patch::trisOf(const LocalFace& face) const {
		return std::span<const std::int32_t>(work.faceTris).subspan(face.trisBegin, face.trisEnd - face.trisBegin);
	}

	std::vector<RingId>& PatchableMesh::Patch::toggle(const std::vector<RingId>& cover, std::span<const EdgeSource> sources, std::vector<RingId>& out) {
		toggledInto(cover, sources, [&](RingId r) { return isBounds(r); }, work.flips, out);
		return out;
	}

	bool PatchableMesh::Patch::isBounds(RingId r) const {
		const Change* c = changeOf(r);
		return !(c != nullptr && c->hasNew ? c->polygon : m.rings[static_cast<std::size_t>(r)].polygon).blocked;
	}

	void PatchableMesh::Patch::addToHole(std::int32_t t) {
		if (work.inHole[static_cast<std::size_t>(t)] == 0) {
			work.inHole[static_cast<std::size_t>(t)] = 1;
			hole.push_back(t);
		}
	}

	void PatchableMesh::Patch::addToS(const EdgeSource& z) {
		const std::int32_t id = m.rings[static_cast<std::size_t>(z.ring)].edgeIds[static_cast<std::size_t>(z.edge)];
		if (static_cast<std::size_t>(id) >= work.inS.size()) {
			work.inS.resize(m.edgeOf.size(), 0);
		}
		if (work.inS[static_cast<std::size_t>(id)] == 0) {
			work.inS[static_cast<std::size_t>(id)] = 1;
			work.sIds.push_back(id);
			work.sList.push_back(z);
			queue.push_back(z);
		}
	}

	void PatchableMesh::Patch::groundAfter(const Vec2i64& lo, const Vec2i64& hi, std::vector<GroundRing>& out) const {
		out.clear();
		m.ringGrid.forEachInBox(lo, hi, 1, [&](std::int32_t r) {
			const RegisteredRing& reg = m.rings[static_cast<std::size_t>(r)];
			if (changed(r) || reg.polygon.blocked) {
				return;
			}
			for (const GroundRing& g : out) {
				if (g.id == r) {
					return;
				}
			}
			out.push_back({&reg.polygon.ring, reg.lo, reg.hi, reg.sample.counterClockwise, r});
		});
		for (const auto& [id, c] : changes) {
			if (c.hasNew && !c.polygon.blocked && boxesOverlap(c.lo, c.hi, lo, hi)) {
				out.push_back({&c.polygon.ring, c.lo, c.hi, c.sample.counterClockwise, id});
			}
		}
	}

	std::pair<std::uint32_t, std::uint32_t> PatchableMesh::Patch::intervalsAfter(const EdgeSource& x) {
		if (const Change* c = changeOf(x.ring)) {
			assert(c->hasNew && !c->polygon.blocked && "PatchableMesh: boundary intervals of a ring that is not new bounds");
			return c->intervals[static_cast<std::size_t>(x.edge)];
		}
		const RegisteredRing& reg = m.rings[static_cast<std::size_t>(x.ring)];
		const std::size_t	  id  = static_cast<std::size_t>(reg.edgeIds[static_cast<std::size_t>(x.edge)]);
		if (work.afterStamp.size() < m.edgeOf.size()) {
			work.afterStamp.resize(m.edgeOf.size(), 0);
			work.afterRange.resize(m.edgeOf.size());
		}
		if (work.afterStamp[id] == work.intervalStamp) {
			return work.afterRange[id];
		}
		Vec2i64 lo = edgeStart(reg.polygon.ring, x.edge);
		Vec2i64 hi = lo;
		growBox(lo, hi, edgeEnd(reg.polygon.ring, x.edge));
		groundAfter(lo, hi, work.groundNear);
		groundBoundaries({&reg.polygon.ring, reg.lo, reg.hi, reg.sample.counterClockwise, x.ring}, x.edge, work.groundNear, work.boundaryScratch,
						 work.intervals);
		const std::uint32_t begin = static_cast<std::uint32_t>(work.intervalPool.size());
		work.intervalPool.insert(work.intervalPool.end(), work.intervals.begin(), work.intervals.end());
		work.afterStamp[id] = work.intervalStamp;
		work.afterRange[id] = {begin, static_cast<std::uint32_t>(work.intervalPool.size())};
		return work.afterRange[id];
	}

	// Every unchanged bounds edge whose segment box meets the box, and whose boundary
	// intervals the patch changes, joins s: its pieces go, and closeS touches its intervals
	// after the patch.
	void PatchableMesh::Patch::classifyGroundEdges(const Vec2i64& lo, const Vec2i64& hi) {
		m.ringEdgesInBox(lo, hi, work.edgeIdsNear, work.edgesNear);
		if (work.classified.size() < m.edgeOf.size()) {
			work.classified.resize(m.edgeOf.size(), 0);
		}
		for (std::size_t i = 0; i < work.edgesNear.size(); ++i) {
			const EdgeSource	  z	  = work.edgesNear[i];
			const RegisteredRing& reg = m.rings[static_cast<std::size_t>(z.ring)];
			if (changed(z.ring) || reg.polygon.blocked) {
				continue;
			}
			const std::int32_t id = reg.edgeIds[static_cast<std::size_t>(z.edge)];
			if (work.classified[static_cast<std::size_t>(id)] != 0) {
				continue;
			}
			work.classified[static_cast<std::size_t>(id)] = 1;
			work.classifiedIds.push_back(id);
			Vec2i64 zLo = edgeStart(reg.polygon.ring, z.edge);
			Vec2i64 zHi = zLo;
			growBox(zLo, zHi, edgeEnd(reg.polygon.ring, z.edge));
			m.groundRingsInBox(zLo, zHi, work.groundNear);
			groundBoundaries({&reg.polygon.ring, reg.lo, reg.hi, reg.sample.counterClockwise, z.ring}, z.edge, work.groundNear, work.boundaryScratch,
							 work.intervalsBefore);
			const std::span<const GroundInterval> after = intervalRun(intervalsAfter(z));
			if (!std::equal(after.begin(), after.end(), work.intervalsBefore.begin(), work.intervalsBefore.end())) {
				addToS(z);
			}
		}
	}

	// On kept ground the mesh and touchSegment's input test show every interaction, but a
	// segment that reaches off ground can come near a hanging ring there without meeting
	// it, and a rounded crossing can still land on ground. Off ground and near its edge the registry stands in: every blocked ring
	// edge whose original comes within kOffGroundReachMm joins s. A bounds edge runs along
	// the boundary, so it always asks the registry, but it never pulls the boundary
	// pieces beside it: those are bounds edges too, which the mesh shows whole, and
	// pulling them would walk the whole border.
	void PatchableMesh::Patch::pullOffGround(const Vec2i64& a, const Vec2i64& b, bool boundsEdge, const std::vector<EdgeSource>* band) {
		const Vec2i64 reach{kOffGroundReachMm, kOffGroundReachMm};
		Vec2i64		  lo = a;
		Vec2i64		  hi = a;
		growBox(lo, hi, b);
		lo = lo - reach;
		hi = hi + reach;
		if (!boundsEdge) {
			bool nearBoundary = false;
			if (m.nearBoundsEdge(lo - reach, hi + reach)) {
				m.forEachLiveInBox(lo, hi, [&](std::int32_t t) {
					const NavTriangle& tri = m.tris[static_cast<std::size_t>(t)];
					for (int e = 0; e < 3; ++e) {
						if (tri.neighbor[static_cast<std::size_t>(e)] >= 0) {
							continue;
						}
						const Vec2i64& p = m.verts[tri.v[static_cast<std::size_t>(e)]];
						const Vec2i64& q = m.verts[tri.v[static_cast<std::size_t>(next3(e))]];
						if (!segmentsWithin(a, b, p, q, kOffGroundReachMm)) {
							continue;
						}
						// Off ground a segment can still end on the boundary, at a vertex
						// with no piece of it on the mesh.
						nearBoundary = true;
						for (const EdgeSource& z : m.edgeSources(t, e)) {
							addToS(z);
						}
					}
				});
			}
			if (!nearBoundary && m.locateScaled(a + b, 2) >= 0) {
				return;
			}
		}
		if (band != nullptr) {
			work.edgesNear.clear();
			for (const EdgeSource& z : *band) {
				const std::vector<Vec2i64>& outline = m.rings[static_cast<std::size_t>(z.ring)].polygon.ring;
				Vec2i64						zLo		= edgeStart(outline, z.edge);
				Vec2i64						zHi		= zLo;
				growBox(zLo, zHi, edgeEnd(outline, z.edge));
				if (boxesOverlap(lo, hi, zLo, zHi)) {
					work.edgesNear.push_back(z);
				}
			}
		} else {
			m.ringEdgesInBox(lo, hi, work.edgeIdsNear, work.edgesNear);
		}
		for (const EdgeSource& z : work.edgesNear) {
			const RegisteredRing& reg = m.rings[static_cast<std::size_t>(z.ring)];
			if (!reg.polygon.blocked || changed(z.ring) || inS(z)) {
				continue;
			}
			const Vec2i64 p = edgeStart(reg.polygon.ring, z.edge);
			const Vec2i64 q = edgeEnd(reg.polygon.ring, z.edge);
			if (p != q && segmentsWithin(a, b, p, q, kOffGroundReachMm)) {
				addToS(z);
			}
		}
	}

	void PatchableMesh::Patch::begin(const PatchEdits& edits) {
		// The last patch's marks, cleared by what it marked.
		for (std::int32_t id : work.sIds) {
			work.inS[static_cast<std::size_t>(id)] = 0;
		}
		for (const auto& [id, c] : changes) {
			work.changeSlot[static_cast<std::size_t>(id)] = -1;
		}
		for (std::int32_t t : hole) {
			work.inHole[static_cast<std::size_t>(t)] = 0;
		}
		work.sIds.clear();
		work.sList.clear();
		changes.clear();
		queue.clear();
		hole.clear();
		if (work.inHole.size() < m.tris.size()) {
			work.inHole.resize(m.tris.size(), 0);
		}

		auto setNew = [&](Change& c, NavInputPolygon polygon) {
			assert(polygon.ring.size() >= 3 && "PatchableMesh: a ring needs three vertices");
			if (!polygon.blocked) {
				polygon.ring = splitLongEdges(polygon.ring, m.maxBoundsEdgeMm);
			}
			c.hasNew  = true;
			c.polygon = std::move(polygon);
			c.sample  = sampleRing(c.polygon);
			outlineBox(c.polygon.ring, c.lo, c.hi);
		};
		for (const NavInputPolygon& add : edits.adds) {
			const RingId id = allocateRing();
			out.added.push_back(id);
			setNew(changeFor(id), add);
		}
		// A dead ring, or one edited twice in a patch, is a caller bug: refused, so the
		// registry and its free list stay sound.
		auto editable = [&](RingId id) {
			const bool ok = id >= 0 && static_cast<std::size_t>(id) < m.rings.size() && m.rings[static_cast<std::size_t>(id)].live && !changed(id);
			assert(ok && "PatchableMesh: editing a dead ring, or one ring twice in a patch");
			return ok;
		};
		for (RingId id : edits.removes) {
			if (!editable(id)) {
				continue;
			}
			changeFor(id).hadOld = true;
		}
		for (const RingMove& move : edits.moves) {
			if (!editable(move.ring)) {
				continue;
			}
			Change& c	   = changeFor(move.ring);
			c.hadOld	   = true;
			NavInputPolygon moved = m.rings[static_cast<std::size_t>(move.ring)].polygon;
			moved.ring			  = move.outline;
			setNew(c, std::move(moved));
		}
		// Every mark covers the registry as it now stands, so a copied mesh's empty scratch
		// is read in bounds too.
		work.changeSlot.resize(std::max(work.changeSlot.size(), m.rings.size()), -1);
		work.inS.resize(std::max(work.inS.size(), m.edgeOf.size()), 0);
		std::sort(changes.begin(), changes.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		for (std::size_t i = 0; i < changes.size(); ++i) {
			work.changeSlot[static_cast<std::size_t>(changes[i].first)] = static_cast<std::int32_t>(i);
		}

		for (const auto& [id, c] : changes) {
			if (c.hadOld) {
				const std::int32_t n = static_cast<std::int32_t>(oldOutline(id).size());
				for (std::int32_t e = 0; e < n; ++e) {
					addToS({id, e});
				}
			}
		}

		// Boundary intervals after the patch: a new bounds ring's for every edge now; an
		// unchanged edge's when first asked for.
		work.intervalPool.clear();
		if (++work.intervalStamp == 0) {
			std::fill(work.afterStamp.begin(), work.afterStamp.end(), 0);
			work.intervalStamp = 1;
		}
		for (std::int32_t id : work.classifiedIds) {
			work.classified[static_cast<std::size_t>(id)] = 0;
		}
		work.classifiedIds.clear();
		for (auto& [id, c] : changes) {
			if (!c.hasNew || c.polygon.blocked) {
				continue;
			}
			c.intervals.clear();
			for (std::int32_t e = 0; e < static_cast<std::int32_t>(c.polygon.ring.size()); ++e) {
				Vec2i64 lo = edgeStart(c.polygon.ring, e);
				Vec2i64 hi = lo;
				growBox(lo, hi, edgeEnd(c.polygon.ring, e));
				groundAfter(lo, hi, work.groundNear);
				groundBoundaries({&c.polygon.ring, c.lo, c.hi, c.sample.counterClockwise, id}, e, work.groundNear, work.boundaryScratch, work.intervals);
				const std::uint32_t begin = static_cast<std::uint32_t>(work.intervalPool.size());
				work.intervalPool.insert(work.intervalPool.end(), work.intervals.begin(), work.intervals.end());
				c.intervals.push_back({begin, static_cast<std::uint32_t>(work.intervalPool.size())});
			}
		}

		// A bounds ring arriving, leaving, or moving changes the boundary intervals only of
		// bounds edges whose boxes meet its own.
		for (const auto& [id, c] : changes) {
			if (c.hadOld && !m.rings[static_cast<std::size_t>(id)].polygon.blocked) {
				classifyGroundEdges(m.rings[static_cast<std::size_t>(id)].lo, m.rings[static_cast<std::size_t>(id)].hi);
			}
			if (c.hasNew && !c.polygon.blocked) {
				classifyGroundEdges(c.lo, c.hi);
			}
		}

		// New ground completes every blocked ring hanging over it: an edge that meets the new
		// outline's area and does not lie wholly on old ground re-derives (a ring around the
		// whole new ground is found by the seeding); one wholly on old ground keeps its
		// pieces. The rest of the blocked edges within kOffGroundReachMm of the new outline's
		// box are what the off-ground pull of its edges reads, so those skip the registry.
		for (auto& [id, c] : changes) {
			if (!c.hasNew || c.polygon.blocked) {
				continue;
			}
			const Vec2i64 reach{kOffGroundReachMm, kOffGroundReachMm};
			m.ringEdgesInBox(c.lo - reach, c.hi + reach, work.edgeIdsNear, work.edgesNear);
			c.band.clear();
			for (const EdgeSource& z : work.edgesNear) {
				const RegisteredRing& reg = m.rings[static_cast<std::size_t>(z.ring)];
				if (changed(z.ring) || !reg.polygon.blocked) {
					continue;
				}
				const Vec2i64 p	 = edgeStart(reg.polygon.ring, z.edge);
				const Vec2i64 q	 = edgeEnd(reg.polygon.ring, z.edge);
				Vec2i64		  zLo = p;
				Vec2i64		  zHi = p;
				growBox(zLo, zHi, q);
				work.runEnds.assign({p, q});
				if (p != q && boxesOverlap(c.lo, c.hi, zLo, zHi) && !m.piecesRunBetween(z, work.runEnds, work.meshPieces, work.runEndList) &&
					segmentMeetsArea(p, q, c.polygon.ring)) {
					addToS(z);
				} else {
					c.band.push_back(z);
				}
			}
		}
	}

	// Every ring edge on the mesh at vertex v joins s unless v is an original corner of
	// both it and each x touching v there: a corner of neither, or of one only, means one
	// split the other at v.
	void PatchableMesh::Patch::pullAtVertex(std::uint32_t v, std::int32_t seed, std::span<const std::pair<EdgeSource, bool>> xs, bool takeStar) {
		const Vec2i64& p = m.verts[v];
		m.forEachAroundVertex(v, seed, [&](std::int32_t t, int i) {
			if (takeStar) {
				addToHole(t);
			}
			for (int e : {i, prev3(i)}) {
				for (const EdgeSource& z : m.edgeSources(t, e)) {
					if (inS(z)) {
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

	// The triangles segment ab (ring edge x) touches join the hole, and every ring edge
	// it crosses or touches, as pieces or as input, joins s.
	void PatchableMesh::Patch::touchSegment(const Vec2i64& a, const Vec2i64& b, const EdgeSource& x) {
		Vec2i64 lo = a;
		Vec2i64 hi = a;
		growBox(lo, hi, b);
		m.forEachLiveInBox(lo, hi, [&](std::int32_t t) {
			const NavTriangle&			 tri = m.tris[static_cast<std::size_t>(t)];
			const std::array<Vec2i64, 3> pts{m.verts[tri.v[0]], m.verts[tri.v[1]], m.verts[tri.v[2]]};
			if (!segmentTouchesTriangle(a, b, pts)) {
				return;
			}
			addToHole(t);
			for (int k = 0; k < 3; ++k) {
				const std::vector<EdgeSource>& sources = m.edgeSources(t, k);
				if (sources.empty()) {
					continue;
				}
				const Vec2i64&			  p = pts[static_cast<std::size_t>(k)];
				const Vec2i64&			  q = pts[static_cast<std::size_t>(next3(k))];
				const SegmentIntersection r = intersectSegments(a, b, p, q);
				if (r.relation == SegmentRelation::Disjoint || (r.relation == SegmentRelation::EndpointTouch && (r.point == p || r.point == q))) {
					continue;
				}
				for (const EdgeSource& z : sources) {
					addToS(z);
				}
			}
			for (int k = 0; k < 3; ++k) {
				const Vec2i64& p = pts[static_cast<std::size_t>(k)];
				if (onClosedSegment(p, a, b)) {
					const std::pair<EdgeSource, bool> one{x, p == a || p == b};
					pullAtVertex(tri.v[static_cast<std::size_t>(k)], t, {&one, 1}, false);
				}
			}
		});
		// The walk reads the pieces as rounded, but the arrangement meets the inputs as given:
		// a piece can pass clear of ab where its input meets it. Every input lies in its
		// edge's box, so the edge grid finds each one that can.
		auto meets = [&](const Vec2i64& p, const Vec2i64& q) {
			const SegmentIntersection r = intersectSegments(a, b, p, q);
			if (r.relation == SegmentRelation::Disjoint) {
				return false;
			}
			return r.relation != SegmentRelation::EndpointTouch || !((r.point == a || r.point == b) && (r.point == p || r.point == q));
		};
		m.ringEdgesInBox(lo, hi, work.edgeIdsMet, work.edgesMet);
		for (const EdgeSource& z : work.edgesMet) {
			if (changed(z.ring) || inS(z)) {
				continue;
			}
			const NavInputPolygon& polygon = m.rings[static_cast<std::size_t>(z.ring)].polygon;
			const Vec2i64		   p	   = edgeStart(polygon.ring, z.edge);
			const Vec2i64		   q	   = edgeEnd(polygon.ring, z.edge);
			if (p == q) {
				continue;
			}
			if (polygon.blocked) {
				if (meets(p, q)) {
					addToS(z);
				}
				continue;
			}
			// A boundary interval's ends are rounded crossings, within a millimetre of the edge.
			if (!segmentsWithin(a, b, p, q, kOffGroundReachMm)) {
				continue;
			}
			const std::pair<std::uint32_t, std::uint32_t> run = intervalsAfter(z);
			for (std::uint32_t i = run.first; i < run.second; ++i) {
				const GroundInterval iv = work.intervalPool[i];
				if (meets(iv.from, iv.to)) {
					addToS(z);
					break;
				}
			}
		}
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
				if (!c.polygon.blocked) {
					// touchSegment can grow the interval pool, so each interval is copied out.
					const std::pair<std::uint32_t, std::uint32_t> run = c.intervals[static_cast<std::size_t>(e)];
					for (std::uint32_t i = run.first; i < run.second; ++i) {
						const GroundInterval iv = work.intervalPool[i];
						pullOffGround(iv.from, iv.to, true, &c.band);
						touchSegment(iv.from, iv.to, {id, e});
					}
					continue;
				}
				const Vec2i64 a = edgeStart(outline, e);
				const Vec2i64 b = edgeEnd(outline, e);
				if (a == b) {
					continue;
				}
				pullOffGround(a, b, false);
				touchSegment(a, b, {id, e});
			}
			// Old ground inside new ground stays ground, and only boundary intervals are
			// arranged, so nothing clear of them changes there (old ground a bounds ring this
			// patch moves or removes joins below). A chunk-sized box query would scan the
			// whole mesh.
			if (!c.polygon.blocked) {
				continue;
			}
			m.forEachLiveInBox(c.lo, c.hi, [&](std::int32_t t) {
				const NavTriangle& tri = m.tris[static_cast<std::size_t>(t)];
				if (ringContainsSample(c.sample, m.verts[tri.v[0]] + m.verts[tri.v[1]] + m.verts[tri.v[2]])) {
					addToHole(t);
				}
			});
		}
		for (const auto& [id, c] : changes) {
			if (!c.hadOld) {
				continue;
			}
			const RegisteredRing& reg = m.rings[static_cast<std::size_t>(id)];
			if (reg.polygon.blocked) {
				m.forEachLiveInBox(reg.lo, reg.hi, [&](std::int32_t t) {
					const std::vector<RingId>& cov = m.cover(t);
					if (std::binary_search(cov.begin(), cov.end(), id)) {
						addToHole(t);
					}
				});
				continue;
			}
			// A cover does not name bounds rings: ground whose centroid the old outline
			// contains, or lies as near its outline as snap rounding bent its pieces (the
			// containment test then does not decide; a neighbour's triangle taken too only
			// widens the hole).
			const std::int64_t			stray = m.pieceDeviationMm(id);
			const std::vector<Vec2i64>& ring3 = reg.sample.ring3;
			m.forEachLiveInBox(reg.lo, reg.hi, [&](std::int32_t t) {
				const std::vector<RingId>& cov = m.cover(t);
				if (cov.empty() || cov.front() != kGroundToken) {
					return;
				}
				const NavTriangle& tri	   = m.tris[static_cast<std::size_t>(t)];
				const Vec2i64	   sample3 = m.verts[tri.v[0]] + m.verts[tri.v[1]] + m.verts[tri.v[2]];
				bool			   inside  = ringContainsSample(reg.sample, sample3);
				for (std::size_t i = 0; i < ring3.size() && !inside; ++i) {
					inside = withinDistanceOfSegment(sample3, ring3[i], ring3[(i + 1) % ring3.size()], 3 * stray);
				}
				if (inside) {
					addToHole(t);
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
			const Vec2i64 a		 = edgeStart(oldOutline(x.ring), x.edge);
			const Vec2i64 b		 = edgeEnd(oldOutline(x.ring), x.edge);
			const bool	  bounds = !m.rings[static_cast<std::size_t>(x.ring)].polygon.blocked;
			if (bounds && !changed(x.ring)) {
				// What it arranges after the patch is its boundary intervals; ground may run
				// across where they lie now, so the triangles on them join the hole.
				const std::pair<std::uint32_t, std::uint32_t> run = intervalsAfter(x);
				for (std::uint32_t i = run.first; i < run.second; ++i) {
					const GroundInterval iv = work.intervalPool[i];
					pullOffGround(iv.from, iv.to, true);
					touchSegment(iv.from, iv.to, x);
				}
			} else if (a != b) {
				pullOffGround(a, b, bounds);
			}
			const std::vector<std::int32_t>& under = m.piecesOf(x);
			std::vector<std::pair<std::uint32_t, std::int32_t>>& ends = work.ends; // vertex, a triangle on it
			ends.clear();
			for (std::int32_t h : under) {
				const std::int32_t t   = h / 3;
				const int		   e   = h % 3;
				const NavTriangle& tri = m.tris[static_cast<std::size_t>(t)];
				addToHole(t);
				ends.push_back({tri.v[static_cast<std::size_t>(e)], t});
				ends.push_back({tri.v[static_cast<std::size_t>(next3(e))], t});
			}
			std::sort(ends.begin(), ends.end());
			ends.erase(std::unique(ends.begin(), ends.end(), [](const auto& a, const auto& b) { return a.first == b.first; }), ends.end());
			// The whole star of each vertex: one that goes must not stay on the boundary.
			for (const auto& [v, seed] : ends) {
				const std::pair<EdgeSource, bool> one{x, isOriginalEndpoint(oldOutline(x.ring), x.edge, m.verts[v])};
				pullAtVertex(v, seed, {&one, 1}, true);
			}
		}
	}

	void PatchableMesh::Patch::collectFixed() {
		std::sort(hole.begin(), hole.end());
		fixed.clear();
		work.fixedSources.clear();
		// f takes the sources outside s; false when none are.
		auto outsideS = [&](Fixed& f, const std::vector<EdgeSource>& sources) {
			f.sourceBegin = static_cast<std::uint32_t>(work.fixedSources.size());
			for (const EdgeSource& z : sources) {
				if (!inS(z)) {
					work.fixedSources.push_back(z);
				}
			}
			f.sourceEnd = static_cast<std::uint32_t>(work.fixedSources.size());
			return f.sourceEnd != f.sourceBegin;
		};
		for (std::int32_t t : hole) {
			const NavTriangle& tri = m.tris[static_cast<std::size_t>(t)];
			for (int e = 0; e < 3; ++e) {
				const std::int32_t			   u	   = tri.neighbor[static_cast<std::size_t>(e)];
				const std::vector<EdgeSource>& sources = m.edgeSources(t, e);
				Fixed						   f;
				f.a = tri.v[static_cast<std::size_t>(e)];
				f.b = tri.v[static_cast<std::size_t>(next3(e))];
				if (u >= 0 && work.inHole[static_cast<std::size_t>(u)] != 0) {
					if (sources.empty() || u < t || !outsideS(f, sources)) {
						continue;
					}
				} else if (u >= 0) {
					outsideS(f, sources);
					assert(f.sourceEnd - f.sourceBegin == sources.size() && "PatchableMesh: a piece of s on the hole boundary");
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
					if (!outsideS(f, sources)) {
						continue;
					}
				}
				fixed.push_back(f);
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
		if (work.coversGrew) {
			closeS();
			return false;
		}
		return coversHeld();
	}

	// s's edges after the patch (the new outline for a changed ring): a blocked edge whole,
	// a bounds edge's boundary intervals; and the fixed edges.
	void PatchableMesh::Patch::arrange() {
		segments.clear();
		std::vector<InputSegment>& input = work.input;
		input.clear();
		work.sSorted.assign(work.sList.begin(), work.sList.end());
		std::sort(work.sSorted.begin(), work.sSorted.end());
		// A kept stretch dissolved after the patch has its pieces, if any, in the hole with the
		// whole star of each piece end (closeS), so an end no other edge keeps goes.
		auto addIntervals = [&](const EdgeSource& x, const std::pair<std::uint32_t, std::uint32_t>& run) {
			for (const GroundInterval& iv : intervalRun(run)) {
				input.push_back({iv.from, iv.to, static_cast<std::int64_t>(segments.size())});
				segments.push_back({x, -1});
			}
		};
		for (const EdgeSource& x : work.sSorted) {
			if (changed(x.ring)) {
				continue;
			}
			const NavInputPolygon& polygon = m.rings[static_cast<std::size_t>(x.ring)].polygon;
			if (!polygon.blocked) {
				addIntervals(x, intervalsAfter(x));
				continue;
			}
			const Vec2i64 a = edgeStart(polygon.ring, x.edge);
			const Vec2i64 b = edgeEnd(polygon.ring, x.edge);
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
				if (!c.polygon.blocked) {
					addIntervals({id, e}, c.intervals[static_cast<std::size_t>(e)]);
					continue;
				}
				const Vec2i64 a = edgeStart(c.polygon.ring, e);
				const Vec2i64 b = edgeEnd(c.polygon.ring, e);
				if (a == b) {
					continue;
				}
				input.push_back({a, b, static_cast<std::int64_t>(segments.size())});
				segments.push_back({{id, e}, -1});
			}
		}
		fixedStart = segments.size();
		for (std::size_t f = 0; f < fixed.size(); ++f) {
			input.push_back({m.verts[fixed[f].a], m.verts[fixed[f].b], static_cast<std::int64_t>(segments.size())});
			segments.push_back({{}, static_cast<std::int32_t>(f)});
		}
		buildArrangement(input, work.arrangementWork, arrangement);
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
			for (const EdgeSource& z : sourcesOf(f)) {
				addToS(z);
			}
			if (f.boundary) {
				addToHole(f.outside);
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
		using Touch					 = PatchScratch::Touch;
		std::vector<Touch>& touching = work.touching;
		touching.clear();
		for (const ArrangementEdge& ae : arrangement.edges) {
			const Vec2i64& p = arrangement.vertices[ae.from];
			const Vec2i64& q = arrangement.vertices[ae.to];
			bool		   derived = false;
			for (std::int64_t local : ae.provenance) {
				derived = derived || fromS(local);
			}
			if (!derived) {
				continue;
			}
			for (const Vec2i64& end : {p, q}) {
				const std::uint32_t* old = m.vertexAt.find(end);
				if (old == nullptr) {
					continue;
				}
				for (std::int64_t local : ae.provenance) {
					if (fromS(local)) {
						const EdgeSource& x = segments[static_cast<std::size_t>(local)].source;
						touching.push_back({*old, x, isOriginalEndpoint(newPolygon(x.ring).ring, x.edge, end)});
					}
				}
			}
			Vec2i64 lo = p;
			Vec2i64 hi = p;
			growBox(lo, hi, q);
			m.forEachLiveInBox(lo, hi, [&](std::int32_t t) {
				if (work.inHole[static_cast<std::size_t>(t)] != 0) {
					return;
				}
				const NavTriangle& tri = m.tris[static_cast<std::size_t>(t)];
				if (segmentEntersTriangle(p, q, {m.verts[tri.v[0]], m.verts[tri.v[1]], m.verts[tri.v[2]]})) {
					addToHole(t);
					grew = true;
				}
			});
		}
		std::sort(touching.begin(), touching.end());
		std::vector<std::pair<EdgeSource, bool>>& xs = work.xs;
		for (std::size_t i = 0; i < touching.size();) {
			const std::uint32_t v = touching[i].vertex;
			xs.clear();
			for (; i < touching.size() && touching[i].vertex == v; ++i) {
				xs.push_back({touching[i].source, touching[i].corner});
			}
			const std::size_t before = work.sList.size();
			pullAtVertex(v, -1, xs, false);
			grew = grew || work.sList.size() != before;
		}
		return !grew;
	}

	// The hole arrangement triangulated, its faces found, and each face placed: in the
	// hole, on kept ground outside it, on new ground, or off every mesh.
	void PatchableMesh::Patch::triangulate() {
		std::vector<std::array<std::uint32_t, 2>>& constraints = work.constraints;
		constraints.clear();
		for (const ArrangementEdge& ae : arrangement.edges) {
			constraints.push_back({static_cast<std::uint32_t>(ae.from), static_cast<std::uint32_t>(ae.to)});
		}
		buildConstrainedDelaunay(arrangement.vertices, constraints, work.cdtWork, cdt);

		std::vector<const SampleRing*>& newBounds = work.newBounds;
		newBounds.clear();
		for (const auto& [id, c] : changes) {
			if (c.hasNew && !c.polygon.blocked) {
				newBounds.push_back(&c.sample);
			}
		}

		// Faces of the hole triangulation: flood across unconstrained edges.
		const std::int32_t cdtCount = static_cast<std::int32_t>(cdt.triangles.size());
		faceOf.assign(static_cast<std::size_t>(cdtCount), -1);
		work.faceCount = 0;
		work.faceTris.clear();
		std::vector<std::int32_t>& fill = work.fill;
		for (std::int32_t seed = 0; seed < cdtCount; ++seed) {
			if (faceOf[static_cast<std::size_t>(seed)] >= 0) {
				continue;
			}
			const std::int32_t f = static_cast<std::int32_t>(work.faceCount++);
			if (faces.size() < work.faceCount) {
				faces.emplace_back();
			}
			LocalFace& face = faces[static_cast<std::size_t>(f)];
			face.kind		= FaceKind::Frame;
			face.sampleTri	= -1;
			face.area2		= Int128(0);
			face.trisBegin	= static_cast<std::uint32_t>(work.faceTris.size());
			face.assigned	= false;
			face.cover.clear();
			bool frame = false;
			faceOf[static_cast<std::size_t>(seed)] = f;
			fill.push_back(seed);
			while (!fill.empty()) {
				const std::int32_t t = fill.back();
				fill.pop_back();
				work.faceTris.push_back(t);
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
			face.trisEnd = static_cast<std::uint32_t>(work.faceTris.size());
			if (frame) {
				continue;
			}
			const CdtTriangle& st	   = cdt.triangles[static_cast<std::size_t>(face.sampleTri)];
			const Vec2i64	   sample3 = cdt.vertices[st.v[0]] + cdt.vertices[st.v[1]] + cdt.vertices[st.v[2]];
			// The sample can lie on an old mesh edge (a dissolved side, or a side about to
			// be): the old triangle and every containment test take it moved the same way.
			const std::int32_t old = m.locateSample(sample3);
			if (old >= 0) {
				face.kind = work.inHole[static_cast<std::size_t>(old)] != 0 ? FaceKind::Hole : FaceKind::Discard;
				continue;
			}
			face.kind = FaceKind::Sink;
			for (const SampleRing* bounds : newBounds) {
				if (boundsContainSample(*bounds, sample3)) {
					face.kind = FaceKind::NewGround;
				}
			}
		}
	}

	// Covers: one exact seed per component, then toggle across each ring edge by the
	// rings on it. A sink (its sample off the old mesh and outside every new bounds ring)
	// takes a cover but passes none on, since rings over no ground have no edges here;
	// one whose cover is ground (a sliver its sample misread beside a bent edge) passes
	// it on, every ring edge around ground being arranged.
	void PatchableMesh::Patch::assignCovers() {
		const std::int32_t cdtCount = static_cast<std::int32_t>(cdt.triangles.size());
		bool conflict = false;
		work.coversGrew = false;
		auto holdsGround = [](const std::vector<RingId>& cover) { return !cover.empty() && cover.front() == kGroundToken; };
		std::vector<std::int32_t>& stack = work.stack;
		auto flood = [&](std::int32_t seed, const std::vector<RingId>& seedCover) {
			LocalFace& sf = faces[static_cast<std::size_t>(seed)];
			sf.cover	  = seedCover;
			sf.assigned	  = true;
			stack.assign(1, seed);
			while (!stack.empty()) {
				const std::int32_t f = stack.back();
				stack.pop_back();
				const LocalFace& face = faces[static_cast<std::size_t>(f)];
				if (face.kind == FaceKind::Sink && !holdsGround(face.cover)) {
					continue;
				}
				for (std::int32_t t : trisOf(face)) {
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
						const std::vector<RingId>& across = toggle(face.cover, sourcesOf(c), work.across);
						if (!next.assigned) {
							next.cover	  = across;
							next.assigned = true;
							stack.push_back(g);
						} else if (next.cover != across && (next.kind != FaceKind::Sink || holdsGround(next.cover) || holdsGround(across))) {
							conflict = true;
						}
					}
				}
			}
		};

		// Seeds, best first. Across the fixed boundary the kept triangle's cover carries
		// in exactly, with no sample, a sink's too (it passes the cover on if it is
		// ground). A group that reaches no boundary (new ground, or ground whose every
		// triangle is in the hole) takes its cover from the input rings at one sample, the
		// centroid of its largest triangle clear of the band every ring near it was bent
		// within; ground is any ground ring holding it.
		for (std::int32_t t = 0; t < cdtCount; ++t) {
			const CdtTriangle& tri	= cdt.triangles[static_cast<std::size_t>(t)];
			const std::int32_t f	= faceOf[static_cast<std::size_t>(t)];
			LocalFace&		   face = faces[static_cast<std::size_t>(f)];
			if (face.assigned || (face.kind != FaceKind::Hole && face.kind != FaceKind::NewGround && face.kind != FaceKind::Sink)) {
				continue;
			}
			for (int e = 0; e < 3 && !face.assigned; ++e) {
				const std::int32_t c = tri.constraint[static_cast<std::size_t>(e)];
				if (c >= 0 && boundaryFixedOf[static_cast<std::size_t>(c)] >= 0) {
					const Fixed& fx = fixed[static_cast<std::size_t>(boundaryFixedOf[static_cast<std::size_t>(c)])];
					flood(f, toggle(m.cover(fx.outside), sourcesOf(fx), work.start));
				}
			}
		}
		std::vector<std::int32_t>& order = work.order;
		order.clear();
		for (std::int32_t f = 0; f < static_cast<std::int32_t>(work.faceCount); ++f) {
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
		// How far each ring's pieces in this pass stray from its outline, ascending by ring.
		std::vector<std::pair<RingId, std::int64_t>>& deviation = work.deviation;
		auto measureDeviation = [&]() {
			deviation.clear();
			auto measure = [&](const EdgeSource& x, const Vec2i64& p, const Vec2i64& q) {
				const std::vector<Vec2i64>& outline = newPolygon(x.ring).ring;
				const Vec2i64				a		= edgeStart(outline, x.edge);
				const Vec2i64				dir		= edgeEnd(outline, x.edge) - a;
				const Int128				len2	= dot(dir, dir);
				deviation.push_back({x.ring, std::max(offLineMm(a, dir, len2, p), offLineMm(a, dir, len2, q))});
			};
			for (const ArrangementEdge& ae : arrangement.edges) {
				const Vec2i64& p = arrangement.vertices[ae.from];
				const Vec2i64& q = arrangement.vertices[ae.to];
				for (std::int64_t local : ae.provenance) {
					const LocalSegment& seg = segments[static_cast<std::size_t>(local)];
					if (seg.fixed < 0) {
						measure(seg.source, p, q);
						continue;
					}
					for (const EdgeSource& x : sourcesOf(fixed[static_cast<std::size_t>(seg.fixed)])) {
						measure(x, p, q);
					}
				}
			}
			std::sort(deviation.begin(), deviation.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first < b.first : a.second > b.second; });
			deviation.erase(std::unique(deviation.begin(), deviation.end(), [](const auto& a, const auto& b) { return a.first == b.first; }), deviation.end());
		};
		// A ring's stray: this pass's pieces, and the kept pieces of a ring the patch leaves.
		std::vector<std::pair<RingId, std::int64_t>>& strays = work.strays;
		strays.clear();
		auto strayOf = [&](RingId r) {
			for (const auto& [ring, stray] : strays) {
				if (ring == r) {
					return stray;
				}
			}
			auto		 it	   = std::lower_bound(deviation.begin(), deviation.end(), r, [](const auto& d, RingId key) { return d.first < key; });
			std::int64_t stray = it != deviation.end() && it->first == r ? it->second : 0;
			if (!changed(r)) {
				stray = std::max(stray, m.pieceDeviationMm(r));
			}
			assert(kSeedClearance * stray <= kOffGroundReachMm && "PatchableMesh: a ring strays past the reach of the seed query");
			strays.push_back({r, stray});
			return stray;
		};
		auto centroid3 = [&](std::int32_t t) {
			const CdtTriangle& tri = cdt.triangles[static_cast<std::size_t>(t)];
			return cdt.vertices[tri.v[0]] + cdt.vertices[tri.v[1]] + cdt.vertices[tri.v[2]];
		};
		// The rings live after the patch whose boxes come within kOffGroundReachMm of the
		// sample: a ring's box can miss its bent pieces by its stray.
		auto ringsNear = [&](const Vec2i64& sample3) -> const std::vector<RingId>& {
			const Vec2i64		 p{floorDiv(sample3.x, 3), floorDiv(sample3.y, 3)};
			const Vec2i64		 lo = p - Vec2i64{kOffGroundReachMm, kOffGroundReachMm};
			const Vec2i64		 hi = p + Vec2i64{kOffGroundReachMm + 1, kOffGroundReachMm + 1};
			std::vector<RingId>& near = work.ringsNear;
			near.clear();
			m.ringGrid.forEachInBox(lo, hi, 1, [&](std::int32_t r) {
				if (!changed(r)) {
					near.push_back(r);
				}
			});
			for (const auto& [id, c] : changes) {
				if (c.hasNew && boxesOverlap(c.lo, c.hi, lo, hi)) {
					near.push_back(id);
				}
			}
			std::sort(near.begin(), near.end());
			near.erase(std::unique(near.begin(), near.end()), near.end());
			return near;
		};
		// The cover of triangle t's centroid read from the rings live after the patch, or
		// false when the centroid lies within kSeedClearance strays of a ring near it (all
		// of a ring's pieces lie within its stray of its outline, and so does every point
		// whose parity the bend could flip).
		auto exactCover = [&](std::int32_t t, std::vector<RingId>& cover, bool anywhere) {
			const Vec2i64			   sample3 = centroid3(t);
			const std::vector<RingId>& near	   = ringsNear(sample3);
			for (std::size_t i = 0; i < near.size() && !anywhere; ++i) {
				const std::vector<Vec2i64>& ring3 = newSample(near[i]).ring3;
				const std::int64_t			reach = 3 * kSeedClearance * strayOf(near[i]);
				for (std::size_t k = 0; k < ring3.size(); ++k) {
					if (withinDistanceOfSegment(sample3, ring3[k], ring3[(k + 1) % ring3.size()], reach)) {
						return false;
					}
				}
			}
			cover.clear();
			bool ground = false;
			for (RingId r : near) {
				if (isBounds(r)) {
					ground = ground || boundsContainSample(newSample(r), sample3);
				} else if (ringContainsSample(newSample(r), sample3)) {
					cover.push_back(r);
				}
			}
			if (ground) {
				cover.push_back(kGroundToken);
			}
			std::sort(cover.begin(), cover.end());
			return true;
		};
		// The cover of triangle t's centroid read from each near ring's outline as the
		// patch leaves it rounded, even-odd along a ray as the flood toggles. An edge the
		// centroid lies clear of the band of reads as given. Otherwise a blocked edge, or a
		// bounds edge's boundary intervals, read from their pieces: this pass's arrangement's
		// for an edge it arranges (all it crosses is arranged with it), the mesh's for one the
		// patch leaves alone when they run end to end; a blocked edge whose pieces do not
		// (it hangs off ground) joins s instead, and the pass grows. A bounds edge's dissolved
		// stretches are never rounded and read as given, joined to the rounded ends of its
		// intervals by the short step each was rounded by. False when some edge can be read
		// none of these ways.
		std::vector<std::pair<EdgeSource, std::int32_t>>& arrangedPieces = work.arrangedPieces;
		bool											  piecesListed	 = false;
		auto roundedCover = [&](std::int32_t t, std::vector<RingId>& cover) {
			if (!piecesListed) {
				arrangedPieces.clear();
				for (std::size_t ae = 0; ae < arrangement.edges.size(); ++ae) {
					for (std::int64_t local : arrangement.edges[ae].provenance) {
						const LocalSegment& seg = segments[static_cast<std::size_t>(local)];
						if (seg.fixed < 0) {
							arrangedPieces.push_back({seg.source, static_cast<std::int32_t>(ae)});
						}
					}
				}
				std::sort(arrangedPieces.begin(), arrangedPieces.end());
				piecesListed = true;
			}
			const Vec2i64			   sample3 = centroid3(t);
			const std::vector<RingId>& near	   = ringsNear(sample3);
			bool					   inside  = false;
			auto pass = [&](const Vec2i64& a3, const Vec2i64& b3) {
				const std::optional<bool> crosses = rayCrosses(sample3, a3, b3);
				inside							  = inside != crosses.value_or(false);
				return crosses.has_value();
			};
			// The point a + t dir in 3x space, each coordinate times t.den.
			auto scaled3 = [](const Vec2i64& a, const Vec2i64& dir, const EdgeParam& t, bool y) {
				const Int128 v = Int128::product(y ? a.y : a.x, t.den) + Int128::product(t.num, y ? dir.y : dir.x);
				return v + v + v;
			};
			// The exact stretch [t0, t1] of the edge a + t dir.
			auto passStretch = [&](const Vec2i64& a, const Vec2i64& dir, const EdgeParam& t0, const EdgeParam& t1) {
				if (!(Int128::product(t0.num, t1.den) < Int128::product(t1.num, t0.den))) {
					return true;
				}
				const bool above0 = Int128::product(sample3.y, t0.den) < scaled3(a, dir, t0, true);
				const bool above1 = Int128::product(sample3.y, t1.den) < scaled3(a, dir, t1, true);
				const int  side	  = cross(dir, sample3 - a * 3).sign();
				if (above0 == above1) {
					if (side == 0 && dir.y == 0) {
						// The stretch runs along the ray's line: refused if the sample is on it.
						const Int128 sx0 = Int128::product(sample3.x, t0.den);
						const Int128 sx1 = Int128::product(sample3.x, t1.den);
						const Int128 x0	 = scaled3(a, dir, t0, false);
						const Int128 x1	 = scaled3(a, dir, t1, false);
						return (sx0 < x0) == (sx1 < x1) && sx0 != x0 && sx1 != x1;
					}
					return true;
				}
				if (side == 0) {
					return false;
				}
				inside = inside != (dir.y > 0 ? side > 0 : side < 0);
				return true;
			};
			// The step from the exact point at t to where it was rounded, p, read as a box:
			// refused when the sample lies in the box, or the ray's line passes through it
			// with the sample beside it.
			auto passStep = [&](const Vec2i64& a, const Vec2i64& dir, const EdgeParam& t, const Vec2i64& p) {
				const Int128  sy = Int128::product(sample3.y, t.den);
				const Int128  ey = scaled3(a, dir, t, true);
				const Int128  sx = Int128::product(sample3.x, t.den);
				const Int128  ex = scaled3(a, dir, t, false);
				const Vec2i64 p3 = p * 3;
				const bool	  rightOfBoth = sx < ex && sample3.x < p3.x;
				const bool	  leftOfBoth  = ex < sx && p3.x < sample3.x;
				const bool	  yIn		  = !(sy < ey && sample3.y < p3.y) && !(ey < sy && p3.y < sample3.y);
				if (yIn && !rightOfBoth && !leftOfBoth) {
					return false;
				}
				if ((sy < ey) != (sample3.y < p3.y) && rightOfBoth) {
					inside = !inside;
				}
				return true;
			};
			std::vector<std::pair<std::uint32_t, std::uint32_t>>& meshPieces = work.meshPieces;
			std::vector<std::uint32_t>&							   chainEnds  = work.chainEnds;
			cover.clear();
			bool ground = false;
			for (RingId r : near) {
				const std::vector<Vec2i64>& ring3 = newSample(r).ring3;
				const std::int64_t			reach = 3 * kSeedClearance * strayOf(r);
				inside							  = false;
				for (std::int32_t e = 0; e < static_cast<std::int32_t>(ring3.size()); ++e) {
					const Vec2i64& a3 = ring3[static_cast<std::size_t>(e)];
					const Vec2i64& b3 = ring3[(static_cast<std::size_t>(e) + 1) % ring3.size()];
					if (a3 == b3) {
						continue;
					}
					if (!withinDistanceOfSegment(sample3, a3, b3, reach)) {
						pass(a3, b3);
						continue;
					}
					const EdgeSource			x{r, e};
					const std::vector<Vec2i64>& outline = newPolygon(r).ring;
					const Vec2i64				a		= edgeStart(outline, e);
					const Vec2i64				dir		= edgeEnd(outline, e) - a;
					std::vector<Vec2i64>&		ends	= work.runEnds;
					ends.clear();
					if (isBounds(r)) {
						const std::pair<std::uint32_t, std::uint32_t> run = intervalsAfter(x);
						EdgeParam									  prev{0, 1};
						for (std::uint32_t i = run.first; i < run.second; ++i) {
							const GroundInterval& iv = work.intervalPool[i];
							if (!passStretch(a, dir, prev, iv.fromAt) || !passStep(a, dir, iv.fromAt, iv.from) || !passStep(a, dir, iv.toAt, iv.to)) {
								return false;
							}
							prev = iv.toAt;
							for (const Vec2i64& end : {iv.from, iv.to}) {
								auto at = std::find(ends.begin(), ends.end(), end);
								if (at == ends.end()) {
									ends.push_back(end);
								} else {
									ends.erase(at);
								}
							}
						}
						if (!passStretch(a, dir, prev, EdgeParam{1, 1})) {
							return false;
						}
						if (run.first == run.second) {
							continue;
						}
					} else {
						ends.assign({a, a + dir});
					}
					auto it = std::lower_bound(arrangedPieces.begin(), arrangedPieces.end(), x, [](const auto& p, const EdgeSource& key) { return p.first < key; });
					if (it != arrangedPieces.end() && it->first == x) {
						for (; it != arrangedPieces.end() && it->first == x; ++it) {
							const ArrangementEdge& ae = arrangement.edges[static_cast<std::size_t>(it->second)];
							if (!pass(arrangement.vertices[ae.from] * 3, arrangement.vertices[ae.to] * 3)) {
								return false;
							}
						}
						continue;
					}
					if (changed(r) || inS(x)) {
						return false;
					}
					if (!m.piecesRunBetween(x, ends, meshPieces, chainEnds)) {
						addToS(x);
						work.coversGrew = true;
						return false;
					}
					for (const auto& [u, v] : meshPieces) {
						if (!pass(m.verts[u] * 3, m.verts[v] * 3)) {
							return false;
						}
					}
				}
				if (isBounds(r)) {
					ground = ground || inside;
				} else if (inside) {
					cover.push_back(r);
				}
			}
			if (ground) {
				cover.push_back(kGroundToken);
			}
			std::sort(cover.begin(), cover.end());
			return true;
		};

		std::vector<std::uint8_t>& inGroup = work.inGroup;
		inGroup.assign(work.faceCount, 0);
		bool measured = false;
		for (std::int32_t seed : order) {
			if (faces[static_cast<std::size_t>(seed)].assigned || inGroup[static_cast<std::size_t>(seed)] != 0) {
				continue;
			}
			// The faces a flood from this one would reach, and their triangles, largest first.
			std::vector<std::int32_t>& group = work.group;
			group.assign(1, seed);
			inGroup[static_cast<std::size_t>(seed)] = 1;
			std::vector<std::pair<Int128, std::int32_t>>& bySize = work.bySize;
			bySize.clear();
			for (std::size_t i = 0; i < group.size(); ++i) {
				for (std::int32_t t : trisOf(faces[static_cast<std::size_t>(group[i])])) {
					const CdtTriangle& tri = cdt.triangles[static_cast<std::size_t>(t)];
					bySize.push_back({cross(cdt.vertices[tri.v[1]] - cdt.vertices[tri.v[0]], cdt.vertices[tri.v[2]] - cdt.vertices[tri.v[0]]), -t});
					for (int e = 0; e < 3; ++e) {
						const std::int32_t c  = tri.constraint[static_cast<std::size_t>(e)];
						const std::int32_t nb = tri.neighbor[static_cast<std::size_t>(e)];
						if (c < 0 || nb < 0) {
							continue;
						}
						const std::int32_t g	= faceOf[static_cast<std::size_t>(nb)];
						const FaceKind	   kind = faces[static_cast<std::size_t>(g)].kind;
						if (inGroup[static_cast<std::size_t>(g)] == 0 && (kind == FaceKind::Hole || kind == FaceKind::NewGround)) {
							inGroup[static_cast<std::size_t>(g)] = 1;
							group.push_back(g);
						}
					}
				}
			}
			std::make_heap(bySize.begin(), bySize.end());
			std::vector<RingId>& start = work.start;
			std::int32_t		 from  = -1;
			for (auto end = bySize.end(); end != bySize.begin() && from < 0; --end) {
				std::pop_heap(bySize.begin(), end);
				const std::int32_t t = -(end - 1)->second;
				if (!measured) {
					measureDeviation();
					measured = true;
				}
				if (exactCover(t, start, false)) {
					from = t;
				}
			}
			// Every triangle lies in some ring's band: read the rings as rounded instead,
			// largest triangle first (popped whole, so ascending).
			for (auto it = bySize.rbegin(); it != bySize.rend() && from < 0 && !work.coversGrew; ++it) {
				if (roundedCover(-it->second, start)) {
					from = -it->second;
				}
			}
			if (work.coversGrew) {
				return;
			}
			if (from < 0) {
				assert(false && "PatchableMesh: no sample of a group reads its cover exactly");
				out.unreadCovers += 1;
				++m.unreadCovers;
				from = -bySize.back().second;
				exactCover(from, start, true);
			}
			flood(faceOf[static_cast<std::size_t>(from)], start);
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
				const std::vector<RingId>& expected = toggle(m.cover(f.outside), sourcesOf(f), work.across);
				if (!face.assigned || face.cover != expected) {
					addToHole(f.outside);
					grew = true;
				}
			}
		}
		return !grew;
	}

	// The ring edges an edge of the hole arrangement lies on, ascending.
	const std::vector<EdgeSource>& PatchableMesh::Patch::sourcesOf(std::int32_t constraint) {
		std::vector<EdgeSource>& list = work.sources;
		list.clear();
		for (std::int64_t local : arrangement.edges[static_cast<std::size_t>(constraint)].provenance) {
			const LocalSegment& seg = segments[static_cast<std::size_t>(local)];
			if (seg.fixed < 0) {
				list.push_back(seg.source);
			} else {
				const std::span<const EdgeSource> more = sourcesOf(fixed[static_cast<std::size_t>(seg.fixed)]);
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
			return !face.cover.empty() && face.cover.front() == kGroundToken;
		};
		const std::int32_t		  cdtCount = static_cast<std::int32_t>(cdt.triangles.size());
		std::vector<std::uint8_t>& kept	  = work.kept;
		std::vector<std::uint8_t>& faceKept = work.faceKept;
		kept.assign(static_cast<std::size_t>(cdtCount), 0);
		faceKept.assign(work.faceCount, 0);
		for (std::size_t f = 0; f < work.faceCount; ++f) {
			faceKept[f] = keptFace(faces[f]) ? 1 : 0;
		}
		for (std::int32_t t = 0; t < cdtCount; ++t) {
			kept[static_cast<std::size_t>(t)] = faceKept[static_cast<std::size_t>(faceOf[static_cast<std::size_t>(t)])];
		}

		// Vertices: an old one at the same point keeps its id.
		std::vector<std::uint32_t>& meshVertex = work.meshVertex;
		meshVertex.assign(cdt.vertices.size(), UINT32_MAX);
		for (std::int32_t t = 0; t < cdtCount; ++t) {
			if (kept[static_cast<std::size_t>(t)] == 0) {
				continue;
			}
			for (std::uint32_t lv : cdt.triangles[static_cast<std::size_t>(t)].v) {
				if (meshVertex[lv] != UINT32_MAX) {
					continue;
				}
				const Vec2i64& p  = cdt.vertices[lv];
				if (const std::uint32_t* found = m.vertexAt.find(p)) {
					meshVertex[lv] = *found;
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
				m.vertexAt.insert(p, id);
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
		std::vector<std::uint32_t>& oldVertices = work.oldVertices;
		oldVertices.clear();
		std::sort(hole.begin(), hole.end());
		for (std::int32_t t : hole) {
			work.inHole[static_cast<std::size_t>(t)] = 0;
		}
		const std::vector<std::int32_t>& reuse = hole;
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
		std::vector<std::int32_t>& freed   = work.freed;
		std::vector<std::int32_t>& created = work.created;
		freed							   = reuse;
		created.clear();

		// Commit the registry; covers acquired below read the new tags. The hole is gone
		// first: its pieces leave the edge index under the ring edge ids they came in with.
		for (auto& [id, c] : changes) {
			RegisteredRing& reg = m.rings[static_cast<std::size_t>(id)];
			if (reg.live) {
				m.ringGrid.erase(id, reg.lo, reg.hi);
				m.unindexRingEdges(id);
			}
			if (!c.hasNew) {
				reg = {};
				m.freeRings.push_back(id);
				continue;
			}
			m.ringGrid.insert(id, c.lo, c.hi);
			reg.polygon = std::move(c.polygon);
			reg.sample	= std::move(c.sample);
			reg.lo		= c.lo;
			reg.hi		= c.hi;
			reg.live	= true;
			m.indexRingEdges(id);
		}

		// New triangles take the hole's ids first. A face's triangles share one cover and
		// an edge's two sides one source list, each interned once.
		std::vector<std::int32_t>& faceCover	  = work.faceCover;
		std::vector<std::int32_t>& edgeList	  = work.edgeList;
		std::vector<std::int64_t>& edgeProvenance = work.edgeProvenance;
		std::vector<std::int32_t>& newId		  = work.newId;
		faceCover.assign(work.faceCount, -1);
		edgeList.assign(arrangement.edges.size(), -1);
		edgeProvenance.assign(arrangement.edges.size(), kNoProvenance);
		newId.assign(static_cast<std::size_t>(cdtCount), -1);
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
			const std::int32_t f	   = faceOf[static_cast<std::size_t>(t)];
			std::int32_t&	   coverId = faceCover[static_cast<std::size_t>(f)];
			if (coverId < 0) {
				coverId = m.acquireCover(faces[static_cast<std::size_t>(f)].cover);
			} else {
				m.covers.retain(coverId);
			}
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
			created.push_back(id);
		}
		for (std::size_t r = nextReuse; r < reuse.size(); ++r) {
			m.freeTriangles.push_back(reuse[r]);
		}

		// Adjacency and ring sources.
		// The hole's own edges are constrained Delaunay already; only an edge it shares
		// with a kept triangle can need a flip.
		std::vector<std::uint8_t>&				   linked		 = work.linked;
		std::vector<std::pair<std::int32_t, int>>& boundaryEdges = work.boundaryEdges;
		linked.assign(fixed.size(), 0);
		boundaryEdges.clear();
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
					boundaryEdges.push_back({id, e});
				} else {
					assert(c >= 0 && "PatchableMesh: kept ground ends at an unconstrained edge");
				}
				if (c < 0) {
					continue;
				}
				std::int32_t& listId = edgeList[static_cast<std::size_t>(c)];
				if (listId < 0) {
					const std::vector<EdgeSource>& list = sourcesOf(c);
					if (list.empty()) {
						continue;
					}
					std::int64_t provenance = m.rings[static_cast<std::size_t>(list.front().ring)].polygon.provenanceId;
					for (const EdgeSource& z : list) {
						provenance = std::min(provenance, m.rings[static_cast<std::size_t>(z.ring)].polygon.provenanceId);
					}
					edgeProvenance[static_cast<std::size_t>(c)] = provenance;
					listId										= m.sourceLists.acquire(list);
				} else {
					m.sourceLists.retain(listId);
				}
				nt.edgeProvenance[static_cast<std::size_t>(e)] = edgeProvenance[static_cast<std::size_t>(c)];
				m.setEdgeSources(id, e, listId);
			}
		}
		work.unlinked.clear();
		for (std::size_t f = 0; f < fixed.size(); ++f) {
			if (fixed[f].boundary && linked[f] == 0) {
				work.unlinked.push_back(fixed[f].outside);
				m.tris[static_cast<std::size_t>(fixed[f].outside)].neighbor[static_cast<std::size_t>(fixed[f].outsideEdge)] = -1;
				// Ground beyond this edge is gone: widths near it change though no fresh
				// triangle borders it.
				m.pendingWidthSeeds.push_back({fixed[f].outside, m.verts[fixed[f].a], m.verts[fixed[f].b]});
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

		for (std::int32_t id : created) {
			Vec2i64 tLo;
			Vec2i64 tHi;
			m.triangleBox(id, tLo, tHi);
			m.grid.insert(id, tLo, tHi);
			reportBox(id);
		}
		std::vector<std::int32_t>& flipped = work.flipped;
		flipped.clear();
		m.legalize(boundaryEdges, flipped);
		for (std::int32_t t : flipped) {
			reportBox(t);
		}
		freed.insert(freed.end(), flipped.begin(), flipped.end());
		created.insert(created.end(), flipped.begin(), flipped.end());
		for (std::vector<std::int32_t>* list : {&freed, &created}) {
			std::sort(list->begin(), list->end());
			list->erase(std::unique(list->begin(), list->end()), list->end());
		}
		out.lo = lo;
		out.hi = hi;

		// Every touched id with why: kept triangles beside a new or flipped one, or whose
		// edge on the hole now borders nothing, are the border.
		std::vector<ChangedTriangle>& changes = out.changes;
		auto isNew = [&](std::int32_t t) { return std::binary_search(created.begin(), created.end(), t); };
		for (std::int32_t t : freed) {
			changes.push_back({t, kTriangleFreed});
		}
		for (std::int32_t t : created) {
			changes.push_back({t, kTriangleCreated});
			for (const std::int32_t nb : m.tris[static_cast<std::size_t>(t)].neighbor) {
				if (nb >= 0 && !isNew(nb)) {
					changes.push_back({nb, kTriangleBorder});
				}
			}
		}
		for (std::int32_t t : flipped) {
			changes.push_back({t, kTriangleFlipped});
		}
		for (std::int32_t t : work.unlinked) {
			if (!isNew(t)) {
				changes.push_back({t, kTriangleBorder});
			}
		}
		std::sort(changes.begin(), changes.end(), [](const ChangedTriangle& a, const ChangedTriangle& b) { return a.id < b.id; });
		std::size_t unique = 0;
		for (std::size_t i = 0; i < changes.size(); ++i) {
			if (unique > 0 && changes[unique - 1].id == changes[i].id) {
				changes[unique - 1].why |= changes[i].why;
			} else {
				changes[unique++] = changes[i];
			}
		}
		changes.resize(unique);

		// A flip moves edges between triangles, so door tags are set afresh on every new
		// and flipped triangle; their widths, and widths near what changed, wait for
		// refreshWidths.
		for (std::int32_t t : created) {
			m.applyDoorTags(t);
		}
		m.freshWidthTriangles.insert(m.freshWidthTriangles.end(), created.begin(), created.end());
	}

	// --- Public edits ---------------------------------------------------------------

	std::vector<Vec2i64> splitLongEdges(const std::vector<Vec2i64>& outline, std::int64_t maxEdgeMm) {
		if (maxEdgeMm <= 0) {
			return outline;
		}
		std::vector<Vec2i64> out;
		for (std::size_t i = 0; i < outline.size(); ++i) {
			const Vec2i64& a = outline[i];
			const Vec2i64& b = outline[(i + 1) % outline.size()];
			out.push_back(a);
			const Vec2i64 d = b - a;
			const Int128  length2 = dot(d, d);
			std::int64_t  pieces  = 1;
			while (Int128::product(pieces * maxEdgeMm, pieces * maxEdgeMm) < length2) {
				++pieces;
			}
			const std::int64_t steps = std::gcd(d.x < 0 ? -d.x : d.x, d.y < 0 ? -d.y : d.y);
			if (steps == 0) {
				continue;
			}
			// Cuts are measured from the lower end, so a side two rings walk in opposite
			// directions is cut at the same points.
			const bool	  fromA = a < b;
			const Vec2i64 step{d.x / steps, d.y / steps};
			std::int64_t  previous = 0;
			for (std::int64_t k = 1; k < pieces; ++k) {
				const std::int64_t at = fromA ? k * steps / pieces : steps - (pieces - k) * steps / pieces;
				if (at > previous && at < steps) {
					out.push_back(a + step * at);
					previous = at;
				}
			}
		}
		return out;
	}

	PatchableMesh::PatchableMesh(const PatchableMeshArgs& args)
		: maxBoundsEdgeMm(args.maxBoundsEdgeMm), maxClearanceMm(args.maxClearanceMm), labels({.openTags = args.componentOpenTags}) {}

	namespace {

		// A solid ring with a water or obstacle tag can only close ground: whatever it
		// covers it blocks, and the smallest solid ring decides a face, so a smaller one
		// already inside keeps its faces.
		bool onlyCloses(const NavInputPolygon& ring) {
			return ring.blocked && !ring.holeCapable && ring.openingId == kNoOpening && ring.provenanceId < 0 && ring.provenanceId != kNoProvenance;
		}

	} // namespace

	PatchReport PatchableMesh::applyPatch(const PatchEdits& edits) {
		Patch patch(*this, edits);
		patchEpoch = nextMeshGeneration();
		PatchReport report = patch.run();
		report.opens	   = !edits.removes.empty() || !edits.moves.empty() || !std::all_of(edits.adds.begin(), edits.adds.end(), onlyCloses);
		labels.update(view(), report);
		return report;
	}

	// --- Doors, widths, and the view ------------------------------------------------

	std::pair<Vec2i64, Vec2i64> PatchableMesh::edgeKey(Vec2i64 a, Vec2i64 b) {
		if (b < a) {
			std::swap(a, b);
		}
		return {a, b};
	}

	void PatchableMesh::applyDoorTags(std::int32_t t) {
		NavTriangle& tri = tris[static_cast<std::size_t>(t)];
		for (int e = 0; e < 3; ++e) {
			auto it = doorsAt.find(edgeKey(verts[tri.v[static_cast<std::size_t>(e)]], verts[tri.v[static_cast<std::size_t>(next3(e))]]));
			const bool tagged						 = it != doorsAt.end();
			tri.edgeOpening[static_cast<std::size_t>(e)] = tagged ? doors[static_cast<std::size_t>(it->second.front())].openingId : kNoOpening;
			tri.edgeClearWidthMm[static_cast<std::size_t>(e)] =
				tagged ? doors[static_cast<std::size_t>(it->second.front())].clearWidthMm : kUnconstrainedWidth;
		}
	}

	// The triangles on edge (a, b), if it is a mesh edge, take its tags again.
	void PatchableMesh::retagEdge(const Vec2i64& a, const Vec2i64& b) {
		const std::uint32_t* va = vertexAt.find(a);
		const std::uint32_t* vb = vertexAt.find(b);
		if (va == nullptr || vb == nullptr) {
			return;
		}
		std::vector<std::int32_t> onEdge;
		forEachAroundVertex(*va, -1, [&](std::int32_t t, int i) {
			const NavTriangle& tri = tris[static_cast<std::size_t>(t)];
			if (tri.v[static_cast<std::size_t>(next3(i))] == *vb || tri.v[static_cast<std::size_t>(prev3(i))] == *vb) {
				onEdge.push_back(t);
			}
		});
		for (std::int32_t t : onEdge) {
			applyDoorTags(t);
		}
	}

	DoorId PatchableMesh::addDoor(const DoorPortal& door) {
		DoorId id = 0;
		if (!freeDoors.empty()) {
			id = freeDoors.back();
			freeDoors.pop_back();
			doors[static_cast<std::size_t>(id)]	   = door;
			doorLive[static_cast<std::size_t>(id)] = 1;
		} else {
			id = static_cast<DoorId>(doors.size());
			doors.push_back(door);
			doorLive.push_back(1);
		}
		std::vector<DoorId>& onEdge = doorsAt[edgeKey(door.a, door.b)];
		onEdge.insert(std::lower_bound(onEdge.begin(), onEdge.end(), id), id);
		retagEdge(door.a, door.b);
		patchEpoch = nextMeshGeneration();
		labels.noteOpening();
		return id;
	}

	void PatchableMesh::removeDoor(DoorId id) {
		assert(doorLive[static_cast<std::size_t>(id)] != 0 && "PatchableMesh: removing a door twice");
		if (id < 0 || static_cast<std::size_t>(id) >= doors.size() || doorLive[static_cast<std::size_t>(id)] == 0) {
			return;
		}
		const DoorPortal	 door	= doors[static_cast<std::size_t>(id)];
		auto				 it		= doorsAt.find(edgeKey(door.a, door.b));
		std::vector<DoorId>& onEdge = it->second;
		onEdge.erase(std::find(onEdge.begin(), onEdge.end(), id));
		if (onEdge.empty()) {
			doorsAt.erase(it);
		}
		doorLive[static_cast<std::size_t>(id)] = 0;
		freeDoors.push_back(id);
		retagEdge(door.a, door.b);
		patchEpoch = nextMeshGeneration();
	}

	namespace {

		struct Vec2dW {
			double x = 0.0;
			double y = 0.0;
		};

		double cross2(const Vec2dW& o, const Vec2dW& a, const Vec2dW& b) {
			return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
		}

		double pointToSegment(const Vec2dW& p, const Vec2dW& a, const Vec2dW& b) {
			const double dx	  = b.x - a.x;
			const double dy	  = b.y - a.y;
			const double len2 = dx * dx + dy * dy;
			double		 u	  = len2 > 0.0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.0;
			u				  = std::clamp(u, 0.0, 1.0);
			return std::hypot(p.x - (a.x + dx * u), p.y - (a.y + dy * u));
		}

		bool segmentsCross(const Vec2dW& a, const Vec2dW& b, const Vec2dW& c, const Vec2dW& d) {
			const double d1 = cross2(c, d, a);
			const double d2 = cross2(c, d, b);
			const double d3 = cross2(a, b, c);
			const double d4 = cross2(a, b, d);
			return ((d1 <= 0.0 && d2 >= 0.0) || (d1 >= 0.0 && d2 <= 0.0)) && ((d3 <= 0.0 && d4 >= 0.0) || (d3 >= 0.0 && d4 <= 0.0));
		}

		// The least distance from segment [a, b] to the closed triangle, 0 where they meet.
		double segmentToTriangle(const Vec2dW& a, const Vec2dW& b, const std::array<Vec2dW, 3>& t) {
			const double area = cross2(t[0], t[1], t[2]);
			auto		 inside = [&](const Vec2dW& p) {
				const double s0 = cross2(t[0], t[1], p);
				const double s1 = cross2(t[1], t[2], p);
				const double s2 = cross2(t[2], t[0], p);
				return area >= 0.0 ? (s0 >= 0.0 && s1 >= 0.0 && s2 >= 0.0) : (s0 <= 0.0 && s1 <= 0.0 && s2 <= 0.0);
			};
			if (inside(a) || inside(b)) {
				return 0.0;
			}
			double least = std::numeric_limits<double>::infinity();
			for (int k = 0; k < 3; ++k) {
				const Vec2dW& p = t[static_cast<std::size_t>(k)];
				const Vec2dW& q = t[static_cast<std::size_t>((k + 1) % 3)];
				if (segmentsCross(a, b, p, q)) {
					return 0.0;
				}
				least = std::min({least, pointToSegment(p, a, b), pointToSegment(a, p, q), pointToSegment(b, p, q)});
			}
			return least;
		}

	} // namespace

	// A patch changes the mesh inside its hole and the triangles legalize flips: the
	// triangles it leaves there (the fresh ones) get their widths from scratch. An
	// unchanged apex C, its width w (read as maxClearanceMm when unconstrained), stays
	// right unless its search can reach what changed: the search reads only triangles
	// joined to C within the disc of radius w round C (corridorWidthMm reads nothing
	// farther than maxClearanceMm), so a changed point P it reaches lies within w of C,
	// on an edge where unchanged ground meets the changed region (a seed edge: a fresh
	// triangle's edge to an unchanged one, or an edge whose far side the patch removed),
	// and every triangle on the way lies within 2w of P. So the triangles within twice
	// the clearance cap of a seed edge, found by walking adjacency out from the seeds,
	// hold every apex to search again: those within their own width of a seed edge. The
	// walk covers what changed and its margin, whatever the size of the mesh.
	PatchReport PatchableMesh::refreshWidths() {
		std::vector<std::int32_t>& stale = staleApexes;
		stale.clear();
		freshMark.growTo(tris.size());
		walkMark.growTo(tris.size());
		if (++freshStamp == 0) {
			freshMark.zero();
			freshStamp = 1;
		}
		if (++walkStamp == 0) {
			walkMark.zero();
			walkStamp = 1;
		}
		auto isFresh = [&](std::int32_t t) { return freshMark[static_cast<std::size_t>(t)] == freshStamp; };
		for (std::int32_t t : freshWidthTriangles) {
			if (live[static_cast<std::size_t>(t)] != 0) {
				freshMark[static_cast<std::size_t>(t)] = freshStamp;
			}
		}
		std::vector<WidthSeed>& seeds = widthSeeds;
		seeds.clear();
		for (std::int32_t t : freshWidthTriangles) {
			if (live[static_cast<std::size_t>(t)] == 0) {
				continue;
			}
			stale.insert(stale.end(), {3 * t, 3 * t + 1, 3 * t + 2});
			const NavTriangle& tri = tris[static_cast<std::size_t>(t)];
			for (int e = 0; e < 3; ++e) {
				const std::int32_t nb = tri.neighbor[static_cast<std::size_t>(e)];
				if (nb >= 0 && !isFresh(nb)) {
					seeds.push_back({nb, verts[tri.v[static_cast<std::size_t>(e)]], verts[tri.v[static_cast<std::size_t>(next3(e))]]});
				}
			}
		}
		for (const WidthSeed& s : pendingWidthSeeds) {
			if (live[static_cast<std::size_t>(s.tri)] != 0 && !isFresh(s.tri)) {
				seeds.push_back(s);
			}
		}

		// The seed edges bucketed on a grid of the walk's reach, each in every cell its box
		// overlaps, as (cell, seed) sorted by cell.
		const double	   walkReach = 2.0 * static_cast<double>(maxClearanceMm) + 4.0;
		const std::int64_t cell		 = 2 * maxClearanceMm + 4;
		auto			   cellKey	 = [](std::int64_t cx, std::int64_t cy) { return (static_cast<std::uint64_t>(cx) << 32) ^ (static_cast<std::uint64_t>(cy) & 0xFFFFFFFFULL); };
		std::vector<std::pair<std::uint64_t, std::int32_t>>& buckets = seedBuckets;
		buckets.clear();
		for (std::int32_t i = 0; i < static_cast<std::int32_t>(seeds.size()); ++i) {
			const WidthSeed& s = seeds[static_cast<std::size_t>(i)];
			for (std::int64_t cy = floorDiv(std::min(s.a.y, s.b.y), cell); cy <= floorDiv(std::max(s.a.y, s.b.y), cell); ++cy) {
				for (std::int64_t cx = floorDiv(std::min(s.a.x, s.b.x), cell); cx <= floorDiv(std::max(s.a.x, s.b.x), cell); ++cx) {
					buckets.push_back({cellKey(cx, cy), i});
				}
			}
		}
		std::sort(buckets.begin(), buckets.end());
		// Each occupied cell's run of the sorted buckets, found by an open-addressed table.
		std::vector<std::int32_t>& runs = seedCellRuns; // begin index of each run, then buckets.size()
		runs.clear();
		for (std::size_t i = 0; i < buckets.size(); ++i) {
			if (i == 0 || buckets[i].first != buckets[i - 1].first) {
				runs.push_back(static_cast<std::int32_t>(i));
			}
		}
		std::vector<std::int32_t>& table = seedCellTable; // run index, -1 empty
		std::size_t				   size	 = 16;
		while (size < 2 * runs.size()) {
			size *= 2;
		}
		table.assign(size, -1);
		auto slotOf = [&](std::uint64_t key) { return static_cast<std::size_t>((key * 0x9E3779B97F4A7C15ULL) >> 32) & (size - 1); };
		for (std::size_t k = 0; k < runs.size(); ++k) {
			std::size_t i = slotOf(buckets[static_cast<std::size_t>(runs[k])].first);
			while (table[i] >= 0) {
				i = (i + 1) & (size - 1);
			}
			table[i] = static_cast<std::int32_t>(k);
		}
		runs.push_back(static_cast<std::int32_t>(buckets.size()));
		auto toD = [](const Vec2i64& p) { return Vec2dW{static_cast<double>(p.x), static_cast<double>(p.y)}; };
		// Visits each seed whose box comes within `margin` of the box [lo, hi], until `visit`
		// returns true: through the cells the box covers, or every seed when they are
		// fewer than those cells.
		auto forSeedsNear = [&](const Vec2i64& lo, const Vec2i64& hi, std::int64_t margin, auto visit) {
			auto boxNear = [&](const WidthSeed& s) {
				return std::min(s.a.x, s.b.x) - margin <= hi.x && lo.x <= std::max(s.a.x, s.b.x) + margin && std::min(s.a.y, s.b.y) - margin <= hi.y &&
					   lo.y <= std::max(s.a.y, s.b.y) + margin;
			};
			const std::int64_t x0 = floorDiv(lo.x - margin, cell);
			const std::int64_t x1 = floorDiv(hi.x + margin, cell);
			const std::int64_t y0 = floorDiv(lo.y - margin, cell);
			const std::int64_t y1 = floorDiv(hi.y + margin, cell);
			if (static_cast<double>(x1 - x0 + 1) * static_cast<double>(y1 - y0 + 1) >= static_cast<double>(seeds.size())) {
				for (const WidthSeed& s : seeds) {
					if (boxNear(s) && visit(s)) {
						return true;
					}
				}
				return false;
			}
			for (std::int64_t cy = y0; cy <= y1; ++cy) {
				for (std::int64_t cx = x0; cx <= x1; ++cx) {
					const std::uint64_t key = cellKey(cx, cy);
					for (std::size_t i = slotOf(key); table[i] >= 0; i = (i + 1) & (size - 1)) {
						const std::size_t k = static_cast<std::size_t>(table[i]);
						if (buckets[static_cast<std::size_t>(runs[k])].first != key) {
							continue;
						}
						for (std::int32_t j = runs[k]; j < runs[k + 1]; ++j) {
							const WidthSeed& s = seeds[static_cast<std::size_t>(buckets[static_cast<std::size_t>(j)].second)];
							if (boxNear(s) && visit(s)) {
								return true;
							}
						}
						break;
					}
				}
			}
			return false;
		};

		std::vector<std::int32_t>& queue = widthWalk;
		queue.clear();
		for (const WidthSeed& s : seeds) {
			if (walkMark[static_cast<std::size_t>(s.tri)] != walkStamp) {
				walkMark[static_cast<std::size_t>(s.tri)] = walkStamp;
				queue.push_back(s.tri);
			}
		}
		for (std::size_t head = 0; head < queue.size(); ++head) {
			const std::int32_t	   t   = queue[head];
			const NavTriangle&	   tri = tris[static_cast<std::size_t>(t)];
			std::array<Vec2dW, 3> pts{toD(verts[tri.v[0]]), toD(verts[tri.v[1]]), toD(verts[tri.v[2]])};
			Vec2i64				   lo  = verts[tri.v[0]];
			Vec2i64				   hi  = lo;
			growBox(lo, hi, verts[tri.v[1]]);
			growBox(lo, hi, verts[tri.v[2]]);
			const bool near = forSeedsNear(lo, hi, cell, [&](const WidthSeed& s) { return segmentToTriangle(toD(s.a), toD(s.b), pts) <= walkReach; });
			if (!near) {
				continue;
			}
			if (!isFresh(t)) {
				for (int apex = 0; apex < 3; ++apex) {
					const Vec2i64& c = verts[tri.v[static_cast<std::size_t>(apex)]];
					// The stored width is floored: the obstacle can lie up to a millimetre farther.
					const double reachMm = static_cast<double>(std::min(tri.edgePairWidthMm[static_cast<std::size_t>(apex)], maxClearanceMm)) + 2.0;
					if (forSeedsNear(c, c, static_cast<std::int64_t>(reachMm) + 1, [&](const WidthSeed& s) { return pointToSegment(toD(c), toD(s.a), toD(s.b)) < reachMm; })) {
						stale.push_back(3 * t + apex);
					}
				}
			}
			for (const std::int32_t nb : tri.neighbor) {
				if (nb >= 0 && walkMark[static_cast<std::size_t>(nb)] != walkStamp) {
					walkMark[static_cast<std::size_t>(nb)] = walkStamp;
					queue.push_back(nb);
				}
			}
		}

		std::sort(stale.begin(), stale.end());
		stale.erase(std::unique(stale.begin(), stale.end()), stale.end());
		const NavView meshView = view();
		PatchReport	  report;
		for (std::int32_t a : stale) {
			const std::int64_t w	 = corridorWidthMm(meshView, widthSearch, a / 3, a % 3, maxClearanceMm);
			std::int64_t&	   width = tris[static_cast<std::size_t>(a / 3)].edgePairWidthMm[static_cast<std::size_t>(a % 3)];
			if (width == w) {
				continue;
			}
			width = w;
			if (report.changes.empty() || report.changes.back().id != a / 3) {
				Vec2i64 tLo;
				Vec2i64 tHi;
				triangleBox(a / 3, tLo, tHi);
				if (report.changes.empty()) {
					report.lo = tLo;
					report.hi = tHi;
				} else {
					growBox(report.lo, report.hi, tLo);
					growBox(report.lo, report.hi, tHi);
				}
				report.changes.push_back({a / 3, kTriangleWidths});
			}
		}
		freshWidthTriangles.clear();
		pendingWidthSeeds.clear();
		return report;
	}

	NavView PatchableMesh::view() const {
		NavView v;
		v.vertices.of(verts);
		v.triangles.of(tris);
		v.live.of(live);
		v.owner			  = this;
		v.locateIn		  = [](const void* owner, const Vec2i64& p) { return static_cast<const PatchableMesh*>(owner)->locate(p); };
		v.componentsIn	  = [](const void* owner) -> const MeshComponents* { return &static_cast<const PatchableMesh*>(owner)->labels; };
		v.widthsCurrentIn = [](const void* owner) { return static_cast<const PatchableMesh*>(owner)->widthsCurrent(); };
		v.epochIn		  = [](const void* owner) { return static_cast<const PatchableMesh*>(owner)->epoch(); };
		v.resolveIn		  = [](const void* owner, NavView& view) {
			const PatchableMesh& mesh = *static_cast<const PatchableMesh*>(owner);
			view.vertices.of(mesh.verts);
			view.triangles.of(mesh.tris);
			view.live.of(mesh.live);
		};
		return v;
	}

	// --- Validator -------------------------------------------------------------------

	// The farthest any vertex of this ring's pieces on the mesh lies from the line of
	// the original edge it is on, rounded up to a millimetre.
	std::int64_t PatchableMesh::pieceDeviationMm(RingId ring) const {
		const std::vector<Vec2i64>& outline = rings[static_cast<std::size_t>(ring)].polygon.ring;
		std::int64_t				worst	= 0;
		for (std::int32_t e = 0; e < static_cast<std::int32_t>(outline.size()); ++e) {
			const Vec2i64 a	   = edgeStart(outline, e);
			const Vec2i64 dir  = edgeEnd(outline, e) - a;
			const Int128  len2 = dot(dir, dir);
			for (std::int32_t h : piecesOf({ring, e})) {
				const NavTriangle& tri = tris[static_cast<std::size_t>(h / 3)];
				for (const std::uint32_t v : {tri.v[static_cast<std::size_t>(h % 3)], tri.v[static_cast<std::size_t>(next3(h % 3))]}) {
					worst = std::max(worst, offLineMm(a, dir, len2, verts[v]));
				}
			}
		}
		return worst;
	}

	std::string PatchableMesh::validate() const {
		std::ostringstream err;
		auto			   fail = [&]() -> std::ostringstream& { return err; };
		auto			   failed = [&]() { return !err.str().empty(); };

		const std::int32_t triCount = static_cast<std::int32_t>(tris.size());

		if (unreadCovers > 0) {
			fail() << unreadCovers << " cover(s) seeded from a sample no exact test could read";
			return err.str();
		}

		// Free lists and counts.
		{
			std::vector<std::uint8_t> freedRing(rings.size(), 0);
			for (RingId r : freeRings) {
				if (r < 0 || static_cast<std::size_t>(r) >= rings.size() || rings[static_cast<std::size_t>(r)].live || freedRing[static_cast<std::size_t>(r)] != 0) {
					fail() << "free ring list holds " << r;
					return err.str();
				}
				freedRing[static_cast<std::size_t>(r)] = 1;
			}
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
					const std::uint32_t* at = vertexAt.find(verts[v]);
					if (at == nullptr || *at != v) {
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
		auto isBounds = [&](RingId r) { return !rings[static_cast<std::size_t>(r)].polygon.blocked; };
		std::vector<std::int32_t> sourceRefs(static_cast<std::size_t>(sourceLists.slots()), 0);
		std::size_t				  halfEdgeCount = 0;
		std::vector<RingId>		  flips;
		std::vector<RingId>		  across;
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
			if (ringSet.empty() || ringSet.front() != kGroundToken) {
				fail() << "triangle " << t << " is not ground";
				break;
			}
			std::vector<const SampleRing*> containing;
			for (std::size_t i = 1; i < ringSet.size(); ++i) {
				const RingId r = ringSet[i];
				if (r < 0 || r >= static_cast<RingId>(rings.size()) || !rings[static_cast<std::size_t>(r)].live) {
					fail() << "triangle " << t << " covered by dead ring " << r;
					break;
				}
				if (!rings[static_cast<std::size_t>(r)].polygon.blocked) {
					fail() << "triangle " << t << " cover names bounds ring " << r;
					break;
				}
				containing.push_back(&rings[static_cast<std::size_t>(r)].sample);
			}
			if (failed()) {
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
						if (z.ring < 0 || z.ring >= static_cast<RingId>(rings.size()) || !rings[static_cast<std::size_t>(z.ring)].live) {
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
						const std::vector<std::int32_t>& under = piecesOf(z);
						if (std::find(under.begin(), under.end(), t * 3 + e) == under.end()) {
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
				} else if (toggledInto(ringSet, sourceLists.get(sourceId), isBounds, flips, across), across != other) {
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
		for (const std::vector<std::int32_t>& list : pieces) {
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
		std::size_t liveRings = 0;
		for (RingId r = 0; r < static_cast<RingId>(rings.size()); ++r) {
			const RegisteredRing& reg = rings[static_cast<std::size_t>(r)];
			if (!reg.live) {
				continue;
			}
			++liveRings;
			const std::vector<RingId> found = ringsInBox(reg.lo, reg.hi);
			if (!std::binary_search(found.begin(), found.end(), r)) {
				fail() << "ring index misses ring " << r;
				return err.str();
			}
			if (reg.edgeIds.size() != reg.polygon.ring.size()) {
				fail() << "ring " << r << " has " << reg.edgeIds.size() << " edge ids for " << reg.polygon.ring.size() << " edges";
				return err.str();
			}
			for (std::int32_t e = 0; e < static_cast<std::int32_t>(reg.edgeIds.size()); ++e) {
				const std::int32_t id = reg.edgeIds[static_cast<std::size_t>(e)];
				if (id < 0 || static_cast<std::size_t>(id) >= edgeOf.size() || !(edgeOf[static_cast<std::size_t>(id)] == EdgeSource{r, e})) {
					fail() << "ring " << r << " edge " << e << " has a stale edge id " << id;
					return err.str();
				}
				std::vector<EdgeSource> near;
				std::vector<std::int32_t> ids;
				Vec2i64 lo = edgeStart(reg.polygon.ring, e);
				Vec2i64 hi = lo;
				growBox(lo, hi, edgeEnd(reg.polygon.ring, e));
				ringEdgesInBox(lo, hi, ids, near);
				if (!std::binary_search(near.begin(), near.end(), EdgeSource{r, e})) {
					fail() << "edge index misses ring " << r << " edge " << e;
					return err.str();
				}
			}
		}
		std::vector<std::uint8_t> edgeFree(edgeOf.size(), 0);
		for (std::int32_t id : freeEdgeIds) {
			if (id < 0 || static_cast<std::size_t>(id) >= edgeOf.size() || edgeFree[static_cast<std::size_t>(id)] != 0) {
				return "free edge id list holds a bad or repeated id";
			}
			edgeFree[static_cast<std::size_t>(id)] = 1;
		}
		std::size_t indexedEdges = 0;
		bool		deadEdge	 = false;
		edgeGrid.forEachEntry([&](std::int32_t id) {
			++indexedEdges;
			deadEdge = deadEdge || id < 0 || static_cast<std::size_t>(id) >= edgeOf.size() || edgeFree[static_cast<std::size_t>(id)] != 0;
		});
		if (deadEdge || indexedEdges + freeEdgeIds.size() < edgeOf.size()) {
			return "edge index holds a freed edge id, or misses one";
		}
		std::size_t indexedRings = 0;
		bool		deadRing	 = false;
		ringGrid.forEachEntry([&](std::int32_t r) {
			++indexedRings;
			deadRing = deadRing || r < 0 || r >= static_cast<RingId>(rings.size()) || !rings[static_cast<std::size_t>(r)].live;
		});
		if (deadRing || indexedRings < liveRings) {
			return "ring index holds a dead ring";
		}

		// Each bounds edge's pieces lie along its boundary intervals over the live bounds
		// rings: none where it is dissolved, a chain with no fork, and every interval end on
		// ground a vertex (so a bounds ring's corner on ground is a vertex where an interval
		// ends there). A chain stops short only where snap rounding laid two bounds edges'
		// pieces on one segment, whose ground toggles cancel. A piece with an odd number of
		// bounds edges on it is boundary: ground on one side only.
		for (std::int32_t t = 0; t < triCount; ++t) {
			if (live[static_cast<std::size_t>(t)] == 0) {
				continue;
			}
			for (int e = 0; e < 3; ++e) {
				int groundSources = 0;
				for (const EdgeSource& z : edgeSources(t, e)) {
					groundSources += rings[static_cast<std::size_t>(z.ring)].polygon.blocked ? 0 : 1;
				}
				if (groundSources % 2 == 1 && tris[static_cast<std::size_t>(t)].neighbor[static_cast<std::size_t>(e)] >= 0) {
					fail() << "triangle " << t << " edge " << e << " is a boundary piece with ground on both sides";
					return err.str();
				}
			}
		}
		{
			std::vector<GroundRing>				  groundNear;
			GroundBoundaryScratch				  boundaryScratch;
			std::vector<GroundInterval>			  intervals;
			std::map<std::uint32_t, int>		  degree;
			for (RingId r = 0; r < static_cast<RingId>(rings.size()); ++r) {
				const RegisteredRing& reg = rings[static_cast<std::size_t>(r)];
				if (!reg.live || reg.polygon.blocked) {
					continue;
				}
				for (std::int32_t e = 0; e < static_cast<std::int32_t>(reg.polygon.ring.size()); ++e) {
					Vec2i64 lo = edgeStart(reg.polygon.ring, e);
					Vec2i64 hi = lo;
					growBox(lo, hi, edgeEnd(reg.polygon.ring, e));
					groundRingsInBox(lo, hi, groundNear);
					groundBoundaries({&reg.polygon.ring, reg.lo, reg.hi, reg.sample.counterClockwise, r}, e, groundNear, boundaryScratch, intervals);
					if (intervals.empty()) {
						if (!piecesOf({r, e}).empty()) {
							fail() << "ring " << r << " edge " << e << " has pieces outside its boundary intervals";
							return err.str();
						}
						continue;
					}
					degree.clear();
					std::set<std::pair<std::uint32_t, std::uint32_t>> seen;
					for (std::int32_t h : piecesOf({r, e})) {
						const NavTriangle&	tri = tris[static_cast<std::size_t>(h / 3)];
						const std::uint32_t p	= tri.v[static_cast<std::size_t>(h % 3)];
						const std::uint32_t q	= tri.v[static_cast<std::size_t>(next3(h % 3))];
						if (seen.insert(std::minmax(p, q)).second) {
							++degree[p];
							++degree[q];
						}
					}
					for (const auto& [v, count] : degree) {
						if (count > 2) {
							fail() << "ring " << r << " edge " << e << " forks at vertex " << v;
							return err.str();
						}
					}
					for (const GroundInterval& iv : intervals) {
						for (const Vec2i64& end : {iv.from, iv.to}) {
							if (locate(end) >= 0 && vertexAt.find(end) == nullptr) {
								fail() << "ring " << r << " edge " << e << " interval end (" << end.x << ", " << end.y << ") is on ground but not a vertex";
								return err.str();
							}
						}
					}
				}
			}
		}
		// Every live vertex ends a constrained edge.
		{
			std::vector<std::uint8_t> constrainedEnd(verts.size(), 0);
			for (std::int32_t t = 0; t < triCount; ++t) {
				if (live[static_cast<std::size_t>(t)] == 0) {
					continue;
				}
				const NavTriangle& tri = tris[static_cast<std::size_t>(t)];
				for (int e = 0; e < 3; ++e) {
					if (edgeSourceIds[static_cast<std::size_t>(t)][static_cast<std::size_t>(e)] >= 0) {
						constrainedEnd[tri.v[static_cast<std::size_t>(e)]]		  = 1;
						constrainedEnd[tri.v[static_cast<std::size_t>(next3(e))]] = 1;
					}
				}
			}
			for (std::size_t v = 0; v < verts.size(); ++v) {
				if (vertexUses[v] == 0) {
					continue;
				}
				if (constrainedEnd[v] == 0) {
					fail() << "vertex " << v << " (" << verts[v].x << ", " << verts[v].y << ") ends no constrained edge";
					return err.str();
				}
			}
		}
		// Each blocked ring edge is a chain of pieces: no fork, and an end that is not an
		// original corner is on the mesh boundary.
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
		std::vector<EdgeSource> onMesh; // ascending, so the first violation reported is the same everywhere
		for (RingId r = 0; r < static_cast<RingId>(rings.size()); ++r) {
			if (!rings[static_cast<std::size_t>(r)].live || !rings[static_cast<std::size_t>(r)].polygon.blocked) {
				continue;
			}
			for (std::int32_t e = 0; e < static_cast<std::int32_t>(rings[static_cast<std::size_t>(r)].polygon.ring.size()); ++e) {
				if (!piecesOf({r, e}).empty()) {
					onMesh.push_back({r, e});
				}
			}
		}
		for (const EdgeSource& z : onMesh) {
			const std::vector<std::int32_t>& list = piecesOf(z);
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
		// A vertex inside the mesh splits a chain of blocked edges only where something else
		// meets it: a corner of one of them, or another segment, which keeps pieces of its own
		// there unless rounding laid them on the chain's either side, so it is in the source
		// list. Every split, exact or rounded, is two segments meeting, and both keep pieces at
		// it unless it is on the mesh boundary. So a vertex with just two constrained edges,
		// one source list on both, blocked edges only, at no corner of theirs, and no two of
		// them crossing within kOffGroundReachMm of it, is a split left over.
		{
			std::map<std::uint32_t, std::vector<std::int32_t>> constrainedAt; // source list ids
			for (std::int32_t t = 0; t < triCount; ++t) {
				if (live[static_cast<std::size_t>(t)] == 0) {
					continue;
				}
				const NavTriangle& tri = tris[static_cast<std::size_t>(t)];
				for (int e = 0; e < 3; ++e) {
					const std::uint32_t a		 = tri.v[static_cast<std::size_t>(e)];
					const std::uint32_t b		 = tri.v[static_cast<std::size_t>(next3(e))];
					const std::int32_t	sourceId = edgeSourceIds[static_cast<std::size_t>(t)][static_cast<std::size_t>(e)];
					// An edge at a vertex off the boundary has a triangle on each side, so it is
					// seen twice; a < b takes it once.
					if (sourceId < 0 || a > b) {
						continue;
					}
					for (const std::uint32_t v : {a, b}) {
						if (onBoundary[v] == 0) {
							constrainedAt[v].push_back(sourceId);
						}
					}
				}
			}
			for (const auto& [v, sourceIds] : constrainedAt) {
				if (sourceIds.size() != 2 || sourceIds[0] != sourceIds[1]) {
					continue;
				}
				const std::vector<EdgeSource>& sources	 = sourceLists.get(sourceIds[0]);
				bool						   explained = false;
				for (const EdgeSource& z : sources) {
					const RegisteredRing& reg = rings[static_cast<std::size_t>(z.ring)];
					explained = explained || !reg.polygon.blocked || isOriginalEndpoint(reg.polygon.ring, z.edge, verts[v]);
				}
				// Two edges crossing there whose pieces rounding laid on one line either side.
				for (std::size_t i = 0; i < sources.size() && !explained; ++i) {
					const std::vector<Vec2i64>& a = rings[static_cast<std::size_t>(sources[i].ring)].polygon.ring;
					for (std::size_t j = i + 1; j < sources.size() && !explained; ++j) {
						const std::vector<Vec2i64>& b = rings[static_cast<std::size_t>(sources[j].ring)].polygon.ring;
						const SegmentIntersection	r = intersectSegments(edgeStart(a, sources[i].edge), edgeEnd(a, sources[i].edge), edgeStart(b, sources[j].edge),
																		  edgeEnd(b, sources[j].edge));
						explained = (r.relation == SegmentRelation::ProperCrossing || r.relation == SegmentRelation::EndpointTouch) &&
									std::abs(r.point.x - verts[v].x) <= kOffGroundReachMm && std::abs(r.point.y - verts[v].y) <= kOffGroundReachMm;
					}
				}
				if (!explained) {
					fail() << "vertex " << v << " (" << verts[v].x << ", " << verts[v].y << ") splits a blocked chain where nothing else meets it";
					return err.str();
				}
			}
		}
		// A blocked ring's original corner over kept ground is a vertex with both its edges on
		// it.
		for (RingId r = 0; r < static_cast<RingId>(rings.size()); ++r) {
			const RegisteredRing& reg = rings[static_cast<std::size_t>(r)];
			if (!reg.live || !reg.polygon.blocked) {
				continue;
			}
			const std::vector<Vec2i64>& outline = reg.polygon.ring;
			const std::int32_t			n		= static_cast<std::int32_t>(outline.size());
			for (std::int32_t i = 0; i < n; ++i) {
				const Vec2i64& p = outline[static_cast<std::size_t>(i)];
				if (locate(p) < 0) {
					continue;
				}
				const std::int32_t	 before = (i + n - 1) % n;
				const std::uint32_t* at		= vertexAt.find(p);
				if (at == nullptr) {
					fail() << "corner " << i << " of ring " << r << " is over ground but not a vertex";
					return err.str();
				}
				if (onBoundary[*at] != 0) {
					continue;
				}
				for (const std::int32_t edge : {i, before}) {
					if (edgeStart(outline, edge) == edgeEnd(outline, edge)) {
						continue;
					}
					bool present = false;
					for (std::int32_t h : piecesOf({r, edge})) {
						const NavTriangle& tri = tris[static_cast<std::size_t>(h / 3)];
						present = present || tri.v[static_cast<std::size_t>(h % 3)] == *at ||
								  tri.v[static_cast<std::size_t>(next3(h % 3))] == *at;
					}
					if (!present) {
						fail() << "ring " << r << " edge " << edge << " is missing at its corner " << i;
						return err.str();
					}
				}
			}
		}

		// Widths, once refreshed, are what a fresh search gives; door tags are what the door
		// registry says.
		{
			const NavView v = view();
			WidthSearch	  search;
			for (std::int32_t t = 0; t < triCount; ++t) {
				if (live[static_cast<std::size_t>(t)] == 0) {
					continue;
				}
				const NavTriangle& tri = tris[static_cast<std::size_t>(t)];
				for (int e = 0; e < 3; ++e) {
					if (widthsCurrent()) {
						if (tri.edgePairWidthMm[static_cast<std::size_t>(e)] != corridorWidthMm(v, search, t, e, maxClearanceMm)) {
							fail() << "triangle " << t << " apex " << e << " has a stale corridor width";
							return err.str();
						}
					}
					auto		 door	 = doorsAt.find(edgeKey(verts[tri.v[static_cast<std::size_t>(e)]], verts[tri.v[static_cast<std::size_t>(next3(e))]]));
					const bool	 tagged	 = door != doorsAt.end();
					std::int64_t opening = tagged ? doors[static_cast<std::size_t>(door->second.front())].openingId : kNoOpening;
					std::int64_t width	 = tagged ? doors[static_cast<std::size_t>(door->second.front())].clearWidthMm : kUnconstrainedWidth;
					if (tri.edgeOpening[static_cast<std::size_t>(e)] != opening || tri.edgeClearWidthMm[static_cast<std::size_t>(e)] != width) {
						fail() << "triangle " << t << " edge " << e << " has stale door tags";
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
			// Snap rounding bends a ring's edges off its original lines, so a face can
			// straddle one; within as far as the ring's pieces stray from its outline, the
			// original outline does not decide.
			auto nearOutline = [&](RingId r) {
				const std::vector<Vec2i64>& ring3 = rings[static_cast<std::size_t>(r)].sample.ring3;
				const std::int64_t			stray = pieceDeviationMm(r);
				bool						near  = false;
				for (std::size_t i = 0; i < ring3.size() && !near; ++i) {
					near = withinDistanceOfSegment(sample3, ring3[i], ring3[(i + 1) % ring3.size()], 3 * stray);
				}
				return near;
			};
			// Ground is any bounds ring holding the sample.
			std::vector<RingId> exact;
			std::vector<RingId> boundsNear;
			bool				exactGround = false;
			const Vec2i64		sampleLo{floorDiv(sample3.x, 3), floorDiv(sample3.y, 3)};
			for (RingId r : ringsInBox(sampleLo, sampleLo + Vec2i64{1, 1})) {
				const SampleRing& sample = rings[static_cast<std::size_t>(r)].sample;
				if (isBounds(r)) {
					boundsNear.push_back(r);
					exactGround = exactGround || boundsContainSample(sample, sample3);
				} else if (ringContainsSample(sample, sample3)) {
					exact.push_back(r);
				}
			}
			const std::vector<RingId>& have		  = cover(best);
			const bool				   haveGround = !have.empty() && have.front() == kGroundToken;
			if (haveGround != exactGround &&
				std::none_of(boundsNear.begin(), boundsNear.end(), [&](RingId r) { return nearOutline(r); })) {
				fail() << "face of triangle " << best << ": ground disagrees with exact containment of its sample in the bounds rings";
				return err.str();
			}
			std::vector<RingId> differ;
			std::set_symmetric_difference(exact.begin(), exact.end(), have.begin() + (haveGround ? 1 : 0), have.end(), std::back_inserter(differ));
			for (RingId r : differ) {
				if (!nearOutline(r)) {
					fail() << "face of triangle " << best << ": cover disagrees with exact containment of its sample in ring " << r;
					return err.str();
				}
			}
		}
		return err.str();
	}

} // namespace geometry::nav

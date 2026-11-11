#include "PatchableMesh.h"

#include "../arrangement/Arrangement.h"
#include "../predicates/Predicates.h"
#include "../triangulation/ConstrainedDelaunay.h"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <numeric>
#include <set>
#include <sstream>
#include <utility>

namespace geometry::nav {

	namespace {

		// How close two original edges must come for the off-ground pull to treat them as
		// interacting. buildArrangement's rounding has no proven drift bound: each cascade
		// level can move a piece another 0.71 mm, and the six segments in the arrangement
		// test RepeatedRoundingDriftsPastTwoMillimetres drift 2.98 mm. Pulling too much
		// into s is only slower; pulling too little loses exactness.
		constexpr std::int64_t kOffGroundReachMm = 8;

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
		assert(refs[static_cast<std::size_t>(id)] > 0 && "InternTable: releasing a dead entry");
		if (--refs[static_cast<std::size_t>(id)] == 0) {
			ids.erase(values[static_cast<std::size_t>(id)]);
			values[static_cast<std::size_t>(id)] = {};
			freeIds.push_back(id);
		}
	}

	// --- BoxGrid -----------------------------------------------------------------

	namespace {

		constexpr std::uint64_t kEmptySlot = ~std::uint64_t{0};
		constexpr int			kCellBits  = 29;
		constexpr std::int64_t	kCellRange = std::int64_t{1} << (kCellBits - 1);
		constexpr std::uint64_t kCellMask  = (std::uint64_t{1} << kCellBits) - 1;

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

	std::uint64_t PatchableMesh::BoxGrid::keyOf(int level, std::int64_t cx, std::int64_t cy) {
		assert(cx >= -kCellRange && cx < kCellRange && cy >= -kCellRange && cy < kCellRange && "BoxGrid: cell out of range");
		return (static_cast<std::uint64_t>(level) << (2 * kCellBits)) | ((static_cast<std::uint64_t>(cx + kCellRange) & kCellMask) << kCellBits) |
			   (static_cast<std::uint64_t>(cy + kCellRange) & kCellMask);
	}

	std::size_t PatchableMesh::BoxGrid::home(std::uint64_t key) const {
		return static_cast<std::size_t>(mixKey(key)) & (slotKeys.size() - 1);
	}

	std::int32_t PatchableMesh::BoxGrid::find(std::uint64_t key) const {
		if (slotKeys.empty()) {
			return -1;
		}
		for (std::size_t i = home(key);; i = (i + 1) & (slotKeys.size() - 1)) {
			if (slotKeys[i] == key) {
				return slotCells[i];
			}
			if (slotKeys[i] == kEmptySlot) {
				return -1;
			}
		}
	}

	std::int32_t PatchableMesh::BoxGrid::findOrAdd(std::uint64_t key, int level) {
		if (2 * (occupied + 1) > slotKeys.size()) {
			const std::size_t		   capacity = std::max<std::size_t>(64, 2 * slotKeys.size());
			std::vector<std::uint64_t> oldKeys	= std::move(slotKeys);
			std::vector<std::int32_t>  oldCells = std::move(slotCells);
			slotKeys.assign(capacity, kEmptySlot);
			slotCells.assign(capacity, -1);
			for (std::size_t i = 0; i < oldKeys.size(); ++i) {
				if (oldKeys[i] == kEmptySlot) {
					continue;
				}
				std::size_t j = home(oldKeys[i]);
				while (slotKeys[j] != kEmptySlot) {
					j = (j + 1) & (capacity - 1);
				}
				slotKeys[j]	 = oldKeys[i];
				slotCells[j] = oldCells[i];
			}
		}
		std::size_t i = home(key);
		while (slotKeys[i] != kEmptySlot) {
			if (slotKeys[i] == key) {
				return slotCells[i];
			}
			i = (i + 1) & (slotKeys.size() - 1);
		}
		std::int32_t cell = 0;
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
		slotKeys[i]	 = key;
		slotCells[i] = cell;
		++occupied;
		return cell;
	}

	// Backward-shift deletion keeps every probe run unbroken without tombstones.
	void PatchableMesh::BoxGrid::dropCell(std::int32_t cell, int level) {
		const std::uint64_t key	 = cells[static_cast<std::size_t>(cell)].key;
		const std::size_t	mask = slotKeys.size() - 1;
		std::size_t			i	 = home(key);
		while (slotKeys[i] != key) {
			i = (i + 1) & mask;
		}
		for (std::size_t j = (i + 1) & mask; slotKeys[j] != kEmptySlot; j = (j + 1) & mask) {
			if (((j - home(slotKeys[j])) & mask) >= ((j - i) & mask)) {
				slotKeys[i]	 = slotKeys[j];
				slotCells[i] = slotCells[j];
				i			 = j;
			}
		}
		slotKeys[i]	 = kEmptySlot;
		slotCells[i] = -1;
		--occupied;

		std::vector<std::int32_t>& onLevel = levelCells[static_cast<std::size_t>(level)];
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
				const std::int32_t cell = findOrAdd(keyOf(level, cx, cy), level);
				cells[static_cast<std::size_t>(cell)].entries.push_back({id, lo, hi});
			}
		}
	}

	void PatchableMesh::BoxGrid::erase(std::int32_t id, const Vec2i64& lo, const Vec2i64& hi) {
		const int		   level = levelFor(lo, hi);
		const std::int64_t side	 = kBaseCell << level;
		for (std::int64_t cy = floorDiv(lo.y, side); cy <= floorDiv(hi.y, side); ++cy) {
			for (std::int64_t cx = floorDiv(lo.x, side); cx <= floorDiv(hi.x, side); ++cx) {
				const std::int32_t cell = find(keyOf(level, cx, cy));
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
			const std::vector<std::int32_t>& onLevel = levelCells[static_cast<std::size_t>(level)];
			const std::int64_t				 side	 = (kBaseCell << level) * scale;
			const std::int64_t				 x0		 = floorDiv(lo.x, side);
			const std::int64_t				 x1		 = floorDiv(hi.x, side);
			const std::int64_t				 y0		 = floorDiv(lo.y, side);
			const std::int64_t				 y1		 = floorDiv(hi.y, side);
			const double					 span	 = static_cast<double>(x1 - x0 + 1) * static_cast<double>(y1 - y0 + 1);
			if (span > static_cast<double>(onLevel.size())) {
				for (std::int32_t cell : onLevel) {
					const Cell&		   c  = cells[static_cast<std::size_t>(cell)];
					const std::int64_t cx = static_cast<std::int64_t>(c.key >> kCellBits & kCellMask) - kCellRange;
					const std::int64_t cy = static_cast<std::int64_t>(c.key & kCellMask) - kCellRange;
					if (cx >= x0 && cx <= x1 && cy >= y0 && cy <= y1) {
						visitOverlapping(c.entries, lo, hi, scale, visit);
					}
				}
				continue;
			}
			for (std::int64_t cy = y0; cy <= y1; ++cy) {
				for (std::int64_t cx = x0; cx <= x1; ++cx) {
					const std::int32_t cell = find(keyOf(level, cx, cy));
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
			const std::int32_t cell = find(keyOf(level, floorDiv(p.x, side), floorDiv(p.y, side)));
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
		for (const std::vector<std::int32_t>& onLevel : levelCells) {
			for (std::int32_t cell : onLevel) {
				for (const Entry& e : cells[static_cast<std::size_t>(cell)].entries) {
					visit(e.id);
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
		// A point strictly inside a triangle is in no other, so that one answers at once;
		// a point on an edge or vertex takes the lowest id of every triangle holding it.
		std::int32_t best = -1;
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
			if (best < 0 || t < best) {
				best = t;
			}
			return false;
		});
		return best;
	}

	std::int32_t PatchableMesh::locate(const Vec2i64& p) const {
		return locateScaled(p, 1);
	}

	template <typename Visit> void PatchableMesh::forEachLiveInBox(const Vec2i64& lo, const Vec2i64& hi, Visit visit) const {
		// A triangle sits in up to four cells; the stamp visits it once. Callers only
		// gather into sets, so the order (the grid's) does not reach any result.
		assert(!boxQueryOpen && "PatchableMesh: nested box query");
		boxQueryOpen = true;
		if (visitStamp.size() < tris.size()) {
			visitStamp.resize(tris.size(), 0);
		}
		if (++stamp == 0) {
			std::fill(visitStamp.begin(), visitStamp.end(), 0);
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

	template <typename Visit> void PatchableMesh::forEachAroundVertex(std::uint32_t v, Visit visit) const {
		// Each level has one cell holding the point, and a triangle on the vertex is in
		// the one of its level, so every triangle comes up once.
		grid.forEachAtPoint(verts[v], 1, [&](std::int32_t t) {
			const NavTriangle& tri = tris[static_cast<std::size_t>(t)];
			for (int i = 0; i < 3; ++i) {
				if (tri.v[i] == v) {
					visit(t, i);
				}
			}
			return false;
		});
	}

	// The mesh boundary runs along bounds edges, so a box clear of every bounds ring's
	// edges is clear of it (the margin covers how far snap rounding bends them).
	bool PatchableMesh::nearBoundsEdge(const Vec2i64& lo, const Vec2i64& hi) const {
		for (RingId r : ringsInBox(lo, hi)) {
			const NavInputPolygon& poly = rings[static_cast<std::size_t>(r)].polygon;
			if (poly.blocked) {
				continue;
			}
			for (std::int32_t e = 0; e < static_cast<std::int32_t>(poly.ring.size()); ++e) {
				Vec2i64 eLo = edgeStart(poly.ring, e);
				Vec2i64 eHi = eLo;
				growBox(eLo, eHi, edgeEnd(poly.ring, e));
				if (boxesOverlap(lo, hi, eLo, eHi)) {
					return true;
				}
			}
		}
		return false;
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
			assert(pos != list.end() && "PatchableMesh: a half-edge missing from the ring edge index");
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
		void pullOffGround(const Vec2i64& a, const Vec2i64& b, bool boundsEdge);
		void pullAtVertex(std::uint32_t v, const Vec2i64& p, const std::vector<std::pair<EdgeSource, bool>>& xs, bool takeStar);
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
	// edge whose original comes within kOffGroundReachMm joins s. A bounds edge runs along
	// the boundary, so it always asks the registry, but it never pulls the boundary
	// pieces beside it: those are bounds edges too, which the mesh shows whole, and
	// pulling them would walk the whole border.
	void PatchableMesh::Patch::pullOffGround(const Vec2i64& a, const Vec2i64& b, bool boundsEdge) {
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
		for (RingId r : m.ringsInBox(lo, hi)) {
			const RegisteredRing& reg = m.rings[static_cast<std::size_t>(r)];
			if (!reg.polygon.blocked || changes.count(r) != 0) {
				continue;
			}
			const std::vector<Vec2i64>& outline = reg.polygon.ring;
			for (std::int32_t e = 0; e < static_cast<std::int32_t>(outline.size()); ++e) {
				const Vec2i64 p = edgeStart(outline, e);
				const Vec2i64 q = edgeEnd(outline, e);
				if (p != q && s.count({r, e}) == 0 && segmentsWithin(a, b, p, q, kOffGroundReachMm)) {
					addToS({r, e});
				}
			}
		}
	}

	void PatchableMesh::Patch::begin(const PatchEdits& edits) {
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
			setNew(changes[id], add);
		}
		for (RingId id : edits.removes) {
			assert(m.rings[static_cast<std::size_t>(id)].live && changes.count(id) == 0 && "PatchableMesh: removing a dead ring, or editing one twice");
			changes[id].hadOld = true;
		}
		for (const RingMove& move : edits.moves) {
			assert(m.rings[static_cast<std::size_t>(move.ring)].live && changes.count(move.ring) == 0 && "PatchableMesh: moving a dead ring, or editing one twice");
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
			for (RingId r : m.ringsInBox(c.lo, c.hi)) {
				const RegisteredRing& reg = m.rings[static_cast<std::size_t>(r)];
				if (changes.count(r) != 0) {
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
		std::uint32_t v, const Vec2i64& p, const std::vector<std::pair<EdgeSource, bool>>& xs, bool takeStar) {
		m.forEachAroundVertex(v, [&](std::int32_t t, int i) {
			if (takeStar) {
				hole.insert(t);
			}
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
				pullOffGround(a, b, !c.polygon.blocked);
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
							pullAtVertex(tri.v[static_cast<std::size_t>(k)], p, {{{id, e}, p == a || p == b}}, false);
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
				pullOffGround(a, b, !m.rings[static_cast<std::size_t>(x.ring)].polygon.blocked);
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
				pullAtVertex(v, p, {{x, isOriginalEndpoint(oldOutline(x.ring), x.edge, p)}}, true);
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
		struct Touch {
			std::uint32_t vertex = 0; // old mesh vertex
			EdgeSource	  source;
			bool		  corner = false;
			auto		  operator<=>(const Touch&) const = default;
		};
		std::vector<Touch> touching;
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
				auto old = m.vertexAt.find(end);
				if (old == m.vertexAt.end()) {
					continue;
				}
				for (std::int64_t local : ae.provenance) {
					if (fromS(local)) {
						const EdgeSource& x = segments[static_cast<std::size_t>(local)].source;
						touching.push_back({old->second, x, isOriginalEndpoint(newPolygon(x.ring).ring, x.edge, end)});
					}
				}
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
		std::sort(touching.begin(), touching.end());
		std::vector<std::pair<EdgeSource, bool>> xs;
		for (std::size_t i = 0; i < touching.size();) {
			const std::uint32_t v = touching[i].vertex;
			xs.clear();
			for (; i < touching.size() && touching[i].vertex == v; ++i) {
				xs.push_back({touching[i].source, touching[i].corner});
			}
			const std::size_t before = s.size();
			pullAtVertex(v, m.verts[v], xs, false);
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

		std::vector<const SampleRing*> newBounds;
		for (const auto& [id, c] : changes) {
			if (c.hasNew && !c.polygon.blocked) {
				newBounds.push_back(&c.sample);
			}
		}

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
			for (const SampleRing* bounds : newBounds) {
				if (ringContainsSample(*bounds, sample3)) {
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
			if (reg.live) {
				m.ringGrid.erase(id, reg.lo, reg.hi);
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

		// New triangles take the hole's ids first. A face's triangles share one cover and
		// an edge's two sides one source list, each interned once.
		std::vector<std::int32_t> faceCover(faces.size(), -1);
		std::vector<std::int32_t> edgeList(arrangement.edges.size(), -1);
		std::vector<std::int64_t> edgeProvenance(arrangement.edges.size(), kNoProvenance);
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
			out.newTriangles.push_back(id);
		}
		for (std::size_t r = nextReuse; r < reuse.size(); ++r) {
			m.freeTriangles.push_back(reuse[r]);
		}

		// Adjacency and ring sources.
		// The hole's own edges are constrained Delaunay already; only an edge it shares
		// with a kept triangle can need a flip.
		std::vector<std::uint8_t>				  linked(fixed.size(), 0);
		std::vector<std::pair<std::int32_t, int>> boundaryEdges;
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
					const std::vector<EdgeSource> list = sourcesOf(c);
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

		for (std::int32_t id : out.newTriangles) {
			Vec2i64 tLo;
			Vec2i64 tHi;
			m.triangleBox(id, tLo, tHi);
			m.grid.insert(id, tLo, tHi);
			reportBox(id);
		}
		std::vector<std::int32_t> flipped;
		m.legalize(boundaryEdges, flipped);
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

		// A flip moves edges between triangles, so door tags are set afresh on every new
		// and flipped triangle; their widths, and widths near what changed, wait for
		// refreshWidths.
		for (std::int32_t t : out.newTriangles) {
			m.applyDoorTags(t);
		}
		if (boxSet) {
			m.pendingWidthBoxes.push_back({lo, hi});
			m.freshWidthTriangles.insert(m.freshWidthTriangles.end(), out.newTriangles.begin(), out.newTriangles.end());
		}
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
			const Vec2i64 step{d.x / steps, d.y / steps};
			std::int64_t  previous = 0;
			for (std::int64_t k = 1; k < pieces; ++k) {
				const std::int64_t at = k * steps / pieces;
				if (at > previous && at < steps) {
					out.push_back(a + step * at);
					previous = at;
				}
			}
		}
		return out;
	}

	PatchableMesh::PatchableMesh(const PatchableMeshArgs& args)
		: maxBoundsEdgeMm(args.maxBoundsEdgeMm), maxClearanceMm(args.maxClearanceMm) {}

	PatchReport PatchableMesh::applyPatch(const PatchEdits& edits) {
		Patch patch(*this, edits);
		forestsCurrent = false;
		return patch.run();
	}

	// --- Doors, widths, forests, and the view ---------------------------------------

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
		auto va = vertexAt.find(a);
		auto vb = vertexAt.find(b);
		if (va == vertexAt.end() || vb == vertexAt.end()) {
			return;
		}
		std::vector<std::int32_t> onEdge;
		forEachAroundVertex(va->second, [&](std::int32_t t, int i) {
			const NavTriangle& tri = tris[static_cast<std::size_t>(t)];
			if (tri.v[static_cast<std::size_t>(next3(i))] == vb->second || tri.v[static_cast<std::size_t>(prev3(i))] == vb->second) {
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
		forestsCurrent = false;
		return id;
	}

	void PatchableMesh::removeDoor(DoorId id) {
		assert(doorLive[static_cast<std::size_t>(id)] != 0 && "PatchableMesh: removing a door twice");
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
		forestsCurrent = false;
	}

	// Each patch since the last refresh left a box holding every triangle and adjacency
	// it changed, and marked its new and flipped triangles, which get their widths from
	// scratch. An unchanged triangle's width w at apex C (w read as maxClearanceMm when
	// unconstrained) stays right unless a box comes within w of C: the obstacle at
	// distance w is found through triangles inside the disc of radius w around C, none of
	// them changed, and nothing nearer than w can appear outside the boxes
	// (corridorWidthMm reads nothing farther than maxClearanceMm). So only those apexes
	// are searched again.
	void PatchableMesh::refreshWidths() {
		const Vec2i64			  reach{maxClearanceMm, maxClearanceMm};
		std::vector<std::int32_t> near;
		for (const auto& [lo, hi] : pendingWidthBoxes) {
			forEachLiveInBox(lo - reach, hi + reach, [&](std::int32_t t) { near.push_back(t); });
		}
		std::sort(near.begin(), near.end());
		near.erase(std::unique(near.begin(), near.end()), near.end());
		std::sort(freshWidthTriangles.begin(), freshWidthTriangles.end());
		const NavView view = meshView();
		for (std::int32_t t : near) {
			const bool fresh = std::binary_search(freshWidthTriangles.begin(), freshWidthTriangles.end(), t);
			NavTriangle& tri = tris[static_cast<std::size_t>(t)];
			for (int apex = 0; apex < 3; ++apex) {
				const Vec2i64&	   c	 = verts[tri.v[static_cast<std::size_t>(apex)]];
				const std::int64_t w	 = std::min(tri.edgePairWidthMm[static_cast<std::size_t>(apex)], maxClearanceMm);
				bool			   stale = fresh;
				for (std::size_t b = 0; b < pendingWidthBoxes.size() && !stale; ++b) {
					const auto& [lo, hi] = pendingWidthBoxes[b];
					const Vec2i64 gap{std::max({lo.x - c.x, c.x - hi.x, std::int64_t{0}}), std::max({lo.y - c.y, c.y - hi.y, std::int64_t{0}})};
					stale = dot(gap, gap) <= Int128::product(w, w);
				}
				if (stale) {
					tri.edgePairWidthMm[static_cast<std::size_t>(apex)] = corridorWidthMm(view, widthSearch, t, apex, maxClearanceMm);
				}
			}
		}
		freshWidthTriangles.clear();
		pendingWidthBoxes.clear();
	}

	void PatchableMesh::rebuildForests() {
		refreshWidths();
		forests		   = buildReachabilityForests(meshView());
		forestsCurrent = true;
	}

	NavView PatchableMesh::view() const {
		assert(widthsCurrent() && "PatchableMesh: refreshWidths before a query");
		return meshView();
	}

	NavView PatchableMesh::meshView() const {
		NavView v;
		v.vertices	= &verts;
		v.triangles = &tris;
		v.live		= &live;
		v.locate	= [](const void* owner, const Vec2i64& p) { return static_cast<const PatchableMesh*>(owner)->locate(p); };
		v.owner		= this;
		if (forestsCurrent) {
			v.truthForest	= &forests.truth;
			v.terrainForest = &forests.terrain;
		}
		return v;
	}

	// --- Validator -------------------------------------------------------------------

	// The farthest any vertex of this ring's pieces on the mesh lies from the line of
	// the original edge it is on, rounded up to a millimetre.
	std::int64_t PatchableMesh::pieceDeviationMm(RingId ring) const {
		const std::vector<Vec2i64>& outline = rings[static_cast<std::size_t>(ring)].polygon.ring;
		std::int64_t				worst	= 0;
		for (std::int32_t e = 0; e < static_cast<std::int32_t>(outline.size()); ++e) {
			auto pieces = halfEdgesOf.find(sourceKey({ring, e}));
			if (pieces == halfEdgesOf.end()) {
				continue;
			}
			const Vec2i64 a	   = edgeStart(outline, e);
			const Vec2i64 dir  = edgeEnd(outline, e) - a;
			const Int128  len2 = dot(dir, dir);
			for (std::int32_t h : pieces->second) {
				const NavTriangle& tri = tris[static_cast<std::size_t>(h / 3)];
				for (const std::uint32_t v : {tri.v[static_cast<std::size_t>(h % 3)], tri.v[static_cast<std::size_t>(next3(h % 3))]}) {
					const Int128 off = cross(dir, verts[v] - a);
					while (Int128::compareSquareToProduct(off, Int128::product(worst, worst), len2) > 0) {
						++worst;
					}
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
				if (r < 0 || r >= static_cast<RingId>(rings.size()) || !rings[static_cast<std::size_t>(r)].live) {
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
		std::vector<std::uint64_t> keys;
		for (const auto& [key, list] : halfEdgesOf) {
			keys.push_back(key);
		}
		std::sort(keys.begin(), keys.end()); // the first violation reported is the same everywhere
		for (const std::uint64_t key : keys) {
			const std::vector<std::int32_t>& list = halfEdgesOf.at(key);
			const EdgeSource				 z{static_cast<RingId>(key >> 32), static_cast<std::int32_t>(key & 0xFFFFFFFFULL)};
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
		for (RingId r = 0; r < static_cast<RingId>(rings.size()); ++r) {
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

		// Widths, once refreshed, are what a fresh search gives; door tags are what the door
		// registry says; forests, when the mesh has them, are what a fresh build gives.
		{
			const NavView v = meshView();
			WidthSearch	  search;
			for (std::int32_t t = 0; t < triCount; ++t) {
				if (live[static_cast<std::size_t>(t)] == 0) {
					continue;
				}
				const NavTriangle& tri = tris[static_cast<std::size_t>(t)];
				for (int e = 0; e < 3; ++e) {
					if (widthsCurrent() && tri.edgePairWidthMm[static_cast<std::size_t>(e)] != corridorWidthMm(v, search, t, e, maxClearanceMm)) {
						fail() << "triangle " << t << " apex " << e << " has a stale corridor width";
						return err.str();
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
			if (forestsCurrent) {
				const ReachabilityForests fresh = buildReachabilityForests(v);
				if (fresh.truth.component != forests.truth.component || fresh.truth.nodeCap != forests.truth.nodeCap ||
					fresh.terrain.component != forests.terrain.component || fresh.terrain.nodeCap != forests.terrain.nodeCap) {
					return "reachability forests differ from a fresh build";
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
			const Vec2i64		sampleLo{floorDiv(sample3.x, 3), floorDiv(sample3.y, 3)};
			for (RingId r : ringsInBox(sampleLo, sampleLo + Vec2i64{1, 1})) {
				if (ringContainsSample(rings[static_cast<std::size_t>(r)].sample, sample3)) {
					exact.push_back(r);
				}
			}
			std::vector<RingId> differ;
			std::set_symmetric_difference(exact.begin(), exact.end(), cover(best).begin(), cover(best).end(), std::back_inserter(differ));
			for (RingId r : differ) {
				// Snap rounding bends a ring's edges off its original lines, so a face can
				// straddle one; within as far as this ring's pieces stray from its outline,
				// the original outline does not decide.
				const std::vector<Vec2i64>& ring3  = rings[static_cast<std::size_t>(r)].sample.ring3;
				const std::int64_t			stray  = pieceDeviationMm(r);
				bool						near   = false;
				for (std::size_t i = 0; i < ring3.size() && !near; ++i) {
					near = withinDistanceOfSegment(sample3, ring3[i], ring3[(i + 1) % ring3.size()], 3 * stray);
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

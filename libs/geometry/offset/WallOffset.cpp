#include "WallOffset.h"

#include "../predicates/Predicates.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <map>
#include <utility>
#include <vector>

namespace geometry {

	namespace {

		// Left normal of a direction vector under CCW = positive area (the core's
		// convention): rotate (dx, dy) by +90 degrees -> (-dy, dx).
		Vec2i64 leftNormalUnscaled(const Vec2i64& dir) {
			return {-dir.y, dir.x};
		}

		double length(const Vec2i64& v) {
			return std::sqrt(static_cast<double>(v.x) * static_cast<double>(v.x) +
							 static_cast<double>(v.y) * static_cast<double>(v.y));
		}

		Vec2i64 roundVec(double x, double y) {
			return {std::llround(x), std::llround(y)};
		}

		// Point `vertex` offset by `signedHalf` mm along the left normal of `dir`,
		// rounded to the nearest mm. Positive signedHalf is the left side.
		Vec2i64 offsetPoint(const Vec2i64& vertex, const Vec2i64& dir, double signedHalf) {
			const double len = length(dir);
			if (len == 0.0) {
				return vertex;
			}
			const Vec2i64 n = leftNormalUnscaled(dir);
			const double  s = signedHalf / len;
			return roundVec(static_cast<double>(vertex.x) + static_cast<double>(n.x) * s,
							static_cast<double>(vertex.y) + static_cast<double>(n.y) * s);
		}

		// Point `from` moved `dist` mm along the unit direction of `dir`, rounded.
		Vec2i64 advance(const Vec2i64& from, const Vec2i64& dir, double dist) {
			const double len = length(dir);
			if (len == 0.0) {
				return from;
			}
			const double s = dist / len;
			return roundVec(static_cast<double>(from.x) + static_cast<double>(dir.x) * s,
							static_cast<double>(from.y) + static_cast<double>(dir.y) * s);
		}

		// Intersect two lines, each given by a point and a direction. Returns the
		// parameter t along (p0, d0) of the intersection, or false when the
		// directions are parallel. The denominator (cross of the two directions)
		// and the numerator are both the exact 128-bit cross products, so the
		// parallel test is exact: a true non-zero determinant can never round to
		// zero and misclassify the lines. Only the final t = num/denom ratio is
		// taken in double, the one inexact step the caller then rounds to the mm
		// grid (consistent with the module's rounding policy in the header).
		bool lineIntersectParam(const Vec2i64& p0, const Vec2i64& d0, const Vec2i64& p1, const Vec2i64& d1, double& t) {
			const Int128 denom = cross(d0, d1);
			if (denom.sign() == 0) {
				return false;
			}
			const Vec2i64 w		= p1 - p0;
			const Int128  numer = cross(w, d1);
			t					= numer.toDouble() / denom.toDouble();
			return true;
		}

	} // namespace

	Ring band(const Vec2i64& a, const Vec2i64& b, std::int64_t halfThicknessMm) {
		const Vec2i64 dir = b - a;
		const double  h	  = static_cast<double>(halfThicknessMm);

		// Left/right corners at each centerline endpoint. The offset h*n/|dir| is
		// generally non-integer; rounding here is the single inexact step (see
		// header). Walk a-left, b-left, b-right, a-right then orient CCW.
		const Vec2i64 aLeft	 = offsetPoint(a, dir, h);
		const Vec2i64 bLeft	 = offsetPoint(b, dir, h);
		const Vec2i64 bRight = offsetPoint(b, dir, -h);
		const Vec2i64 aRight = offsetPoint(a, dir, -h);

		Ring ring = {aLeft, bLeft, bRight, aRight};
		ensureCounterClockwise(ring);
		return ring;
	}

	bool trimmedBand(const Vec2i64& a, const Vec2i64& b, std::int64_t halfThicknessMm, std::int64_t trimAtA,
					 std::int64_t trimAtB, Ring& out) {
		const Vec2i64 dir = b - a;
		const double  len = length(dir);
		if (len == 0.0) {
			return false;
		}
		if (static_cast<double>(trimAtA) + static_cast<double>(trimAtB) >= len) {
			return false; // cutbacks meet or cross: the band would vanish or invert
		}

		const Vec2i64 a2 = advance(a, dir, static_cast<double>(trimAtA));
		const Vec2i64 b2 = advance(b, -dir, static_cast<double>(trimAtB));
		const double  h	 = static_cast<double>(halfThicknessMm);

		const Vec2i64 aLeft	 = offsetPoint(a2, dir, h);
		const Vec2i64 bLeft	 = offsetPoint(b2, dir, h);
		const Vec2i64 bRight = offsetPoint(b2, dir, -h);
		const Vec2i64 aRight = offsetPoint(a2, dir, -h);

		out = {aLeft, bLeft, bRight, aRight};
		ensureCounterClockwise(out);
		return true;
	}

	JunctionResolution resolveJunction(const Vec2i64& vertex, const std::vector<IncidentSegment>& incidents,
									   double miterLimit) {
		JunctionResolution result;

		const std::size_t degree = incidents.size();
		for (const IncidentSegment& s : incidents) {
			if (s.direction == vertex) {
				result.status = OffsetStatus::ZeroLengthSegment;
				return result;
			}
		}

		// Degree 1: free end. The band's own flat cap is the boundary; nothing to
		// trim, no polygon. Degree 0 is meaningless but harmless (empty result).
		if (degree <= 1) {
			for (const IncidentSegment& s : incidents) {
				result.trims.push_back({s.index, 0});
			}
			return result;
		}

		// Sort incidents CCW by outgoing angle. Wedges are between CCW-adjacent
		// pairs; each wedge's apex is where the two facing band edges meet. The
		// order uses the exact angleLess comparator (no atan2, deterministic);
		// two incidents with the exact same outgoing direction tie-break on the
		// local index so the order is a strict total order regardless of input
		// permutation.
		std::vector<std::size_t> order(degree);
		for (std::size_t i = 0; i < degree; ++i) {
			order[i] = i;
		}
		std::sort(order.begin(), order.end(), [&](std::size_t l, std::size_t r) {
			const Vec2i64 dl = incidents[l].direction - vertex;
			const Vec2i64 dr = incidents[r].direction - vertex;
			if (angleLess(dl, dr)) {
				return true;
			}
			if (angleLess(dr, dl)) {
				return false;
			}
			return l < r; // identical direction: deterministic tie-break by index
		});

		std::vector<std::int64_t> trimByLocal(degree, 0);

		// Per CCW-adjacent wedge (i -> i+1, wrapping), the apex is the intersection
		// of segment i's LEFT band edge and segment (i+1)'s RIGHT band edge. The
		// trim each segment needs is the projection of (apex - vertex) onto its own
		// outgoing direction; a segment takes the max over its two wedges so both
		// are cleared. Apexes are kept to weld into the junction polygon later.
		struct WedgeApex {
			Vec2i64 point;
			bool	beveled = false; // miter exceeded the limit -> squared off
		};
		std::vector<WedgeApex> apex(degree);

		for (std::size_t k = 0; k < degree; ++k) {
			const std::size_t li = order[k];
			const std::size_t ri = order[(k + 1) % degree];

			const IncidentSegment& segL = incidents[li];
			const IncidentSegment& segR = incidents[ri];
			const Vec2i64		   dirL = segL.direction - vertex;
			const Vec2i64		   dirR = segR.direction - vertex;
			const double		   hL	= static_cast<double>(segL.halfThicknessMm);
			const double		   hR	= static_cast<double>(segR.halfThicknessMm);

			// Left edge of segL: passes through vertex + leftOffset(dirL, hL).
			// Right edge of segR: passes through vertex - leftOffset(dirR, hR).
			const Vec2i64 pL = offsetPoint(vertex, dirL, hL);
			const Vec2i64 pR = offsetPoint(vertex, dirR, -hR);

			double tParam = 0.0;
			Vec2i64 apexPoint;
			bool	beveled = false;

			if (lineIntersectParam(pL, dirL, pR, dirR, tParam)) {
				apexPoint = roundVec(static_cast<double>(pL.x) + tParam * static_cast<double>(dirL.x),
									 static_cast<double>(pL.y) + tParam * static_cast<double>(dirL.y));
				// Miter length = distance from vertex to apex; cap it.
				const double miterLen	= length(apexPoint - vertex);
				const double maxHalf	= std::max(hL, hR);
				const double miterBound = miterLimit * maxHalf;
				if (maxHalf > 0.0 && miterLen > miterBound) {
					beveled = true;
				}
			} else {
				// Parallel facing edges (straight 180-deg continuation): no apex.
				// Leave apex at the band-corner level; trims stay 0 for this wedge.
				beveled	  = false;
				apexPoint = vertex; // sentinel; not used when both trims stay 0
				apex[k]	  = {apexPoint, false};
				continue;
			}

			apex[k] = {apexPoint, beveled};

			// The cutback each segment needs is the projection of this wedge's apex
			// onto its outgoing direction: that is how far the inner faces overlap,
			// and it is the same whether the wedge is mitered or beveled. Beveling
			// only changes the OUTER fill (the far apex is dropped and the two
			// corners are joined by a straight edge), not the trim. A negative
			// projection means the apex is on the far side of the vertex (the outer,
			// reflex wedge) and contributes no trim.
			auto projOnto = [&](const Vec2i64& dir) -> std::int64_t {
				const double len = length(dir);
				if (len == 0.0) {
					return 0;
				}
				const double proj = (static_cast<double>(apexPoint.x - vertex.x) * static_cast<double>(dir.x) +
									 static_cast<double>(apexPoint.y - vertex.y) * static_cast<double>(dir.y)) /
									len;
				return proj > 0.0 ? std::llround(proj) : 0;
			};
			trimByLocal[li] = std::max(trimByLocal[li], projOnto(dirL));
			trimByLocal[ri] = std::max(trimByLocal[ri], projOnto(dirR));
		}

		// Degree-2 straight continuation: both trims zero and the single real wedge
		// had no apex (parallel edges). No junction polygon needed; bands abut.
		bool anyTrim = false;
		for (std::int64_t t : trimByLocal) {
			anyTrim = anyTrim || (t > 0);
		}
		if (degree == 2 && !anyTrim) {
			for (std::size_t i = 0; i < degree; ++i) {
				result.trims.push_back({incidents[i].index, 0});
			}
			return result; // empty polygon
		}

		// Build the junction polygon as a star polygon about `vertex`. The fill
		// region is visible from the junction vertex (every band emanates from it),
		// so its boundary points sorted by angle around the vertex form a simple
		// ring. Boundary points are each segment's two EXACT trimmed near-corners
		// (the same rounded points the trimmed bands use, so shared edges coincide
		// verbatim) plus each non-beveled wedge apex (the outer/inner miter point).
		// Beveled wedges contribute no apex: the straight edge between the two
		// neighboring corners is the squared-off bevel.
		std::vector<Vec2i64> pts;
		pts.reserve(degree * 3);
		for (std::size_t k = 0; k < degree; ++k) {
			const std::size_t	   localIdx = order[k];
			const IncidentSegment& seg		= incidents[localIdx];
			const Vec2i64		   dir		= seg.direction - vertex;
			const double		   h		= static_cast<double>(seg.halfThicknessMm);
			const std::int64_t	   trim		= trimByLocal[localIdx];

			const Vec2i64 base	= advance(vertex, dir, static_cast<double>(trim));
			pts.push_back(offsetPoint(base, dir, -h));
			pts.push_back(offsetPoint(base, dir, h));

			const WedgeApex& w = apex[k];
			if (!w.beveled && !(w.point == vertex)) {
				pts.push_back(w.point);
			}
		}

		// Order boundary points CCW about the vertex with the exact angleLess
		// comparator (no atan2, deterministic). Points sharing a ray from the
		// vertex (equal angle) tie-break on exact squared distance then on the
		// point itself, so the comparator is a strict total order: std::sort then
		// never permutes collinear points unpredictably or yields a degenerate,
		// self-touching star. Duplicate points compare equal and are collapsed by
		// the dedup pass below.
		std::sort(pts.begin(), pts.end(), [&](const Vec2i64& l, const Vec2i64& r) {
			const Vec2i64 dl = l - vertex;
			const Vec2i64 dr = r - vertex;
			if (angleLess(dl, dr)) {
				return true;
			}
			if (angleLess(dr, dl)) {
				return false;
			}
			const Int128 distL = dot(dl, dl);
			const Int128 distR = dot(dr, dr);
			if (distL != distR) {
				return distL < distR;
			}
			return l < r; // exact lexicographic final tiebreak
		});
		Ring poly;
		poly.reserve(pts.size());
		for (const Vec2i64& p : pts) {
			if (poly.empty() || !(poly.back() == p)) {
				poly.push_back(p);
			}
		}
		if (poly.size() >= 2 && poly.front() == poly.back()) {
			poly.pop_back();
		}

		result.polygon = poly;
		for (std::size_t i = 0; i < degree; ++i) {
			result.trims.push_back({incidents[i].index, trimByLocal[i]});
		}
		return result;
	}

	void simplifyRing(Ring& ring, std::int64_t epsilonMm) {
		std::vector<std::uint8_t> pinned(ring.size(), 0);
		simplifyRing(ring, epsilonMm, pinned);
	}

	namespace {

		// Squared distance from p to segment [a, b], in double: only ranks
		// candidates, never decides a threshold (withinDistanceOfSegment does that
		// exactly), so rounding cannot change which vertices survive the tolerance.
		double rankDistance(const Vec2i64& p, const Vec2i64& a, const Vec2i64& b) {
			const double abx = static_cast<double>(b.x - a.x);
			const double aby = static_cast<double>(b.y - a.y);
			const double apx = static_cast<double>(p.x - a.x);
			const double apy = static_cast<double>(p.y - a.y);
			const double len = abx * abx + aby * aby;
			const double t	 = len > 0.0 ? std::clamp((apx * abx + apy * aby) / len, 0.0, 1.0) : 0.0;
			const double dx	 = apx - abx * t;
			const double dy	 = apy - aby * t;
			return dx * dx + dy * dy;
		}

	} // namespace

	void simplifyRing(Ring& ring, std::int64_t epsilonMm, std::vector<std::uint8_t>& pinned) {
		assert(pinned.size() == ring.size());
		const std::size_t n = ring.size();
		if (n <= 3) {
			return;
		}

		// Runs between consecutive anchors: every pinned vertex, and with fewer than
		// two pins also the vertex farthest from the first anchor, so every run has
		// two distinct ends. Each run is simplified on its own, so its result
		// depends only on the run, never on the ring's start vertex.
		std::vector<std::size_t> anchors;
		for (std::size_t i = 0; i < n; ++i) {
			if (pinned[i] != 0) {
				anchors.push_back(i);
			}
		}
		if (anchors.size() < 2) {
			const std::size_t from	   = anchors.empty() ? 0 : anchors.front();
			std::size_t		  farthest = from;
			Int128			  best(0);
			for (std::size_t i = 0; i < n; ++i) {
				const Vec2i64 d = ring[i] - ring[from];
				if (dot(d, d) > best) {
					best	 = dot(d, d);
					farthest = i;
				}
			}
			if (farthest == from) {
				return; // every vertex coincides
			}
			anchors = {std::min(from, farthest), std::max(from, farthest)};
		}

		// Douglas-Peucker per run: a span keeps its interior only where some vertex
		// lies farther than epsilon from the span's chord (exact test), splitting at
		// the farthest one. Every removed vertex therefore lies within epsilon of
		// the kept polyline, so the error never accumulates along a gentle curve.
		std::vector<std::uint8_t>							keep(n, 0);
		std::vector<std::pair<std::size_t, std::size_t>> spans; // (first vertex, edge count)
		for (std::size_t r = 0; r < anchors.size(); ++r) {
			keep[anchors[r]] = 1;
			spans.emplace_back(anchors[r], (anchors[(r + 1) % anchors.size()] + n - anchors[r]) % n);
		}
		while (!spans.empty()) {
			const auto [first, edges] = spans.back();
			spans.pop_back();
			if (edges < 2) {
				continue;
			}
			const Vec2i64& a		= ring[first];
			const Vec2i64& b		= ring[(first + edges) % n];
			bool		   within	= true;
			std::size_t	   split	= 1;
			double		   farthest = -1.0;
			for (std::size_t k = 1; k < edges; ++k) {
				const Vec2i64& v = ring[(first + k) % n];
				within			 = within && withinDistanceOfSegment(v, a, b, epsilonMm);
				const double d	 = rankDistance(v, a, b);
				if (d > farthest) {
					farthest = d;
					split	 = k;
				}
			}
			if (within) {
				continue;
			}
			keep[(first + split) % n] = 1;
			spans.emplace_back(first, split);
			spans.emplace_back((first + split) % n, edges - split);
		}

		std::size_t kept = 0;
		for (const std::uint8_t k : keep) {
			kept += k;
		}

		// An anchor added only to split the ring (not pinned) may go too, under the
		// same tolerance: when every vertex between its kept neighbors lies within
		// epsilon of their chord.
		for (const std::size_t c : anchors) {
			if (pinned[c] != 0 || kept <= 3) {
				continue;
			}
			std::size_t p = (c + n - 1) % n;
			while (keep[p] == 0) {
				p = (p + n - 1) % n;
			}
			std::size_t q = (c + 1) % n;
			while (keep[q] == 0) {
				q = (q + 1) % n;
			}
			bool within = true;
			for (std::size_t i = (p + 1) % n; i != q && within; i = (i + 1) % n) {
				within = withinDistanceOfSegment(ring[i], ring[p], ring[q], epsilonMm);
			}
			if (within) {
				keep[c] = 0;
				--kept;
			}
		}

		// Never below a triangle: restore the vertex farthest from the first span's
		// chord until there are three.
		while (kept < 3) {
			const Vec2i64& a		= ring[anchors[0]];
			const Vec2i64& b		= ring[anchors[1]];
			std::size_t	   restore	= n;
			double		   farthest = -1.0;
			for (std::size_t i = 0; i < n; ++i) {
				const double d = rankDistance(ring[i], a, b);
				if (keep[i] == 0 && d > farthest) {
					farthest = d;
					restore	 = i;
				}
			}
			keep[restore] = 1;
			++kept;
		}

		Ring					  out(kept);
		std::vector<std::uint8_t> outPinned(kept);
		std::size_t				  w = 0;
		for (std::size_t i = 0; i < n; ++i) {
			if (keep[i] != 0) {
				out[w]		 = ring[i];
				outPinned[w] = pinned[i];
				++w;
			}
		}
		ring   = std::move(out);
		pinned = std::move(outPinned);
	}

	namespace {

		// Validate a produced ring: at least a triangle, simple, non-zero area, and
		// orient CCW. Returns the failing status or Ok.
		OffsetStatus validateAndOrient(Ring& ring) {
			if (ring.size() < 3) {
				return OffsetStatus::DegenerateRing;
			}
			if (signedAreaDoubled(ring).sign() == 0) {
				return OffsetStatus::DegenerateRing;
			}
			ensureCounterClockwise(ring);
			if (!isSimple(ring).pass) {
				return OffsetStatus::NonSimpleRing;
			}
			return OffsetStatus::Ok;
		}

	} // namespace

	WallBands resolveWallBands(const std::vector<WallSegment>& segments, double miterLimit) {
		WallBands result;
		const std::size_t segCount = segments.size();
		result.bands.resize(segCount);

		// Reject zero-length input up front (reject-don't-repair, D4).
		for (const WallSegment& s : segments) {
			if (s.a == s.b) {
				result.status = OffsetStatus::ZeroLengthSegment;
				return result;
			}
		}

		// Group segment endpoints by exact position to derive junctions. Each entry
		// records (segment index, which endpoint, the other endpoint direction).
		struct Incidence {
			std::size_t segIndex;
			bool		atA; // true if this junction is segment's `a` endpoint
		};
		std::map<Vec2i64, std::vector<Incidence>> byVertex;
		for (std::size_t i = 0; i < segCount; ++i) {
			byVertex[segments[i].a].push_back({i, true});
			byVertex[segments[i].b].push_back({i, false});
		}

		// Per segment, the trim distance accumulated at each end.
		std::vector<std::int64_t> trimA(segCount, 0);
		std::vector<std::int64_t> trimB(segCount, 0);

		// Resolve each junction; collect its polygon and fold trims into the
		// per-segment per-end accumulators.
		for (const auto& [vertex, incidences] : byVertex) {
			std::vector<IncidentSegment> incidents;
			incidents.reserve(incidences.size());
			for (std::size_t k = 0; k < incidences.size(); ++k) {
				const Incidence& inc = incidences[k];
				const WallSegment& seg = segments[inc.segIndex];
				const Vec2i64	   other = inc.atA ? seg.b : seg.a;
				incidents.push_back({other, seg.halfThicknessMm, k});
			}

			const JunctionResolution jr = resolveJunction(vertex, incidents, miterLimit);
			if (jr.status != OffsetStatus::Ok) {
				result.status = jr.status;
				return result;
			}

			for (const JunctionTrim& t : jr.trims) {
				const Incidence& inc = incidences[t.index];
				if (inc.atA) {
					trimA[inc.segIndex] = std::max(trimA[inc.segIndex], t.trimMm);
				} else {
					trimB[inc.segIndex] = std::max(trimB[inc.segIndex], t.trimMm);
				}
			}

			if (!jr.polygon.empty()) {
				Ring poly = jr.polygon;
				simplifyRing(poly, kDefaultSimplifyEpsMm);
				const OffsetStatus st = validateAndOrient(poly);
				if (st != OffsetStatus::Ok) {
					result.status = st;
					return result;
				}
				std::vector<std::size_t> incidentSegs;
				incidentSegs.reserve(incidences.size());
				for (const Incidence& inc : incidences) {
					incidentSegs.push_back(inc.segIndex);
				}
				result.junctions.push_back(std::move(poly));
				result.junctionSegments.push_back(std::move(incidentSegs));
			}
		}

		// Apply accumulated trims to each band and validate.
		for (std::size_t i = 0; i < segCount; ++i) {
			const WallSegment& seg = segments[i];
			Ring			   ring;
			if (!trimmedBand(seg.a, seg.b, seg.halfThicknessMm, trimA[i], trimB[i], ring)) {
				result.status = OffsetStatus::TrimOverrunsSegment;
				return result;
			}
			simplifyRing(ring, kDefaultSimplifyEpsMm);
			const OffsetStatus st = validateAndOrient(ring);
			if (st != OffsetStatus::Ok) {
				result.status = st;
				return result;
			}
			result.bands[i] = std::move(ring);
		}

		return result;
	}

} // namespace geometry

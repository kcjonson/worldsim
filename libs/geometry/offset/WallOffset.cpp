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
		std::vector<std::size_t> steps; // wedges joined as a near-straight step

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

			// A near-straight join of two thicknesses is a step, not a miter. Off
			// straight by angle d the faces meet sqrt(hL^2 + hR^2 - 2 hL hR cos d) /
			// sin d from the vertex: about h with equal halves, but with unequal ones
			// it grows without bound as d shrinks, and the trims to that apex overrun
			// the segments a few degrees off straight (exactly straight has no apex
			// and is a step already). The cut is the miter bound itself, on the same
			// rounded apex the bevel test uses: a wedge whose apex is inside it is
			// mitered as before, one past it would put the trimmed corners past the
			// bound every other junction vertex keeps. Bands pointing within 90
			// degrees of each other never qualify. At legal corners (30 degrees and
			// up) that leaves a near-straight pair of different thicknesses, within
			// about 7.2 degrees of straight for halves 75/150, 4.8 for 100/150 and
			// 3.6 for 75/100, and an equal pair a hair off straight, whose faces
			// nearly coincide so that rounding their offsets throws the apex far off.
			// Right at the cut the two wedges of a degree-2 join can decide
			// differently; either way the shape is valid.
			if (beveled && dot(dirL, dirR).sign() < 0) {
				apex[k] = {vertex, false};
				steps.push_back(k);
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

		// A step has no apex and asks no trim of its own, like a parallel wedge. It
		// leaves the thicker band's cap where the other wedges put it and cuts the
		// thinner band (of an equal pair, the one whose right face borders the
		// step) back only until its corner facing the step is strictly in
		// front of that cap's line (exact side test on the rounded points, so the
		// two never overlap, and the corner never sits on the polygon's edge along
		// the cap). The polygon closes the step with a straight edge from that corner
		// out to the thicker band's corner. The corner moves away from the thicker
		// band as the trim grows, the two pointing more than 90 degrees apart, so
		// the loop ends.
		for (const std::size_t k : steps) {
			const std::size_t left		 = order[k];
			const std::size_t right		 = order[(k + 1) % degree];
			const bool		  leftIsThin = incidents[left].halfThicknessMm < incidents[right].halfThicknessMm;
			const std::size_t thin		 = leftIsThin ? left : right;
			const std::size_t thick		 = leftIsThin ? right : left;
			const double	  thinFace	 = static_cast<double>(incidents[thin].halfThicknessMm) * (leftIsThin ? 1.0 : -1.0);
			const double	  thickHalf	 = static_cast<double>(incidents[thick].halfThicknessMm);
			const Vec2i64	  thinDir	 = incidents[thin].direction - vertex;
			const Vec2i64	  thickDir	 = incidents[thick].direction - vertex;

			const Vec2i64 thickBase = advance(vertex, thickDir, static_cast<double>(trimByLocal[thick]));
			const Vec2i64 capRight	= offsetPoint(thickBase, thickDir, -thickHalf);
			const Vec2i64 capEdge	= offsetPoint(thickBase, thickDir, thickHalf) - capRight;
			const int	  thickSide = cross(capEdge, thickDir).sign();
			auto		  clearOfCap = [&](std::int64_t trim) {
				 const Vec2i64 corner = offsetPoint(advance(vertex, thinDir, static_cast<double>(trim)), thinDir, thinFace);
				 return cross(capEdge, corner - capRight).sign() == -thickSide;
			};

			std::int64_t trim = trimByLocal[thin];
			if (clearOfCap(trim)) {
				continue;
			}
			double tParam = 0.0;
			if (lineIntersectParam(offsetPoint(vertex, thinDir, thinFace), thinDir, capRight, capEdge, tParam)) {
				trim = std::max(trim, static_cast<std::int64_t>(std::ceil(tParam * length(thinDir))));
			}
			while (!clearOfCap(trim)) {
				++trim;
			}
			trimByLocal[thin] = trim;
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

		// How far apart two rings meeting at a junction may put what is one point,
		// or a point on the other's edge. Shared edges coincide exactly except where
		// simplification dropped a band corner from a junction polygon (it stays
		// within kDefaultSimplifyEpsMm of the kept edge), a thinner band's cap lies
		// along a thicker one's, or the two band corners cut back to one miter apex
		// each round about a millimetre from it, in different directions.
		constexpr std::int64_t kSeamToleranceMm = 3;

		// The shortest opening or run of wall between openings that is kept: twice
		// the seam tolerance, so a slot or a stub is never close enough to its
		// neighbour for the outline to fold it shut, and a few millimetres of
		// either is not a wall feature anyway.
		constexpr std::int64_t kMinPieceFeatureMm = 2 * kSeamToleranceMm;

		// The cutbacks {at a, at b} of each solid run of a segment's trimmed band
		// between its gaps, in order along the segment. A gap opening less than
		// kMinPieceFeatureMm of the trimmed band is ignored, and a run shorter than
		// that beside a gap is dropped. Worked from the segment's lexicographically
		// smaller endpoint, with a distance from the other end taken as the rounded
		// remainder of the length, so listing the segment either way round gives the
		// same integer cutbacks and, through trimmedBand, the same corners.
		std::vector<std::pair<std::int64_t, std::int64_t>> solidRuns(const WallSegment& segment, const std::vector<WallGap>& segmentGaps,
																	  std::int64_t trimAtA, std::int64_t trimAtB) {
			const double segmentLength = length(segment.b - segment.a);
			const bool	 fromA		   = !(segment.b < segment.a);
			auto		 fromOtherEnd  = [&](std::int64_t distance) {
				  return std::llround(segmentLength - static_cast<double>(distance));
			};

			std::vector<WallGap> gaps;
			gaps.reserve(segmentGaps.size());
			for (const WallGap& gap : segmentGaps) {
				gaps.push_back(fromA ? gap : WallGap{fromOtherEnd(gap.toMm), fromOtherEnd(gap.fromMm)});
			}
			std::sort(gaps.begin(), gaps.end(), [](const WallGap& l, const WallGap& r) {
				return l.fromMm != r.fromMm ? l.fromMm < r.fromMm : l.toMm < r.toMm;
			});

			const std::int64_t trimNear = fromA ? trimAtA : trimAtB;
			const std::int64_t trimFar	= fromA ? trimAtB : trimAtA;
			const double	   bandEnd	= segmentLength - static_cast<double>(trimFar);
			const double	   minimum	= static_cast<double>(kMinPieceFeatureMm);
			auto			   longEnough = [&](std::int64_t cutNear, std::int64_t cutFar) {
				  return segmentLength - static_cast<double>(cutNear + cutFar) >= minimum;
			};

			std::vector<std::pair<std::int64_t, std::int64_t>> runs;
			std::int64_t										solidFrom = trimNear;
			for (const WallGap& gap : gaps) {
				const double opening = std::min(static_cast<double>(gap.toMm), bandEnd) -
									   static_cast<double>(std::max(gap.fromMm, trimNear));
				if (opening < minimum) {
					continue;
				}
				if (gap.fromMm > solidFrom) {
					const std::int64_t cutFar = std::max(trimFar, fromOtherEnd(gap.fromMm));
					if (longEnough(solidFrom, cutFar)) {
						runs.emplace_back(solidFrom, cutFar);
					}
				}
				solidFrom = std::max(solidFrom, gap.toMm);
			}
			if (solidFrom == trimNear || longEnough(solidFrom, trimFar)) {
				runs.emplace_back(solidFrom, trimFar);
			}
			if (!fromA) {
				for (auto& [cutNear, cutFar] : runs) {
					std::swap(cutNear, cutFar);
				}
			}
			return runs;
		}

		// A returned ring taking part in the outline: its vertices (snapped where
		// rings meet), the segment each edge belongs to, and the vertices of
		// neighbouring rings that lie on each edge.
		struct OutlineTile {
			Ring							  points;
			std::vector<std::size_t>		  edgeSegment;
			std::vector<std::vector<Vec2i64>> splits;
		};

		// Vertices of the rings meeting at one junction that lie within the
		// tolerance of each other (chained) all move to the lowest of them, so a
		// corner rounded a millimetre differently in two rings is one point.
		void snapTogether(std::vector<OutlineTile>& tiles, const std::vector<std::size_t>& meeting) {
			std::vector<std::pair<std::size_t, std::size_t>> corners;
			std::vector<Vec2i64>							 points;
			for (const std::size_t tile : meeting) {
				for (std::size_t v = 0; v < tiles[tile].points.size(); ++v) {
					corners.emplace_back(tile, v);
					points.push_back(tiles[tile].points[v]);
				}
			}
			std::vector<std::size_t> root(points.size());
			for (std::size_t i = 0; i < root.size(); ++i) {
				root[i] = i;
			}
			auto find = [&](std::size_t i) {
				while (root[i] != i) {
					root[i] = root[root[i]];
					i		= root[i];
				}
				return i;
			};
			for (std::size_t i = 0; i < points.size(); ++i) {
				for (std::size_t k = i + 1; k < points.size(); ++k) {
					const Vec2i64 apart = points[k] - points[i];
					if (!(Int128(kSeamToleranceMm * kSeamToleranceMm) < dot(apart, apart))) {
						root[find(k)] = find(i);
					}
				}
			}
			std::map<std::size_t, Vec2i64> lowest;
			for (std::size_t i = 0; i < points.size(); ++i) {
				const auto [entry, added] = lowest.emplace(find(i), points[i]);
				if (!added && points[i] < entry->second) {
					entry->second = points[i];
				}
			}
			for (std::size_t i = 0; i < points.size(); ++i) {
				tiles[corners[i].first].points[corners[i].second] = lowest[find(i)];
			}
		}

		void collectSplits(OutlineTile& tile, const Ring& other) {
			const Ring&		  ring = tile.points;
			const std::size_t n	   = ring.size();
			for (std::size_t e = 0; e < n; ++e) {
				const Vec2i64& from	 = ring[e];
				const Vec2i64& to	 = ring[(e + 1) % n];
				const Vec2i64  along = to - from;
				for (const Vec2i64& point : other) {
					if (from == to || point == from || point == to) {
						continue;
					}
					const Int128 reach = dot(point - from, along);
					if (reach.sign() <= 0 || !(reach < dot(along, along))) {
						continue;
					}
					if (withinDistanceOfSegment(point, from, to, kSeamToleranceMm)) {
						tile.splits[e].push_back(point);
					}
				}
			}
		}

		// The incident segment a junction polygon edge belongs to: the one whose
		// direction is nearest the edge's midpoint as seen from the vertex.
		std::size_t nearestIncident(const Vec2i64& vertex, const Vec2i64& from, const Vec2i64& to,
									const std::vector<std::pair<Vec2i64, std::size_t>>& directions) {
			const Vec2i64 midpointTwice = from + to - vertex - vertex;
			std::size_t	  best			= 0;
			double		  bestScore		= 0.0;
			for (std::size_t i = 0; i < directions.size(); ++i) {
				const Vec2i64& direction = directions[i].first;
				const double   score	 = dot(midpointTwice, direction).toDouble() / length(direction);
				const bool	   better	 = i == 0 || score > bestScore ||
									   (score == bestScore && angleLess(direction, directions[best].first));
				if (better) {
					best	  = i;
					bestScore = score;
				}
			}
			return directions[best].second;
		}

		// Clockwise turn in (0, 2 pi] from `back` (the incoming edge reversed) to
		// `out`; a turn straight back counts as a full turn.
		double clockwiseTurn(const Vec2i64& back, const Vec2i64& out) {
			constexpr double kFullTurn		  = 6.283185307179586;
			const double	 counterClockwise = std::atan2(cross(back, out).toDouble(), dot(back, out).toDouble());
			const double	 turn			  = counterClockwise > 0.0 ? kFullTurn - counterClockwise : -counterClockwise;
			return turn > 0.0 ? turn : kFullTurn;
		}

		// The boundary of the union of the tiles: every edge split at the vertices
		// collected on it, edges snapped to nothing dropped, the edges two tiles
		// share (one in each direction) cancelled, and the rest walked into closed
		// loops. Each loop starts at the smallest remaining edge; where a loop passes
		// a vertex twice it takes the sharpest clockwise turn, which keeps to the
		// region on its left.
		std::vector<OutlineEdge> outlineLoops(const std::vector<OutlineTile>& tiles, const std::vector<WallSegment>& segments) {
			std::vector<OutlineEdge> edges;
			for (const OutlineTile& tile : tiles) {
				const Ring&		  ring = tile.points;
				const std::size_t n	   = ring.size();
				for (std::size_t e = 0; e < n; ++e) {
					const Vec2i64& from	   = ring[e];
					const Vec2i64  along   = ring[(e + 1) % n] - from;
					if (along == Vec2i64{0, 0}) {
						continue;
					}
					const std::size_t segment = tile.edgeSegment[e];
					std::vector<Vec2i64> points = tile.splits[e];
					std::sort(points.begin(), points.end(), [&](const Vec2i64& l, const Vec2i64& r) {
						const Int128 reachL = dot(l - from, along);
						const Int128 reachR = dot(r - from, along);
						return reachL != reachR ? reachL < reachR : l < r;
					});
					points.erase(std::unique(points.begin(), points.end()), points.end());
					points.push_back(ring[(e + 1) % n]);
					Vec2i64 start = from;
					for (const Vec2i64& point : points) {
						edges.push_back({start, point, segment});
						start = point;
					}
				}
			}

			auto byEnds = [](const OutlineEdge& l, const OutlineEdge& r) {
				return l.from != r.from ? l.from < r.from : l.to < r.to;
			};
			// Where two edges coincide, which one cancels decides the segment the
			// survivor belongs to; order them by their segments' endpoints, not by
			// index, so that does not depend on how the segments were listed.
			auto segmentEnds = [&](std::size_t index) {
				const WallSegment& seg = segments[index];
				return seg.a < seg.b ? std::pair{seg.a, seg.b} : std::pair{seg.b, seg.a};
			};
			std::sort(edges.begin(), edges.end(), [&](const OutlineEdge& l, const OutlineEdge& r) {
				return byEnds(l, r) || (!byEnds(r, l) && segmentEnds(l.index) < segmentEnds(r.index));
			});
			std::vector<std::uint8_t> cancelled(edges.size(), 0);
			for (std::size_t i = 0; i < edges.size(); ++i) {
				if (cancelled[i] != 0) {
					continue;
				}
				const OutlineEdge reverse{edges[i].to, edges[i].from};
				for (auto match = std::lower_bound(edges.begin(), edges.end(), reverse, byEnds);
					 match != edges.end() && match->from == reverse.from && match->to == reverse.to; ++match) {
					const std::size_t j = static_cast<std::size_t>(match - edges.begin());
					if (cancelled[j] == 0) {
						cancelled[i] = 1;
						cancelled[j] = 1;
						break;
					}
				}
			}
			std::vector<OutlineEdge> remaining;
			for (std::size_t i = 0; i < edges.size(); ++i) {
				if (cancelled[i] == 0) {
					remaining.push_back(edges[i]);
				}
			}

			std::vector<OutlineEdge>  loops;
			std::vector<std::uint8_t> used(remaining.size(), 0);
			loops.reserve(remaining.size());
			for (std::size_t start = 0; start < remaining.size(); ++start) {
				std::size_t current = start;
				while (used[current] == 0) {
					used[current] = 1;
					loops.push_back(remaining[current]);
					const OutlineEdge& edge = remaining[current];
					if (edge.to == remaining[start].from) {
						break;
					}
					double bestTurn = 0.0;
					const auto firstOut = std::lower_bound(remaining.begin(), remaining.end(), edge.to,
														   [](const OutlineEdge& e, const Vec2i64& p) { return e.from < p; });
					for (std::size_t next = static_cast<std::size_t>(firstOut - remaining.begin());
						 next < remaining.size() && remaining[next].from == edge.to; ++next) {
						if (used[next] != 0) {
							continue;
						}
						const double turn = clockwiseTurn(edge.from - edge.to, remaining[next].to - edge.to);
						if (bestTurn == 0.0 || turn < bestTurn) {
							bestTurn = turn;
							current	 = next;
						}
					}
				}
			}
			return loops;
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

			const JunctionResolution resolution = resolveJunction(vertex, incidents, miterLimit);
			if (resolution.status != OffsetStatus::Ok) {
				result.status = resolution.status;
				return result;
			}

			for (const JunctionTrim& t : resolution.trims) {
				const Incidence& inc = incidences[t.index];
				if (inc.atA) {
					trimA[inc.segIndex] = std::max(trimA[inc.segIndex], t.trimMm);
				} else {
					trimB[inc.segIndex] = std::max(trimB[inc.segIndex], t.trimMm);
				}
			}

			if (!resolution.polygon.empty()) {
				Ring poly = resolution.polygon;
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

		result.trimAtA = std::move(trimA);
		result.trimAtB = std::move(trimB);

		return result;
	}

	WallPiecesAndOutline wallPiecesAndOutline(const std::vector<WallSegment>& segments,
											  const std::vector<std::vector<WallGap>>& gaps, const WallBands& bands) {
		WallPiecesAndOutline result;
		const std::size_t	 segCount = segments.size();
		assert(bands.status == OffsetStatus::Ok && bands.bands.size() == segCount);
		assert(gaps.empty() || gaps.size() == segCount);
		const std::vector<WallGap> noGaps;

		// Pieces: each trimmed band less its gaps. A run that still makes no valid
		// ring after rounding is dropped too.
		constexpr std::size_t kNoTile = static_cast<std::size_t>(-1);
		std::vector<std::size_t> endTileA(segCount, kNoTile);
		std::vector<std::size_t> endTileB(segCount, kNoTile);
		result.pieces.resize(segCount);
		for (std::size_t i = 0; i < segCount; ++i) {
			const WallSegment& seg		 = segments[i];
			const std::int64_t trimAtA = bands.trimAtA[i];
			const std::int64_t trimAtB = bands.trimAtB[i];
			for (const auto& [atA, atB] : solidRuns(seg, gaps.empty() ? noGaps : gaps[i], trimAtA, trimAtB)) {
				Ring ring;
				if (!trimmedBand(seg.a, seg.b, seg.halfThicknessMm, atA, atB, ring)) {
					continue;
				}
				simplifyRing(ring, kDefaultSimplifyEpsMm);
				if (validateAndOrient(ring) != OffsetStatus::Ok) {
					continue;
				}
				if (atA == trimAtA) {
					endTileA[i] = result.pieces[i].size();
				}
				if (atB == trimAtB) {
					endTileB[i] = result.pieces[i].size();
				}
				result.pieces[i].push_back(std::move(ring));
			}
		}

		// The junctions again, as resolveWallBands grouped them, and where each
		// polygon sits.
		struct Incidence {
			std::size_t segIndex;
			bool		atA;
		};
		std::map<Vec2i64, std::vector<Incidence>> byVertex;
		for (std::size_t i = 0; i < segCount; ++i) {
			byVertex[segments[i].a].push_back({i, true});
			byVertex[segments[i].b].push_back({i, false});
		}
		std::map<Vec2i64, std::size_t> polygonAt;
		for (std::size_t j = 0; j < bands.junctions.size(); ++j) {
			const WallSegment& first  = segments[bands.junctionSegments[j][0]];
			const WallSegment& second = segments[bands.junctionSegments[j][1]];
			polygonAt[(first.a == second.a || first.a == second.b) ? first.a : first.b] = j;
		}

		// Outline tiles: every piece, then every junction polygon.
		std::vector<OutlineTile>			  tiles;
		std::vector<std::vector<std::size_t>> pieceTile(segCount);
		for (std::size_t i = 0; i < segCount; ++i) {
			for (const Ring& piece : result.pieces[i]) {
				pieceTile[i].push_back(tiles.size());
				tiles.push_back({piece, std::vector<std::size_t>(piece.size(), i), std::vector<std::vector<Vec2i64>>(piece.size())});
			}
		}
		std::vector<std::vector<std::size_t>> meetings;
		for (const auto& [vertex, incidences] : byVertex) {
			if (incidences.size() < 2) {
				continue;
			}
			// The rings meeting here: the junction polygon, if any, and each incident
			// segment's piece that reaches this end. Only these can share an edge.
			std::vector<std::size_t> meeting;
			const auto				 polygon = polygonAt.find(vertex);
			if (polygon != polygonAt.end()) {
				const Ring& ring = bands.junctions[polygon->second];
				std::vector<std::pair<Vec2i64, std::size_t>> directions;
				for (const Incidence& inc : incidences) {
					const WallSegment& seg = segments[inc.segIndex];
					directions.emplace_back((inc.atA ? seg.b : seg.a) - vertex, inc.segIndex);
				}
				OutlineTile tile{ring, {}, std::vector<std::vector<Vec2i64>>(ring.size())};
				for (std::size_t e = 0; e < ring.size(); ++e) {
					tile.edgeSegment.push_back(nearestIncident(vertex, ring[e], ring[(e + 1) % ring.size()], directions));
				}
				meeting.push_back(tiles.size());
				tiles.push_back(std::move(tile));
			}
			for (const Incidence& inc : incidences) {
				const std::size_t end = inc.atA ? endTileA[inc.segIndex] : endTileB[inc.segIndex];
				if (end != kNoTile) {
					meeting.push_back(pieceTile[inc.segIndex][end]);
				}
			}
			snapTogether(tiles, meeting);
			meetings.push_back(std::move(meeting));
		}
		// A ring's own vertices split its edges too: a corner a step leaves within
		// the tolerance of the polygon's edge along the thicker cap has to split that
		// edge as it splits the band's cap, or the two would not cancel.
		for (const std::vector<std::size_t>& meeting : meetings) {
			for (const std::size_t tile : meeting) {
				for (const std::size_t other : meeting) {
					collectSplits(tiles[tile], tiles[other].points);
				}
			}
		}
		result.outline = outlineLoops(tiles, segments);

		return result;
	}

} // namespace geometry

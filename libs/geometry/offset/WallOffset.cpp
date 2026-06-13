#include "WallOffset.h"

#include "../predicates/Predicates.h"

#include <algorithm>
#include <cmath>
#include <map>

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
		// parameter t along (p0, d0) of the intersection, or nullopt when the
		// directions are parallel. Float; the caller rounds the resulting point.
		bool lineIntersectParam(const Vec2i64& p0, const Vec2i64& d0, const Vec2i64& p1, const Vec2i64& d1, double& t) {
			const double denom = static_cast<double>(d0.x) * static_cast<double>(d1.y) -
								 static_cast<double>(d0.y) * static_cast<double>(d1.x);
			if (denom == 0.0) {
				return false;
			}
			const double wx = static_cast<double>(p1.x - p0.x);
			const double wy = static_cast<double>(p1.y - p0.y);
			t = (wx * static_cast<double>(d1.y) - wy * static_cast<double>(d1.x)) / denom;
			return true;
		}

		double angleOf(const Vec2i64& dir) {
			return std::atan2(static_cast<double>(dir.y), static_cast<double>(dir.x));
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
		// pairs; each wedge's apex is where the two facing band edges meet.
		std::vector<std::size_t> order(degree);
		for (std::size_t i = 0; i < degree; ++i) {
			order[i] = i;
		}
		std::vector<double> ang(degree);
		for (std::size_t i = 0; i < degree; ++i) {
			ang[i] = angleOf(incidents[i].direction - vertex);
		}
		std::sort(order.begin(), order.end(), [&](std::size_t l, std::size_t r) { return ang[l] < ang[r]; });

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

		std::sort(pts.begin(), pts.end(), [&](const Vec2i64& l, const Vec2i64& r) {
			return std::atan2(static_cast<double>(l.y - vertex.y), static_cast<double>(l.x - vertex.x)) <
				   std::atan2(static_cast<double>(r.y - vertex.y), static_cast<double>(r.x - vertex.x));
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
		if (ring.size() <= 3) {
			return;
		}
		bool changed = true;
		while (changed && ring.size() > 3) {
			changed = false;
			for (std::size_t cur = 0; cur < ring.size(); ++cur) {
				const std::size_t prev = (cur + ring.size() - 1) % ring.size();
				const std::size_t next = (cur + 1) % ring.size();
				const Vec2i64&	  p	   = ring[prev];
				const Vec2i64&	  c	   = ring[cur];
				const Vec2i64&	  q	   = ring[next];

				// Collinear with neighbors, or within epsilon of the prev->next
				// segment (a sub-mm sliver from offset rounding): drop it.
				const bool collinear = orientation(p, c, q) == Orientation::Collinear;
				const bool sliver	 = withinDistanceOfSegment(c, p, q, epsilonMm);
				if (collinear || sliver) {
					ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(cur));
					changed = true;
					break;
				}
			}
		}
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
				result.junctions.push_back(std::move(poly));
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

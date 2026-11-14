#include "WallOffset.h"

#include "../predicates/Predicates.h"

#include <algorithm>
#include <array>
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

		// The band a->b cut back trimAtA mm from a and trimAtB mm from b, as its
		// corners {a-left, b-left, b-right, a-right}.
		std::array<Vec2i64, 4> bandCorners(const Vec2i64& a, const Vec2i64& b, std::int64_t halfThicknessMm, std::int64_t trimAtA,
										   std::int64_t trimAtB) {
			const Vec2i64 dir		 = b - a;
			const double  half		 = static_cast<double>(halfThicknessMm);
			const Vec2i64 trimmedA = advance(a, dir, static_cast<double>(trimAtA));
			const Vec2i64 trimmedB = advance(b, -dir, static_cast<double>(trimAtB));
			return {offsetPoint(trimmedA, dir, half), offsetPoint(trimmedB, dir, half), offsetPoint(trimmedB, dir, -half),
					offsetPoint(trimmedA, dir, -half)};
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

		const std::array<Vec2i64, 4> corners = bandCorners(a, b, halfThicknessMm, trimAtA, trimAtB);
		out.assign(corners.begin(), corners.end());
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

		std::vector<Vec2i64> dirs(degree);
		std::vector<double>	 halves(degree);
		for (std::size_t k = 0; k < degree; ++k) {
			dirs[k]	  = incidents[order[k]].direction - vertex;
			halves[k] = static_cast<double>(incidents[order[k]].halfThicknessMm);
		}
		const double maxHalf = *std::max_element(halves.begin(), halves.end());

		// Cutbacks by CCW position. A segment takes the largest any of its wedges
		// asks for, so every wedge is cleared.
		std::vector<std::int64_t> trims(degree, 0);

		// side +1 is the left face, -1 the right.
		auto cornerAt = [&](std::size_t k, std::int64_t trim, double side) {
			return offsetPoint(advance(vertex, dirs[k], static_cast<double>(trim)), dirs[k], side * halves[k]);
		};
		auto corner = [&](std::size_t k, double side) { return cornerAt(k, trims[k], side); };
		// How far along `dir` from the vertex a point sits, rounded; 0 when behind.
		auto reachAlong = [&](const Vec2i64& point, const Vec2i64& dir) -> std::int64_t {
			const double len = length(dir);
			if (len == 0.0) {
				return 0;
			}
			const double proj = (static_cast<double>(point.x - vertex.x) * static_cast<double>(dir.x) +
								 static_cast<double>(point.y - vertex.y) * static_cast<double>(dir.y)) /
								len;
			return proj > 0.0 ? std::llround(proj) : 0;
		};

		// One wedge per CCW-adjacent pair (k -> k+1, wrapping): segment k's LEFT face
		// faces segment k+1's RIGHT face across it.
		enum class WedgeJoin { Straight, Miter, Step };
		struct Wedge {
			WedgeJoin join			 = WedgeJoin::Straight;
			bool	  convex		 = false; // the faces converge: wedge under 180 degrees
			bool	  beveled		 = false; // outer miter past the limit, squared off
			bool	  thickerOnRight = false; // Step: k+1 is the band whose cap the step runs along
			Vec2i64	  point;				  // Miter: where the faces meet. Step: where the thinner face meets that cap
		};
		std::vector<Wedge> wedges(degree);

		for (std::size_t k = 0; k < degree; ++k) {
			const std::size_t right = (k + 1) % degree;
			Wedge&			  wedge = wedges[k];
			const Vec2i64&	  dirL	= dirs[k];
			const Vec2i64&	  dirR	= dirs[right];
			const Int128	  turn	= cross(dirL, dirR);

			if (turn.sign() == 0) {
				// Straight continuation: the faces are parallel. Equal halves abut; a
				// thinner band steps out to a thicker one along its cap.
				if (halves[k] != halves[right]) {
					wedge.join			 = WedgeJoin::Step;
					wedge.convex		 = true;
					wedge.thickerOnRight = halves[right] > halves[k];
				}
				continue;
			}

			// Where the faces meet, as the sign of the parameter along each face from
			// its point beside the vertex (exact: both are 128-bit cross products).
			wedge.convex			 = turn.sign() > 0;
			const Vec2i64 faceL		 = offsetPoint(vertex, dirL, halves[k]);
			const Vec2i64 faceR		 = offsetPoint(vertex, dirR, -halves[right]);
			const Vec2i64 between	 = faceR - faceL;
			const int	  alongLeft	 = cross(between, dirR).sign() * turn.sign();
			const int	  alongRight = cross(between, dirL).sign() * turn.sign();

			// Converging faces meet ahead of both bands (inner miter); diverging faces
			// meet behind both (outer miter). Otherwise the bands differ in thickness
			// and the faces only meet behind the thicker band's cap, as far off as the
			// join is near straight: the thinner face stops at that cap instead.
			const bool miter = wedge.convex ? (alongLeft >= 0 && alongRight >= 0) : (alongLeft <= 0 && alongRight <= 0);
			if (!miter) {
				wedge.join			 = WedgeJoin::Step;
				wedge.thickerOnRight = wedge.convex ? (alongRight < 0) : (alongRight > 0);
				continue;
			}

			double tParam = 0.0;
			lineIntersectParam(faceL, dirL, faceR, dirR, tParam);
			wedge.join	= WedgeJoin::Miter;
			wedge.point = roundVec(static_cast<double>(faceL.x) + tParam * static_cast<double>(dirL.x),
								   static_cast<double>(faceL.y) + tParam * static_cast<double>(dirL.y));
			if (wedge.convex) {
				// The inner faces overlap up to the apex; each band cuts back to it.
				trims[k]	 = std::max(trims[k], reachAlong(wedge.point, dirL));
				trims[right] = std::max(trims[right], reachAlong(wedge.point, dirR));
			} else {
				wedge.beveled = maxHalf > 0.0 && length(wedge.point - vertex) > miterLimit * maxHalf;
			}
		}

		// A step's two bands: the thinner one, whose face meets the thicker one's cap
		// line, and the side of each band that faces the wedge.
		struct StepBands {
			std::size_t thin	  = 0;
			std::size_t thick	  = 0;
			double		thinSide  = 1.0;
			double		thickSide = -1.0;
		};
		auto stepBands = [&](std::size_t k) {
			const bool thickerOnRight = wedges[k].thickerOnRight;
			const std::size_t right	  = (k + 1) % degree;
			return StepBands{thickerOnRight ? k : right, thickerOnRight ? right : k, thickerOnRight ? 1.0 : -1.0,
							 thickerOnRight ? -1.0 : 1.0};
		};

		// Whether a point is clear of the thicker band: on or behind the line of its
		// rounded cap corners, never a fraction of a mm into the band.
		auto clearOfCap = [&](const Vec2i64& p, std::size_t thick) {
			const Vec2i64 capRight = corner(thick, -1.0);
			const Vec2i64 capEdge  = corner(thick, 1.0) - capRight;
			return cross(capEdge, p - capRight).sign() != cross(capEdge, dirs[thick]).sign();
		};

		// The unrounded step point: the thinner face, crossing the thicker cap line.
		struct RealPoint {
			double x = 0.0;
			double y = 0.0;
		};
		auto stepPoint = [&](std::size_t k) -> RealPoint {
			const StepBands bands = stepBands(k);
			const Vec2i64	face  = offsetPoint(vertex, dirs[bands.thin], bands.thinSide * halves[bands.thin]);
			const Vec2i64	cap	  = advance(vertex, dirs[bands.thick], static_cast<double>(trims[bands.thick]));
			double			tParam = 0.0;
			if (!lineIntersectParam(face, dirs[bands.thin], cap, leftNormalUnscaled(dirs[bands.thick]), tParam)) {
				const Vec2i64 thickCorner = corner(bands.thick, bands.thickSide);
				return {static_cast<double>(thickCorner.x), static_cast<double>(thickCorner.y)};
			}
			return {static_cast<double>(face.x) + tParam * static_cast<double>(dirs[bands.thin].x),
					static_cast<double>(face.y) + tParam * static_cast<double>(dirs[bands.thin].y)};
		};

		// The cutback a converging step asks of the thinner band: up to the step
		// point, rounded up, and further while its rounded corner still pokes into
		// the thicker band.
		auto stepReach = [&](std::size_t k) -> std::int64_t {
			const StepBands bands = stepBands(k);
			const RealPoint point = stepPoint(k);
			const Vec2i64&	dir	  = dirs[bands.thin];
			const double	proj  = ((point.x - static_cast<double>(vertex.x)) * static_cast<double>(dir.x) +
								 (point.y - static_cast<double>(vertex.y)) * static_cast<double>(dir.y)) /
								length(dir);
			std::int64_t reach = proj > 0.0 ? static_cast<std::int64_t>(std::ceil(proj)) : 0;
			for (int nudge = 0; nudge < 2 && !clearOfCap(cornerAt(bands.thin, reach, bands.thinSide), bands.thick); ++nudge) {
				++reach;
			}
			return reach;
		};

		// A converging step cuts the thinner band back to the thicker one's cap. The
		// cap moves when the thicker band is cut by another wedge, so settle until
		// no cutback grows (each pass can only grow one, at most degree passes).
		for (std::size_t pass = 0; pass < degree; ++pass) {
			bool grew = false;
			for (std::size_t k = 0; k < degree; ++k) {
				const Wedge& wedge = wedges[k];
				if (wedge.join != WedgeJoin::Step || !wedge.convex) {
					continue;
				}
				const std::size_t  thin	 = stepBands(k).thin;
				const std::int64_t reach = stepReach(k);
				if (reach > trims[thin]) {
					trims[thin] = reach;
					grew		= true;
				}
			}
			if (!grew) {
				break;
			}
		}

		// Final step points against the settled cutbacks. Where the step set the
		// thinner band's cutback, use that band's own corner so the two weld exactly;
		// otherwise round to the neighbouring mm point clear of the thicker band.
		for (std::size_t k = 0; k < degree; ++k) {
			Wedge& wedge = wedges[k];
			if (wedge.join != WedgeJoin::Step) {
				continue;
			}
			const StepBands bands = stepBands(k);
			if (wedge.convex && stepReach(k) == trims[bands.thin]) {
				wedge.point = corner(bands.thin, bands.thinSide);
				continue;
			}
			const RealPoint point		 = stepPoint(k);
			double			bestDistance = -1.0;
			wedge.point					 = roundVec(point.x, point.y);
			for (const double x : {std::floor(point.x), std::ceil(point.x)}) {
				for (const double y : {std::floor(point.y), std::ceil(point.y)}) {
					const Vec2i64 candidate{static_cast<std::int64_t>(x), static_cast<std::int64_t>(y)};
					const double  distance = (x - point.x) * (x - point.x) + (y - point.y) * (y - point.y);
					if (clearOfCap(candidate, bands.thick) && (bestDistance < 0.0 || distance < bestDistance)) {
						wedge.point	 = candidate;
						bestDistance = distance;
					}
				}
			}
		}

		// The polygon, walked CCW about the vertex: each band's trimmed cap (shared
		// with the band, so a seam, not outline), then the wedge's join to the next
		// band. A step stands in for the thicker band's corner on its side, so the
		// polygon covers only the part of that cap the thinner band meets.
		auto rightPoint = [&](std::size_t k) {
			const Wedge& before = wedges[(k + degree - 1) % degree];
			return (before.join == WedgeJoin::Step && before.thickerOnRight) ? before.point : corner(k, -1.0);
		};
		auto leftPoint = [&](std::size_t k) {
			const Wedge& after = wedges[k];
			return (after.join == WedgeJoin::Step && !after.thickerOnRight) ? after.point : corner(k, 1.0);
		};
		auto outlineEdge = [&](const Vec2i64& from, const Vec2i64& to, std::size_t k) {
			if (from != to) {
				result.outline.push_back({from, to, incidents[order[k]].index});
			}
		};

		// A straight run of two has no area to fill: only its steps are outline.
		const bool straightRun = degree == 2 && cross(dirs[0], dirs[1]).sign() == 0;

		Ring polygon;
		auto append = [&](const Vec2i64& p) {
			if (polygon.empty() || polygon.back() != p) {
				polygon.push_back(p);
			}
		};
		for (std::size_t k = 0; k < degree; ++k) {
			const std::size_t right = (k + 1) % degree;
			const Wedge&	  wedge = wedges[k];
			const Vec2i64	  from	= leftPoint(k);
			const Vec2i64	  to	= rightPoint(right);
			append(rightPoint(k));
			append(from);
			switch (wedge.join) {
				case WedgeJoin::Straight:
					if (!straightRun) {
						outlineEdge(from, to, k);
					}
					break;
				case WedgeJoin::Miter:
					if (wedge.convex || !wedge.beveled) {
						append(wedge.point);
						outlineEdge(from, wedge.point, k);
						outlineEdge(wedge.point, to, right);
					} else {
						outlineEdge(from, to, k);
					}
					break;
				case WedgeJoin::Step: {
					const std::size_t thin	= wedge.thickerOnRight ? k : right;
					const std::size_t thick = wedge.thickerOnRight ? right : k;
					append(wedge.point);
					outlineEdge(from, to, thin);
					outlineEdge(wedge.point, corner(thick, wedge.thickerOnRight ? -1.0 : 1.0), thick);
					break;
				}
			}
		}
		if (polygon.size() >= 2 && polygon.front() == polygon.back()) {
			polygon.pop_back();
		}
		if (!straightRun && polygon.size() >= 3 && signedAreaDoubled(polygon).sign() != 0) {
			result.polygon = std::move(polygon);
		}

		std::vector<std::int64_t> trimByLocal(degree, 0);
		for (std::size_t k = 0; k < degree; ++k) {
			trimByLocal[order[k]] = trims[k];
		}
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

		// The spans of a centerline of `segLength` mm left solid between `gaps`, as
		// [from, to] mm from the segment's `a` end. Gaps may overlap or run past
		// either end.
		std::vector<std::pair<double, double>> openRuns(std::vector<WallGap> gaps, double segLength) {
			std::sort(gaps.begin(), gaps.end(), [](const WallGap& l, const WallGap& r) { return l.fromMm < r.fromMm; });
			std::vector<std::pair<double, double>> runs;
			double								   solidFrom = 0.0;
			for (const WallGap& gap : gaps) {
				const double gapFrom = std::clamp(static_cast<double>(gap.fromMm), 0.0, segLength);
				const double gapTo	 = std::clamp(static_cast<double>(gap.toMm), 0.0, segLength);
				if (gapTo <= gapFrom) {
					continue;
				}
				if (gapFrom > solidFrom) {
					runs.emplace_back(solidFrom, gapFrom);
				}
				solidFrom = std::max(solidFrom, gapTo);
			}
			if (solidFrom < segLength) {
				runs.emplace_back(solidFrom, segLength);
			}
			return runs;
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

		// Per segment, the trim distance accumulated at each end, and whether that
		// end is free (no other segment there), so its cap is outline.
		std::vector<std::int64_t> trimA(segCount, 0);
		std::vector<std::int64_t> trimB(segCount, 0);
		std::vector<std::uint8_t> freeA(segCount, 0);
		std::vector<std::uint8_t> freeB(segCount, 0);

		// Resolve each junction; collect its polygon and outline and fold trims into
		// the per-segment per-end accumulators.
		for (const auto& [vertex, incidences] : byVertex) {
			if (incidences.size() == 1) {
				(incidences.front().atA ? freeA : freeB)[incidences.front().segIndex] = 1;
			}
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
			for (const OutlineEdge& edge : jr.outline) {
				result.outline.push_back({edge.from, edge.to, incidences[edge.index].segIndex});
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

		// Pieces: each band cut at its segment's gaps, with its outline. A piece's
		// faces are always outline; an end is outline where it opens onto a gap or
		// is a free end, and a seam where it meets a junction or another band.
		result.pieces.resize(segCount);
		for (std::size_t i = 0; i < segCount; ++i) {
			const WallSegment& seg		  = segments[i];
			const double	   segLength = length(seg.b - seg.a);
			for (const auto& [runFrom, runTo] : openRuns(seg.gaps, segLength)) {
				const std::int64_t atA = std::max(trimA[i], std::llround(runFrom));
				const std::int64_t atB = std::max(trimB[i], std::llround(segLength - runTo));
				Ring			   ring;
				if (!trimmedBand(seg.a, seg.b, seg.halfThicknessMm, atA, atB, ring)) {
					continue;
				}
				simplifyRing(ring, kDefaultSimplifyEpsMm);
				if (validateAndOrient(ring) != OffsetStatus::Ok) {
					continue;
				}
				result.pieces[i].push_back(std::move(ring));

				const auto [aLeft, bLeft, bRight, aRight] = bandCorners(seg.a, seg.b, seg.halfThicknessMm, atA, atB);
				result.outline.push_back({aLeft, bLeft, i});
				result.outline.push_back({bRight, aRight, i});
				if (freeA[i] != 0 || atA > trimA[i]) {
					result.outline.push_back({aRight, aLeft, i});
				}
				if (freeB[i] != 0 || atB > trimB[i]) {
					result.outline.push_back({bLeft, bRight, i});
				}
			}
		}

		return result;
	}

} // namespace geometry

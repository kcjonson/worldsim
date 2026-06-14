#include "ConstructionValidator.h"

#include "ConstructionWorld.h"

#include <assets/ConstructionRegistry.h>

#include <boolean/RingBoolean.h>
#include <offset/WallOffset.h>
#include <polygon/Polygon.h>
#include <predicates/Predicates.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace engine::construction {

	namespace {

		geometry::Ring quantizeRing(const std::vector<::Foundation::Vec2>& meters) {
			geometry::Ring ring;
			ring.reserve(meters.size());
			for (const auto& p : meters) {
				ring.push_back(geometry::quantize(p));
			}
			return ring;
		}

		// Interior angle (degrees) at vertex `b` formed by edges (a->b) and (b->c).
		// Float math from integer mm vectors; thresholds are coarse (~30 deg) so
		// this matches geometry::minInteriorAngle. Returns 0 for a degenerate edge.
		double cornerAngleDegrees(const geometry::Vec2i64& a, const geometry::Vec2i64& b, const geometry::Vec2i64& c) {
			const auto	 u = a - b;
			const auto	 v = c - b;
			const double ux = static_cast<double>(u.x);
			const double uy = static_cast<double>(u.y);
			const double vx = static_cast<double>(v.x);
			const double vy = static_cast<double>(v.y);
			const double lenU = std::sqrt(ux * ux + uy * uy);
			const double lenV = std::sqrt(vx * vx + vy * vy);
			if (lenU == 0.0 || lenV == 0.0) {
				return 0.0;
			}
			double cosA = (ux * vx + uy * vy) / (lenU * lenV);
			cosA = std::clamp(cosA, -1.0, 1.0);
			return std::acos(cosA) * (180.0 / std::numbers::pi);
		}

		// True if the two bands' interiors intersect: a proper edge crossing /
		// collinear overlap between them, or a corner of one strictly inside the
		// other. Rings are CCW 4-corner bands (geometry::band output).
		bool bandsOverlap(const geometry::Ring& p, const geometry::Ring& q) {
			const std::size_t np = p.size();
			const std::size_t nq = q.size();
			for (std::size_t i = 0; i < np; ++i) {
				const auto& a0 = p[i];
				const auto& a1 = p[(i + 1) % np];
				for (std::size_t k = 0; k < nq; ++k) {
					const auto& b0 = q[k];
					const auto& b1 = q[(k + 1) % nq];
					const auto	rel = geometry::intersectSegments(a0, a1, b0, b1).relation;
					if (rel == geometry::SegmentRelation::ProperCrossing || rel == geometry::SegmentRelation::CollinearOverlap) {
						return true;
					}
				}
			}
			// No edge crossing: one band may still sit fully inside the other.
			for (const auto& c : p) {
				if (geometry::pointInPolygon(c, q) == geometry::PointInPolygon::Inside) {
					return true;
				}
			}
			for (const auto& c : q) {
				if (geometry::pointInPolygon(c, p) == geometry::PointInPolygon::Inside) {
					return true;
				}
			}
			return false;
		}

		// Strict min face-to-face distance between two bands is below `thresholdMm`.
		// Probes every corner of each band against the other band's edges (both
		// directions), the same endpoint-probe approximation foundation edge
		// clearance uses; exact at editor scale. Strict-< so a gap exactly equal to
		// the threshold passes, matching minVertexSpacing / minEdgeClearance.
		bool bandsCloserThan(const geometry::Ring& p, const geometry::Ring& q, std::int64_t thresholdMm) {
			const std::size_t np = p.size();
			const std::size_t nq = q.size();
			for (const auto& c : p) {
				for (std::size_t k = 0; k < nq; ++k) {
					if (geometry::closerThanToSegment(c, q[k], q[(k + 1) % nq], thresholdMm)) {
						return true;
					}
				}
			}
			for (const auto& c : q) {
				for (std::size_t i = 0; i < np; ++i) {
					if (geometry::closerThanToSegment(c, p[i], p[(i + 1) % np], thresholdMm)) {
						return true;
					}
				}
			}
			return false;
		}

	} // namespace

	std::string validationReason(ValidationCode code) {
		switch (code) {
			case ValidationCode::Ok:
				return {};
			case ValidationCode::TooFewPoints:
				return "need at least 3 points";
			case ValidationCode::TooManyPoints:
				return "too many points";
			case ValidationCode::VerticesTooClose:
				return "points too close";
			case ValidationCode::AngleTooSharp:
				return "corner too tight";
			case ValidationCode::SelfIntersects:
				return "shape crosses itself";
			case ValidationCode::EdgeClearanceTooSmall:
				return "edges too close";
			case ValidationCode::AreaTooSmall:
				return "area too small";
			case ValidationCode::AreaTooLarge:
				return "area too large";
			case ValidationCode::OverlapsExisting:
				return "overlaps another foundation";
			case ValidationCode::SegmentTooShort:
				return "wall too short";
			case ValidationCode::NotContainedInHostFoundation:
				return "wall off the foundation";
			case ValidationCode::WallsOverlap:
				return "walls overlap";
			case ValidationCode::ParallelClearanceTooSmall:
				return "walls too close";
			case ValidationCode::XCrossing:
				return "walls can't cross";
		}
		return {};
	}

	ValidationResult ConstructionValidator::checkShape(const std::vector<geometry::Vec2i64>& ring, bool closed) const {
		const auto&		  c = *constraints_;
		const std::size_t n = ring.size();

		// Vertex spacing: consecutive vertices, plus the closing pair when closed.
		const std::size_t spacingPairs = closed ? n : (n - 1);
		for (std::size_t i = 0; i < spacingPairs; ++i) {
			const std::size_t j = (i + 1) % n;
			const auto		  d = ring[j] - ring[i];
			const auto		  sq = geometry::dot(d, d);
			if (sq < geometry::Int128::product(c.minVertexSpacingMm, c.minVertexSpacingMm)) {
				const double mm = std::sqrt(sq.toDouble());
				return {ValidationCode::VerticesTooClose, i, j, mm / static_cast<double>(geometry::kMillimetersPerMeter)};
			}
		}

		// Corner angles: every interior vertex; when open, the endpoints have no
		// corner so skip them. When closed, wrap around.
		if (n >= 3) {
			const std::size_t first = closed ? 0 : 1;
			const std::size_t last = closed ? n : (n - 1);
			for (std::size_t i = first; i < last; ++i) {
				const auto&	 a = ring[(i + n - 1) % n];
				const auto&	 b = ring[i % n];
				const auto&	 c2 = ring[(i + 1) % n];
				const double angle = cornerAngleDegrees(a, b, c2);
				if (angle < c.minCornerAngleDegrees) {
					return {ValidationCode::AngleTooSharp, i % n, 0, angle};
				}
			}
		}

		// Self-intersection: any pair of non-adjacent edges that crosses. For the
		// open chain the closing edge is excluded; for the closed ring it is in.
		const std::size_t edgeCount = closed ? n : (n - 1);
		for (std::size_t i = 0; i < edgeCount; ++i) {
			const auto& a0 = ring[i];
			const auto& a1 = ring[(i + 1) % n];
			for (std::size_t k = i + 1; k < edgeCount; ++k) {
				const bool adjacent = (k == i + 1) || (closed && i == 0 && k == edgeCount - 1);
				if (adjacent) {
					continue;
				}
				const auto& b0 = ring[k];
				const auto& b1 = ring[(k + 1) % n];
				const auto	rel = geometry::intersectSegments(a0, a1, b0, b1).relation;
				if (rel != geometry::SegmentRelation::Disjoint) {
					return {ValidationCode::SelfIntersects, i, k, 0.0};
				}
			}
		}

		// Edge clearance: non-adjacent edges must stay at least segmentClearance
		// apart. Only meaningful once the shape has at least 4 edges to have a
		// non-adjacent pair (closed: 4 vertices; open: 5 vertices, since the
		// open chain has edgeCount = n - 1 edges).
		if (edgeCount >= 4) {
			for (std::size_t i = 0; i < edgeCount; ++i) {
				const auto& a0 = ring[i];
				const auto& a1 = ring[(i + 1) % n];
				for (std::size_t k = i + 1; k < edgeCount; ++k) {
					const bool adjacent = (k == i + 1) || (closed && i == 0 && k == edgeCount - 1);
					if (adjacent) {
						continue;
					}
					const auto& b0 = ring[k];
					const auto& b1 = ring[(k + 1) % n];
					// Endpoint distance probes are enough at editor scale: check the
					// four segment-endpoint distances against the clearance. Strict
					// < so a gap exactly equal to segmentClearance passes, matching
					// minVertexSpacing and geometry::minEdgeClearance.
					const bool tooClose = geometry::closerThanToSegment(b0, a0, a1, c.segmentClearanceMm) ||
										  geometry::closerThanToSegment(b1, a0, a1, c.segmentClearanceMm) ||
										  geometry::closerThanToSegment(a0, b0, b1, c.segmentClearanceMm) ||
										  geometry::closerThanToSegment(a1, b0, b1, c.segmentClearanceMm);
					if (tooClose) {
						return {ValidationCode::EdgeClearanceTooSmall, i, k, 0.0};
					}
				}
			}
		}

		return {};
	}

	ValidationResult
	ConstructionValidator::validatePoint(const std::vector<::Foundation::Vec2>& points, ::Foundation::Vec2 candidate) const {
		// First point is always placeable.
		if (points.empty()) {
			return {};
		}

		std::vector<::Foundation::Vec2> chain = points;
		chain.push_back(candidate);

		const auto& c = *constraints_;
		if (static_cast<int>(chain.size()) > c.maxPoints) {
			return {ValidationCode::TooManyPoints, chain.size() - 1, 0, static_cast<double>(chain.size())};
		}

		const geometry::Ring ring = quantizeRing(chain);
		return checkShape(ring, /*closed=*/false);
	}

	ValidationResult ConstructionValidator::validateRing(const std::vector<::Foundation::Vec2>& ringMeters) const {
		if (ringMeters.size() < 3) {
			return {ValidationCode::TooFewPoints, 0, 0, static_cast<double>(ringMeters.size())};
		}

		const auto& c = *constraints_;
		if (static_cast<int>(ringMeters.size()) > c.maxPoints) {
			return {ValidationCode::TooManyPoints, ringMeters.size() - 1, 0, static_cast<double>(ringMeters.size())};
		}

		geometry::Ring ring = quantizeRing(ringMeters);

		const ValidationResult shape = checkShape(ring, /*closed=*/true);
		if (!shape.ok()) {
			return shape;
		}

		// Area, using the exact-ish signed area in m^2 (abs because winding is not
		// yet normalized at draw time).
		const double area = std::abs(geometry::signedAreaSquareMeters(ring));
		if (area < static_cast<double>(c.minAreaSquareMeters)) {
			return {ValidationCode::AreaTooSmall, 0, 0, area};
		}
		if (area > static_cast<double>(c.maxAreaSquareMeters)) {
			return {ValidationCode::AreaTooLarge, 0, 0, area};
		}

		// Overlap against committed foundations. The world's commit path re-checks
		// this with CCW-normalized winding; doing it here gives the same reject at
		// draw time so the user never gets a click that the commit then refuses.
		geometry::ensureCounterClockwise(ring);
		for (const Foundation& other : world_->foundations()) {
			if (geometry::ringsInteriorOverlap(ring, other.ring)) {
				return {ValidationCode::OverlapsExisting, 0, 0, 0.0};
			}
		}

		return {};
	}

	// --- Walls --------------------------------------------------------------

	bool ConstructionValidator::bandContainedInFoundation(
		const geometry::Vec2i64& a,
		const geometry::Vec2i64& b,
		std::int64_t			 halfThickMm,
		FoundationId			 host
	) const {
		if (host == kInvalidFoundation) {
			return true; // no host to contain the wall (freestanding, future tool)
		}
		const Foundation* f = world_->get(host);
		if (f == nullptr) {
			return false;
		}
		// The full-thickness footprint is the band; a zero-thickness preset
		// degenerates the band to the centerline, so probe the two endpoints in that
		// case. A band corner inside-or-on the ring plus no band edge crossing a ring
		// edge means the convex band lies entirely within the (possibly non-convex)
		// host: corners alone miss a band bulging over a concave notch, the
		// edge-cross check catches it.
		geometry::Ring band;
		if (halfThickMm > 0) {
			band = geometry::band(a, b, halfThickMm);
		} else {
			band = {a, b};
		}
		for (const auto& c : band) {
			if (geometry::pointInPolygon(c, f->ring) == geometry::PointInPolygon::Outside) {
				return false;
			}
		}
		const std::size_t nb = band.size();
		const std::size_t nf = f->ring.size();
		for (std::size_t i = 0; i < nb; ++i) {
			const auto& a0 = band[i];
			const auto& a1 = band[(i + 1) % nb];
			for (std::size_t k = 0; k < nf; ++k) {
				const auto& b0 = f->ring[k];
				const auto& b1 = f->ring[(k + 1) % nf];
				if (geometry::intersectSegments(a0, a1, b0, b1).relation == geometry::SegmentRelation::ProperCrossing) {
					return false;
				}
			}
		}
		return true;
	}

	ValidationResult ConstructionValidator::validateWallPoint(
		const std::vector<::Foundation::Vec2>& points,
		::Foundation::Vec2					   candidate,
		const engine::assets::ThicknessPreset& thickness,
		FoundationId						   host
	) const {
		// First point is always placeable (matches foundation validatePoint).
		if (points.empty()) {
			return {};
		}

		const auto&				c = *constraints_;
		const geometry::Vec2i64 prev = geometry::quantize(points.back());
		const geometry::Vec2i64 cand = geometry::quantize(candidate);
		const std::size_t		candIndex = points.size();

		// Min segment length: the new segment prev->candidate. Strict-< so a segment
		// exactly minSegmentLength long passes (matches minVertexSpacing convention).
		const auto d = cand - prev;
		const auto sq = geometry::dot(d, d);
		if (sq < geometry::Int128::product(c.minSegmentLengthMm, c.minSegmentLengthMm)) {
			const double mm = std::sqrt(sq.toDouble());
			return {ValidationCode::SegmentTooShort, candIndex, 0, mm / static_cast<double>(geometry::kMillimetersPerMeter)};
		}

		// Junction angle at the previous vertex, between the prior segment and the
		// new one. Only when there is a prior segment to turn off of.
		if (points.size() >= 2) {
			const geometry::Vec2i64 before = geometry::quantize(points[points.size() - 2]);
			const double			angle = cornerAngleDegrees(before, prev, cand);
			if (angle < c.minWallJunctionAngleDegrees) {
				return {ValidationCode::AngleTooSharp, points.size() - 1, 0, angle};
			}
		}

		// Open chain stays simple: the new segment prev->candidate must not cross any
		// earlier segment of the chain. The last existing segment shares the prev
		// vertex (adjacent) and is exempt; the chain is open, so no closing edge.
		for (std::size_t i = 0; i + 1 < points.size(); ++i) {
			const bool adjacent = (i + 1 == points.size() - 1); // shares the prev vertex
			if (adjacent) {
				continue;
			}
			const geometry::Vec2i64 e0 = geometry::quantize(points[i]);
			const geometry::Vec2i64 e1 = geometry::quantize(points[i + 1]);
			const auto				rel = geometry::intersectSegments(prev, cand, e0, e1).relation;
			if (rel != geometry::SegmentRelation::Disjoint) {
				return {ValidationCode::SelfIntersects, candIndex, i, 0.0};
			}
		}

		// Host containment of the new segment's full-thickness footprint.
		if (!bandContainedInFoundation(prev, cand, thickness.halfThicknessMm, host)) {
			return {ValidationCode::NotContainedInHostFoundation, candIndex, 0, 0.0};
		}

		return {};
	}

	ValidationResult ConstructionValidator::validateWallSegment(
		::Foundation::Vec2					   a,
		::Foundation::Vec2					   b,
		const engine::assets::ThicknessPreset& thickness,
		FoundationId						   host
	) const {
		const auto&				c = *constraints_;
		const geometry::Vec2i64 qa = geometry::quantize(a);
		const geometry::Vec2i64 qb = geometry::quantize(b);

		// Length (strict-< at-threshold passes).
		const auto d = qb - qa;
		const auto sq = geometry::dot(d, d);
		if (sq < geometry::Int128::product(c.minSegmentLengthMm, c.minSegmentLengthMm)) {
			const double mm = std::sqrt(sq.toDouble());
			return {ValidationCode::SegmentTooShort, 0, 0, mm / static_cast<double>(geometry::kMillimetersPerMeter)};
		}

		// Host containment.
		if (!bandContainedInFoundation(qa, qb, thickness.halfThicknessMm, host)) {
			return {ValidationCode::NotContainedInHostFoundation, 0, 0, 0.0};
		}

		// The candidate footprint, built once.
		const std::int64_t	 half = thickness.halfThicknessMm;
		const geometry::Ring candBand = half > 0 ? geometry::band(qa, qb, half) : geometry::Ring{qa, qb};

		for (const WallSegment& s : world_->segments()) {
			const Vertex* sv0 = world_->getVertex(s.v0);
			const Vertex* sv1 = world_->getVertex(s.v1);
			if (sv0 == nullptr || sv1 == nullptr) {
				continue;
			}
			const geometry::Vec2i64& e0 = sv0->pos;
			const geometry::Vec2i64& e1 = sv1->pos;

			const geometry::SegmentRelation rel = geometry::intersectSegments(qa, qb, e0, e1).relation;

			// X-crossing: centerlines properly cross. Report it even though
			// ConstructionWorld also rejects it, so the tool can colorize the reason
			// (no X-crossings in v1; the snap turns these into T-junctions).
			if (rel == geometry::SegmentRelation::ProperCrossing) {
				return {ValidationCode::XCrossing, 0, 0, 0.0};
			}

			// Joined walls meet at a junction and are exempt from overlap /
			// clearance: their bands touch there by design. This covers BOTH a
			// shared endpoint AND a T-junction (one segment's endpoint landing on the
			// other's interior, either direction) -- the world's commitSegment splits
			// the latter into a shared vertex, and SnapEngine snaps to it as a
			// WallSegment hit, so the validator must accept it too. intersectSegments
			// reports exactly these single-point incidences as EndpointTouch, and
			// (critically) reports a genuine parallel overlap as CollinearOverlap, not
			// EndpointTouch, so a parallel overlap is NOT exempted here -- it falls
			// through to the bandsOverlap reject below.
			if (rel == geometry::SegmentRelation::EndpointTouch) {
				continue;
			}

			// Build the existing wall's footprint. Its half-thickness lives only in
			// config, so resolve it from the registry (the production source of
			// truth); an unresolved preset degenerates to the centerline.
			std::int64_t						   otherHalf = 0;
			const engine::assets::ThicknessPreset* preset =
				engine::assets::ConstructionRegistry::Get().getThicknessPreset(s.material, s.thicknessPreset);
			if (preset != nullptr) {
				otherHalf = preset->halfThicknessMm;
			}
			const geometry::Ring otherBand = otherHalf > 0 ? geometry::band(e0, e1, otherHalf) : geometry::Ring{e0, e1};

			if (bandsOverlap(candBand, otherBand)) {
				return {ValidationCode::WallsOverlap, 0, 0, 0.0};
			}
			// Parallel-clearance only applies to (anti-)parallel runs: two walls
			// sitting close alongside each other need the pathing gap between their
			// faces. Walls meeting at an angle are not a parallel run -- a true
			// intersection/overlap is already caught above -- so don't reject them.
			const bool parallel = geometry::cross(d, e1 - e0).sign() == 0;
			if (parallel && bandsCloserThan(candBand, otherBand, c.minParallelClearanceMm)) {
				return {ValidationCode::ParallelClearanceTooSmall, 0, 0, 0.0};
			}
		}

		return {};
	}

} // namespace engine::construction

#include "ConstructionValidator.h"

#include "ConstructionWorld.h"

#include <assets/ConstructionRegistry.h>

#include <boolean/RingBoolean.h>
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
			const auto   u	 = a - b;
			const auto   v	 = c - b;
			const double ux	 = static_cast<double>(u.x);
			const double uy	 = static_cast<double>(u.y);
			const double vx	 = static_cast<double>(v.x);
			const double vy	 = static_cast<double>(v.y);
			const double lenU = std::sqrt(ux * ux + uy * uy);
			const double lenV = std::sqrt(vx * vx + vy * vy);
			if (lenU == 0.0 || lenV == 0.0) {
				return 0.0;
			}
			double cosA = (ux * vx + uy * vy) / (lenU * lenV);
			cosA		= std::clamp(cosA, -1.0, 1.0);
			return std::acos(cosA) * (180.0 / std::numbers::pi);
		}

	} // namespace

	std::string validationReason(ValidationCode code) {
		switch (code) {
			case ValidationCode::Ok:				   return {};
			case ValidationCode::TooFewPoints:		   return "need at least 3 points";
			case ValidationCode::TooManyPoints:		   return "too many points";
			case ValidationCode::VerticesTooClose:	   return "points too close";
			case ValidationCode::AngleTooSharp:		   return "corner too tight";
			case ValidationCode::SelfIntersects:	   return "shape crosses itself";
			case ValidationCode::EdgeClearanceTooSmall: return "edges too close";
			case ValidationCode::AreaTooSmall:		   return "area too small";
			case ValidationCode::AreaTooLarge:		   return "area too large";
			case ValidationCode::OverlapsExisting:	   return "overlaps another foundation";
		}
		return {};
	}

	ValidationResult ConstructionValidator::checkShape(const std::vector<geometry::Vec2i64>& ring, bool closed) const {
		const auto&		  c = *constraints_;
		const std::size_t n = ring.size();

		// Vertex spacing: consecutive vertices, plus the closing pair when closed.
		const std::size_t spacingPairs = closed ? n : (n - 1);
		for (std::size_t i = 0; i < spacingPairs; ++i) {
			const std::size_t j	 = (i + 1) % n;
			const auto		  d	 = ring[j] - ring[i];
			const auto		  sq = geometry::dot(d, d);
			if (sq < geometry::Int128::product(c.minVertexSpacingMm, c.minVertexSpacingMm)) {
				const double mm	 = std::sqrt(sq.toDouble());
				return {ValidationCode::VerticesTooClose, i, j, mm / static_cast<double>(geometry::kMillimetersPerMeter)};
			}
		}

		// Corner angles: every interior vertex; when open, the endpoints have no
		// corner so skip them. When closed, wrap around.
		if (n >= 3) {
			const std::size_t first = closed ? 0 : 1;
			const std::size_t last	= closed ? n : (n - 1);
			for (std::size_t i = first; i < last; ++i) {
				const auto&	 a	   = ring[(i + n - 1) % n];
				const auto&	 b	   = ring[i % n];
				const auto&	 c2	   = ring[(i + 1) % n];
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
				const auto& b0	 = ring[k];
				const auto& b1	 = ring[(k + 1) % n];
				const auto	rel	 = geometry::intersectSegments(a0, a1, b0, b1).relation;
				if (rel != geometry::SegmentRelation::Disjoint) {
					return {ValidationCode::SelfIntersects, i, k, 0.0};
				}
			}
		}

		// Edge clearance: non-adjacent edges must stay at least segmentClearance
		// apart. Only meaningful once the shape has enough edges to have a
		// non-adjacent pair (>= 4 edges closed, >= 4 vertices open).
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
					// four segment-endpoint distances against the clearance.
					const bool tooClose = geometry::withinDistanceOfSegment(b0, a0, a1, c.segmentClearanceMm) ||
										  geometry::withinDistanceOfSegment(b1, a0, a1, c.segmentClearanceMm) ||
										  geometry::withinDistanceOfSegment(a0, b0, b1, c.segmentClearanceMm) ||
										  geometry::withinDistanceOfSegment(a1, b0, b1, c.segmentClearanceMm);
					if (tooClose) {
						return {ValidationCode::EdgeClearanceTooSmall, i, k, 0.0};
					}
				}
			}
		}

		return {};
	}

	ValidationResult ConstructionValidator::validatePoint(const std::vector<::Foundation::Vec2>& points, ::Foundation::Vec2 candidate) const {
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

} // namespace engine::construction

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

		// A snapped edge on a diagonal lands up to ~1 mm off the exact line after
		// quantization; that still counts as touching (the zero-gap case).
		constexpr std::int64_t kFoundationTouchToleranceMm = 2;

		// Two foundations with disjoint interiors must, everywhere, either touch
		// (snapped edge-to-edge) or keep at least `clearanceMm` apart; anything in
		// between is a sliver no colonist can path through. Their boundaries don't
		// cross (interiors are disjoint), so each gap narrows to a vertex of one
		// ring and is probed there, vertex-to-edge, both ways.
		bool foundationsTooClose(const geometry::Ring& a, const geometry::Ring& b, std::int64_t clearanceMm) {
			// Judged per vertex, so touching in one place never excuses a sliver in
			// another: a vertex closer than clearance to the other ring is a violation
			// unless that vertex itself touches it. Where a shared run ends and one
			// edge angles away, the vertex at the end of the run is the touching one.
			auto sliverFrom = [clearanceMm](const geometry::Ring& points, const geometry::Ring& edges) {
				const std::size_t n = edges.size();
				for (const auto& p : points) {
					bool touching = false;
					bool close = false;
					for (std::size_t i = 0; i < n && !touching; ++i) {
						const auto& e0 = edges[i];
						const auto& e1 = edges[(i + 1) % n];
						touching = geometry::withinDistanceOfSegment(p, e0, e1, kFoundationTouchToleranceMm);
						close = close || geometry::closerThanToSegment(p, e0, e1, clearanceMm);
					}
					if (close && !touching) {
						return true;
					}
				}
				return false;
			};
			return sliverFrom(a, b) || sliverFrom(b, a);
		}

		// Is the full-thickness band of centerline a->b entirely inside `ring`? A
		// zero half-thickness degenerates the band to the centerline. A band corner
		// inside-or-on the ring plus no band edge crossing a ring edge means the
		// convex band lies within the (possibly non-convex) ring: corners alone miss
		// a band bulging over a concave notch, the edge-cross check catches it.
		bool bandContainedInRing(const geometry::Vec2i64& a, const geometry::Vec2i64& b, std::int64_t halfThickMm, const geometry::Ring& ring) {
			const geometry::Ring band = halfThickMm > 0 ? geometry::band(a, b, halfThickMm) : geometry::Ring{a, b};
			for (const auto& c : band) {
				if (geometry::pointInPolygon(c, ring) == geometry::PointInPolygon::Outside) {
					return false;
				}
			}
			const std::size_t nb = band.size();
			const std::size_t nr = ring.size();
			for (std::size_t i = 0; i < nb; ++i) {
				const auto& a0 = band[i];
				const auto& a1 = band[(i + 1) % nb];
				for (std::size_t k = 0; k < nr; ++k) {
					if (geometry::intersectSegments(a0, a1, ring[k], ring[(k + 1) % nr]).relation ==
						geometry::SegmentRelation::ProperCrossing) {
						return false;
					}
				}
			}
			return true;
		}

		ValidationCode editFailureCode(geometry::BooleanStatus status, FoundationEditMode mode) {
			const bool add = mode == FoundationEditMode::Add;
			switch (status) {
				case geometry::BooleanStatus::Ok:
					return ValidationCode::Ok;
				case geometry::BooleanStatus::InvalidInput:
					return ValidationCode::SelfIntersects;
				case geometry::BooleanStatus::Disjoint:
					return ValidationCode::EditDisjoint;
				case geometry::BooleanStatus::PinchVertex:
					return ValidationCode::EditPinch;
				case geometry::BooleanStatus::ResultHasHole:
					return add ? ValidationCode::EditEnclosesHole : ValidationCode::EditCutsHole;
				case geometry::BooleanStatus::ResultSplits:
					return ValidationCode::EditSplits;
				case geometry::BooleanStatus::ConsumesInput:
					return ValidationCode::EditConsumes;
				case geometry::BooleanStatus::NoEffect:
					return add ? ValidationCode::EditNothingToAdd : ValidationCode::EditNothingToRemove;
			}
			return ValidationCode::SelfIntersects;
		}

		// Failures of (drawn minus target), the region a built Add turns into an
		// extension blueprint.
		ValidationCode extensionFailureCode(geometry::BooleanStatus status) {
			switch (status) {
				case geometry::BooleanStatus::ResultHasHole:
					return ValidationCode::ExtensionWrapsAround;
				case geometry::BooleanStatus::ResultSplits:
					return ValidationCode::ExtensionSplits;
				case geometry::BooleanStatus::ConsumesInput:
				case geometry::BooleanStatus::NoEffect:
					return ValidationCode::EditNothingToAdd;
				case geometry::BooleanStatus::PinchVertex:
					return ValidationCode::EditPinch;
				case geometry::BooleanStatus::Disjoint:
					return ValidationCode::EditDisjoint;
				case geometry::BooleanStatus::Ok:
					return ValidationCode::Ok;
				case geometry::BooleanStatus::InvalidInput:
					return ValidationCode::SelfIntersects;
			}
			return ValidationCode::SelfIntersects;
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
			case ValidationCode::OpeningSegmentInvalid:
				return "no wall here";
			case ValidationCode::OpeningTypeInvalid:
				return "unknown opening type";
			case ValidationCode::OpeningMaterialInvalid:
				return "unknown material";
			case ValidationCode::OpeningParamOutOfRange:
				return "off the wall";
			case ValidationCode::OpeningMarginTooSmall:
				return "too close to the end";
			case ValidationCode::OpeningWallTooShort:
				return "wall too short for opening";
			case ValidationCode::OpeningOverlap:
				return "openings overlap";
			case ValidationCode::TooCloseToFoundation:
				return "too close to another foundation";
			case ValidationCode::FoundationNotFound:
				return "no foundation";
			case ValidationCode::EditMaterialsDelivered:
				return "materials already delivered";
			case ValidationCode::BuiltNeverShrinks:
				return "built foundations never shrink";
			case ValidationCode::ExtensionPending:
				return "finish the pending extension first";
			case ValidationCode::ExtensionNotEditable:
				return "cancel the extension to reshape it";
			case ValidationCode::EditDisjoint:
				return "must touch the foundation";
			case ValidationCode::EditPinch:
				return "shapes meet at a single point";
			case ValidationCode::EditEnclosesHole:
				return "would enclose a hole";
			case ValidationCode::EditCutsHole:
				return "can't cut a hole";
			case ValidationCode::EditSplits:
				return "would split the foundation";
			case ValidationCode::EditConsumes:
				return "would remove the whole foundation";
			case ValidationCode::EditNothingToAdd:
				return "nothing to add";
			case ValidationCode::EditNothingToRemove:
				return "nothing to remove";
			case ValidationCode::ExtensionSplits:
				return "addition must be one piece";
			case ValidationCode::ExtensionWrapsAround:
				return "addition can't wrap around";
			case ValidationCode::EditUnderWall:
				return "would cut under a wall";
		}
		return {};
	}

	ValidationResult ConstructionValidator::checkShape(const std::vector<geometry::Vec2i64>& ring, bool closed) const {
		ValidationResult result = checkSpacing(ring, closed);
		if (result.ok()) {
			result = checkAngles(ring, closed);
		}
		if (result.ok()) {
			result = checkSimple(ring, closed);
		}
		if (result.ok()) {
			result = checkEdgeClearance(ring, closed);
		}
		return result;
	}

	ValidationResult ConstructionValidator::checkSpacing(const std::vector<geometry::Vec2i64>& ring, bool closed) const {
		const auto&		  c = *constraints_;
		const std::size_t n = ring.size();

		// Consecutive vertices, plus the closing pair when closed.
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
		return {};
	}

	ValidationResult ConstructionValidator::checkAngles(const std::vector<geometry::Vec2i64>& ring, bool closed) const {
		const auto&		  c = *constraints_;
		const std::size_t n = ring.size();

		// Every interior vertex; when open, the endpoints have no corner so skip
		// them. When closed, wrap around.
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
		return {};
	}

	ValidationResult ConstructionValidator::checkSimple(const std::vector<geometry::Vec2i64>& ring, bool closed) const {
		const std::size_t n = ring.size();

		// Any pair of non-adjacent edges that crosses. For the open chain the
		// closing edge is excluded; for the closed ring it is in.
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
		return {};
	}

	ValidationResult ConstructionValidator::checkEdgeClearance(const std::vector<geometry::Vec2i64>& ring, bool closed) const {
		const auto&		  c = *constraints_;
		const std::size_t n = ring.size();
		const std::size_t edgeCount = closed ? n : (n - 1);

		// Non-adjacent edges must stay at least segmentClearance apart. Only
		// meaningful once the shape has at least 4 edges to have a non-adjacent
		// pair (closed: 4 vertices; open: 5 vertices, since the open chain has
		// edgeCount = n - 1 edges).
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

		return checkOutline(quantizeRing(ringMeters), kInvalidFoundation);
	}

	ValidationResult ConstructionValidator::checkOutline(geometry::Ring ring, FoundationId ignore) const {
		const auto& c = *constraints_;
		if (static_cast<int>(ring.size()) > c.maxPoints) {
			return {ValidationCode::TooManyPoints, ring.size() - 1, 0, static_cast<double>(ring.size())};
		}

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

		// Overlap and clearance against committed foundations. The world's commit
		// path re-checks overlap with CCW-normalized winding; doing it here gives the
		// same reject at draw time so the user never gets a click the commit refuses.
		geometry::ensureCounterClockwise(ring);
		for (const Foundation& other : world_->foundations()) {
			if (other.id == ignore) {
				continue;
			}
			if (geometry::ringsInteriorOverlap(ring, other.ring)) {
				return {ValidationCode::OverlapsExisting, 0, 0, 0.0};
			}
			if (foundationsTooClose(ring, other.ring, c.pathingClearanceMm)) {
				return {ValidationCode::TooCloseToFoundation, 0, 0, 0.0};
			}
		}

		return {};
	}

	// --- Foundation Add / Subtract -------------------------------------------

	ValidationResult
	ConstructionValidator::validateEditPoint(const std::vector<::Foundation::Vec2>& points, ::Foundation::Vec2 candidate) const {
		if (points.empty()) {
			return {};
		}

		std::vector<::Foundation::Vec2> chain = points;
		chain.push_back(candidate);
		if (static_cast<int>(chain.size()) > constraints_->maxPoints) {
			return {ValidationCode::TooManyPoints, chain.size() - 1, 0, static_cast<double>(chain.size())};
		}

		const geometry::Ring   ring = quantizeRing(chain);
		const ValidationResult spacing = checkSpacing(ring, /*closed=*/false);
		return spacing.ok() ? checkSimple(ring, /*closed=*/false) : spacing;
	}

	FoundationEditResult ConstructionValidator::validateFoundationEdit(
		FoundationId						   targetId,
		const std::vector<::Foundation::Vec2>& drawn,
		FoundationEditMode					   mode,
		bool								   blueprintEditable
	) const {
		auto reject = [](ValidationResult validation) {
			FoundationEditResult result;
			result.validation = validation;
			return result;
		};
		auto rejectCode = [&reject](ValidationCode code) { return reject({code, 0, 0, 0.0}); };

		const Foundation* target = world_->get(targetId);
		if (target == nullptr) {
			return rejectCode(ValidationCode::FoundationNotFound);
		}
		if (target->mergeTarget != kInvalidFoundation) {
			return rejectCode(ValidationCode::ExtensionNotEditable);
		}
		const bool built = target->state == FoundationState::Built;
		if (built) {
			if (mode == FoundationEditMode::Subtract) {
				return rejectCode(ValidationCode::BuiltNeverShrinks);
			}
			if (world_->pendingExtensionOf(targetId) != kInvalidFoundation) {
				return rejectCode(ValidationCode::ExtensionPending);
			}
		} else if (!blueprintEditable) {
			return rejectCode(ValidationCode::EditMaterialsDelivered);
		}

		// The drawn polygon only has to be a usable cutter; the outline carries the
		// foundation rules.
		if (drawn.size() < 3) {
			return reject({ValidationCode::TooFewPoints, 0, 0, static_cast<double>(drawn.size())});
		}
		if (static_cast<int>(drawn.size()) > constraints_->maxPoints) {
			return reject({ValidationCode::TooManyPoints, drawn.size() - 1, 0, static_cast<double>(drawn.size())});
		}
		geometry::Ring		   cutter = quantizeRing(drawn);
		const ValidationResult spacing = checkSpacing(cutter, /*closed=*/true);
		if (!spacing.ok()) {
			return reject(spacing);
		}
		const ValidationResult simple = checkSimple(cutter, /*closed=*/true);
		if (!simple.ok()) {
			return reject(simple);
		}
		geometry::ensureCounterClockwise(cutter);

		FoundationEditResult result;
		if (mode == FoundationEditMode::Add) {
			geometry::BooleanResult merged = geometry::unionRings(target->ring, cutter);
			if (!merged.ok()) {
				return rejectCode(editFailureCode(merged.status, mode));
			}
			if (geometry::signedAreaDoubled(merged.ring) <= geometry::signedAreaDoubled(target->ring)) {
				return rejectCode(ValidationCode::EditNothingToAdd);
			}
			if (built) {
				// The extension is only what the drawing adds; judge the outline its
				// merge will actually produce, so validation and merge agree exactly.
				geometry::BooleanResult added = geometry::subtractRings(cutter, target->ring);
				if (added.status == geometry::BooleanStatus::NoEffect) {
					added = {geometry::BooleanStatus::Ok, cutter}; // shares only an edge: all of it is new
				}
				if (!added.ok()) {
					return rejectCode(extensionFailureCode(added.status));
				}
				merged = geometry::unionRings(target->ring, added.ring);
				if (!merged.ok()) {
					return rejectCode(editFailureCode(merged.status, mode));
				}
				result.extension = std::move(added.ring);
				result.buildsOn = result.extension;
			} else {
				result.buildsOn = cutter;
			}
			result.outline = std::move(merged.ring);
		} else {
			geometry::BooleanResult remainder = geometry::subtractRings(target->ring, cutter);
			if (!remainder.ok()) {
				return rejectCode(editFailureCode(remainder.status, mode));
			}
			result.outline = std::move(remainder.ring);
		}

		result.validation = checkOutline(result.outline, targetId);
		if (!result.validation.ok()) {
			return reject(result.validation);
		}

		if (mode == FoundationEditMode::Subtract) {
			const auto& registry = engine::assets::ConstructionRegistry::Get();
			for (const WallSegment& s : world_->segments()) {
				if (s.hostFoundation != targetId) {
					continue;
				}
				const Vertex* v0 = world_->getVertex(s.v0);
				const Vertex* v1 = world_->getVertex(s.v1);
				if (v0 == nullptr || v1 == nullptr) {
					continue;
				}
				const auto*		   preset = registry.getThicknessPreset(s.material, s.thicknessPreset);
				const std::int64_t half = preset != nullptr ? preset->halfThicknessMm : 0;
				if (!bandContainedInRing(v0->pos, v1->pos, half, result.outline)) {
					return rejectCode(ValidationCode::EditUnderWall);
				}
			}
		}

		return result;
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
		return f != nullptr && bandContainedInRing(a, b, halfThickMm, f->ring);
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

	// --- Openings -----------------------------------------------------------

	ValidationResult
	ConstructionValidator::validateOpening(SegmentId segment, float t, const std::string& type, const std::string& material) const {
		// 1. Segment exists and is a wall in this world.
		const WallSegment* seg = world_->getSegment(segment);
		if (seg == nullptr) {
			return {ValidationCode::OpeningSegmentInvalid, 0, 0, 0.0};
		}
		const Vertex* v0 = world_->getVertex(seg->v0);
		const Vertex* v1 = world_->getVertex(seg->v1);
		if (v0 == nullptr || v1 == nullptr) {
			return {ValidationCode::OpeningSegmentInvalid, 0, 0, 0.0};
		}

		// 2. Type and material are known config entries. Width comes from the type.
		const auto&							  reg = engine::assets::ConstructionRegistry::Get();
		const engine::assets::OpeningTypeDef* typeDef = reg.getOpeningType(type);
		if (typeDef == nullptr) {
			return {ValidationCode::OpeningTypeInvalid, 0, 0, 0.0};
		}
		if (reg.getMaterial(material) == nullptr) {
			return {ValidationCode::OpeningMaterialInvalid, 0, 0, 0.0};
		}

		// 3. t in [0,1].
		if (t < 0.0F || t > 1.0F) {
			return {ValidationCode::OpeningParamOutOfRange, 0, 0, static_cast<double>(t)};
		}

		// Segment length in meters (exact from integer mm, no float drift).
		const auto	 d = v1->pos - v0->pos;
		const double lengthMm = std::sqrt(geometry::dot(d, d).toDouble());
		const double lengthM = lengthMm / static_cast<double>(geometry::kMillimetersPerMeter);

		const auto&	 c = *constraints_;
		const double halfWidthM = static_cast<double>(typeDef->widthMeters) / 2.0;
		const double marginM = static_cast<double>(c.openingMarginMeters);

		// 5. Wall must be long enough to host width + 2*margin. Check before the
		// margin clearance so a wall that simply cannot fit the opening reports the
		// clearer reason (and avoids a meaningless negative clearance below).
		if (lengthM < static_cast<double>(typeDef->widthMeters) + 2.0 * marginM) {
			return {ValidationCode::OpeningWallTooShort, 0, 0, lengthM};
		}

		// 4. End margin: the opening occupies [t - half/L, t + half/L] along the
		// centerline; its near edges must clear `openingMargin` from each end.
		// Clearance from the v0 end to the near edge, in meters: (t*L - half).
		const double clearV0 = t * lengthM - halfWidthM;
		const double clearV1 = (1.0 - t) * lengthM - halfWidthM;
		const double minClear = std::min(clearV0, clearV1);
		if (minClear < marginM) {
			return {ValidationCode::OpeningMarginTooSmall, 0, 0, minClear};
		}

		// 6. No overlap with another opening already on the SAME segment. Two
		// openings conflict when the gap between their near edges is below the
		// inter-opening margin (reuse openingMargin). Compare in meters along the
		// centerline. Iterate openings() in stable insertion order (determinism).
		const double centerM = t * lengthM;
		for (const Opening& other : world_->openings()) {
			if (other.segment != segment) {
				continue;
			}
			const engine::assets::OpeningTypeDef* otherType = reg.getOpeningType(other.type);
			const double otherHalfM = otherType != nullptr ? static_cast<double>(otherType->widthMeters) / 2.0 : 0.0;
			const double otherCenterM = static_cast<double>(other.t) * lengthM;
			const double gap = std::abs(centerM - otherCenterM) - halfWidthM - otherHalfM;
			if (gap < marginM) {
				return {ValidationCode::OpeningOverlap, 0, 0, gap};
			}
		}

		return {};
	}

} // namespace engine::construction

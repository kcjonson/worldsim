#include "SnapEngine.h"

#include "ConstructionValidator.h"
#include "ConstructionWorld.h"
#include "OpeningGeometry.h"
#include "SnapGuidesDetail.h"

#include <assets/ConstructionRegistry.h>

#include <predicates/Predicates.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

namespace construction {

	namespace {

		float distanceMeters(::Foundation::Vec2 a, ::Foundation::Vec2 b) {
			const float dx = a.x - b.x;
			const float dy = a.y - b.y;
			return std::sqrt(dx * dx + dy * dy);
		}

		// Nearest point on segment [a,b] to p, all in world meters.
		::Foundation::Vec2 closestOnSegment(::Foundation::Vec2 p, ::Foundation::Vec2 a, ::Foundation::Vec2 b) {
			const float abx = b.x - a.x;
			const float aby = b.y - a.y;
			const float lenSq = abx * abx + aby * aby;
			if (lenSq <= 0.0F) {
				return a;
			}
			float t = ((p.x - a.x) * abx + (p.y - a.y) * aby) / lenSq;
			t = std::clamp(t, 0.0F, 1.0F);
			return {a.x + abx * t, a.y + aby * t};
		}

		// Unclamped projection parameter of p onto the line through a->b. A value
		// strictly in (0,1) means the foot of the perpendicular lands on the
		// segment's interior; <=0 or >=1 means it clamps to an endpoint. Returns
		// 0 for a degenerate (zero-length) segment.
		float projectionParam(::Foundation::Vec2 p, ::Foundation::Vec2 a, ::Foundation::Vec2 b) {
			const float abx = b.x - a.x;
			const float aby = b.y - a.y;
			const float lenSq = abx * abx + aby * aby;
			if (lenSq <= 0.0F) {
				return 0.0F;
			}
			return ((p.x - a.x) * abx + (p.y - a.y) * aby) / lenSq;
		}

		using geometry::Vec2d;
		using snap_guides::GuideContext;
		using snap_guides::GuideStudy;
		using snap_guides::meters;
		using snap_guides::toVec2d;

		// A foundation outline closes once it has this many points.
		constexpr std::size_t kMinClosingShapePoints = 3;

		::Foundation::Vec2 toVec2(Vec2d point) {
			return {static_cast<float>(point.x), static_cast<float>(point.y)};
		}

		// The guide study's working storage, reused by every snap on this thread so a moving
		// cursor allocates only while the inputs grow past anything seen before.
		GuideStudy& scratchStudy() {
			thread_local GuideStudy study;
			return study;
		}

		// A geometry hit on the edge from `start` to `end`, moved to where a captured shape
		// guide crosses it near the cursor (squareOntoEdge), keeping its kind and hit ids.
		SnapResult
		squareEdgeHit(SnapResult hit, const GuideContext& context, const GuideStudy& study, Vec2d start, Vec2d end, bool strictInterior) {
			const auto squared = snap_guides::squareOntoEdge(context, study, start, end, strictInterior);
			if (!squared) {
				return hit;
			}
			hit.point = toVec2(squared->point);
			hit.guides[0] = snap_guides::makeGuide(context, study, study.lines[squared->line], squared->point);
			hit.guideCount = 1;
			return hit;
		}

		// Unit interior normal (meters) of ring edge `edgeIndex` (ring[i] -> ring[i+1]),
		// pointing toward the polygon interior. The candidate left normal is flipped
		// when a probe just off the edge midpoint lands outside the ring, so this is
		// correct for either winding without relying on a coordinate-handedness
		// assumption. Zero for a degenerate (zero-length) edge.
		::Foundation::Vec2 inwardNormalMeters(const geometry::Ring& ring, std::size_t edgeIndex) {
			const std::size_t		 n = ring.size();
			const ::Foundation::Vec2 a = geometry::dequantize(ring[edgeIndex]);
			const ::Foundation::Vec2 b = geometry::dequantize(ring[(edgeIndex + 1) % n]);
			const float				 dx = b.x - a.x;
			const float				 dy = b.y - a.y;
			const float				 len = std::sqrt(dx * dx + dy * dy);
			if (len <= 0.0F) {
				return {0.0F, 0.0F};
			}
			float nx = -dy / len;
			float ny = dx / len;
			// Disambiguate the interior side exactly: a 10 mm probe off the midpoint
			// (well within any legal foundation, min spacing 0.5 m) that lands outside
			// means we picked the exterior normal, so flip.
			const ::Foundation::Vec2 mid{(a.x + b.x) * 0.5F, (a.y + b.y) * 0.5F};
			const float				 probe = 0.01F;
			const geometry::Vec2i64	 q = geometry::quantize({mid.x + nx * probe, mid.y + ny * probe});
			if (geometry::pointInPolygon(q, ring) == geometry::PointInPolygon::Outside) {
				nx = -nx;
				ny = -ny;
			}
			return {nx, ny};
		}

	} // namespace

	::Foundation::Vec2 outerFaceFlushCorner(const geometry::Ring& ring, std::size_t vertexIndex, std::int64_t halfThicknessMm) {
		const std::size_t n = ring.size();
		if (vertexIndex >= n) {
			return {0.0F, 0.0F}; // out-of-range index (covers an empty ring): nothing to inset
		}
		const ::Foundation::Vec2 v = geometry::dequantize(ring[vertexIndex]);
		if (halfThicknessMm <= 0 || n < 3) {
			return v; // zero-thickness or degenerate ring: the corner is returned unchanged
		}

		const auto				 d = static_cast<float>(snap_guides::flushInsetMeters(halfThicknessMm));
		const std::size_t		 prevEdge = (vertexIndex + n - 1) % n; // edge prev -> v
		const std::size_t		 nextEdge = vertexIndex;			   // edge v -> next
		const ::Foundation::Vec2 prev = geometry::dequantize(ring[prevEdge]);
		const ::Foundation::Vec2 next = geometry::dequantize(ring[(nextEdge + 1) % n]);
		const ::Foundation::Vec2 n1 = inwardNormalMeters(ring, prevEdge);
		const ::Foundation::Vec2 n2 = inwardNormalMeters(ring, nextEdge);

		// Each incident edge offset inward by d defines a line; the corner is their
		// intersection (the miter). Line 1 passes through A1 along d1 (prev->v); line 2
		// through A2 along d2 (v->next).
		const ::Foundation::Vec2 d1{v.x - prev.x, v.y - prev.y};
		const ::Foundation::Vec2 d2{next.x - v.x, next.y - v.y};
		const ::Foundation::Vec2 a1{v.x + n1.x * d, v.y + n1.y * d};
		const ::Foundation::Vec2 a2{v.x + n2.x * d, v.y + n2.y * d};

		const float denom = d1.x * d2.y - d1.y * d2.x;
		if (std::abs(denom) < 1e-6F) {
			// (Near-)collinear edges (a straight pass-through vertex): the two inset
			// lines coincide, so a single-edge offset is the miter.
			return a1;
		}
		const float s = ((a2.x - a1.x) * d2.y - (a2.y - a1.y) * d2.x) / denom;
		return {a1.x + s * d1.x, a1.y + s * d1.y};
	}

	std::optional<FoundationCornerHit>
	nearestFoundationCorner(const ConstructionWorld& world, ::Foundation::Vec2 cursor, float radiusMeters) {
		std::optional<FoundationCornerHit> nearest;
		float							   best = radiusMeters;
		for (const Foundation& f : world.foundations()) {
			for (std::size_t i = 0; i < f.ring.size(); ++i) {
				const ::Foundation::Vec2 corner = geometry::dequantize(f.ring[i]);
				const float				 d = distanceMeters(cursor, corner);
				if (d <= best) {
					best = d;
					nearest = FoundationCornerHit{.point = corner, .foundation = f.id, .vertexIndex = i, .distanceMeters = d};
				}
			}
		}
		return nearest;
	}

	std::optional<FoundationEdgeHit> nearestFoundationEdge(const ConstructionWorld& world, ::Foundation::Vec2 cursor, float radiusMeters) {
		std::optional<FoundationEdgeHit> nearest;
		float							 best = radiusMeters;
		for (const Foundation& f : world.foundations()) {
			const std::size_t n = f.ring.size();
			for (std::size_t i = 0; i < n; ++i) {
				const ::Foundation::Vec2 a = geometry::dequantize(f.ring[i]);
				const ::Foundation::Vec2 b = geometry::dequantize(f.ring[(i + 1) % n]);
				const ::Foundation::Vec2 foot = closestOnSegment(cursor, a, b);
				const float				 d = distanceMeters(cursor, foot);
				if (d <= best) {
					best = d;
					nearest = FoundationEdgeHit{.point = foot, .foundation = f.id, .edgeIndex = i, .distanceMeters = d};
				}
			}
		}
		return nearest;
	}

	bool SnapEngine::snapToWallVertex(::Foundation::Vec2 cursor, ::Foundation::Vec2& out, VertexId& outVertex) const {
		const float radius = snapping_->vertexSnapRadiusMeters;
		float		best = radius;
		bool		found = false;
		for (const Vertex& v : world_->vertices()) {
			const ::Foundation::Vec2 vm = geometry::dequantize(v.pos);
			const float				 d = distanceMeters(cursor, vm);
			// Strict-< on a tie keeps the lowest-id vertex (vertices() is in
			// ascending-id insertion order), so ties resolve deterministically.
			if (d < best) {
				best = d;
				out = vm;
				outVertex = v.id;
				found = true;
			}
		}
		return found;
	}

	bool SnapEngine::snapToWallSegment(::Foundation::Vec2 cursor, ::Foundation::Vec2& out, SegmentId& outSegment) const {
		const float radius = snapping_->edgeSnapRadiusMeters;
		float		best = radius;
		bool		found = false;
		for (const WallSegment& s : world_->segments()) {
			const Vertex* v0 = world_->getVertex(s.v0);
			const Vertex* v1 = world_->getVertex(s.v1);
			if (v0 == nullptr || v1 == nullptr) {
				continue;
			}
			const ::Foundation::Vec2 a = geometry::dequantize(v0->pos);
			const ::Foundation::Vec2 b = geometry::dequantize(v1->pos);
			// Endpoints are explicitly NOT segment hits: a projection that clamps to
			// either end is an endpoint join (the wall-vertex tier owns it), not a
			// T-junction. Only a foot of perpendicular landing strictly inside the
			// span (0 < t < 1) is a genuine interior point-on-wall, so skip the rest.
			// Without this, a cursor near a segment's end but outside the vertex
			// radius would report WallSegment with hitSegment set, telling the
			// WallTool to T-split when it should plain-join the endpoint.
			const float t = projectionParam(cursor, a, b);
			if (t <= 0.0F || t >= 1.0F) {
				continue;
			}
			const ::Foundation::Vec2 c = closestOnSegment(cursor, a, b);
			const float				 d = distanceMeters(cursor, c);
			// Strict-< keeps the lowest-id segment on a tie; segments() is in
			// ascending-id order. (segmentAt's stored tie-break is highest-id, but
			// that is the pick query, not snapping; here the wall vertices already
			// handle endpoint priority, so the segment tie-break only ever picks
			// among true interior projections and lowest-id is the simplest rule.)
			if (d < best) {
				best = d;
				out = c;
				outSegment = s.id;
				found = true;
			}
		}
		return found;
	}

	SnapResult SnapEngine::snapAngle(const std::vector<::Foundation::Vec2>& points, ::Foundation::Vec2 cursor) const {
		const Vec2d	 previous = toVec2d(points.back());
		const Vec2d	 offset = toVec2d(cursor) - previous;
		const double span = geometry::length(offset);
		const double increment = static_cast<double>(snapping_->angleIncrementDegrees) * (std::numbers::pi / 180.0);
		if (span <= 0.0 || increment <= 0.0) {
			return {cursor, SnapKind::None};
		}

		// Reference direction: the previous segment if there is one, else +x axis.
		double reference = 0.0;
		if (points.size() >= 2) {
			const Vec2d before = toVec2d(points[points.size() - 2]);
			reference = std::atan2(previous.y - before.y, previous.x - before.x);
		}
		// Half steps round up, as the tuned prototype rounds them.
		const double steps = std::floor((std::atan2(offset.y, offset.x) - reference) / increment + 0.5);
		const double snappedAngle = reference + steps * increment;
		const Vec2d	 snapped = previous + Vec2d{std::cos(snappedAngle), std::sin(snappedAngle)} * span;

		SnapResult result;
		result.point = toVec2(snapped);
		result.kind = SnapKind::Angle;
		result.guides[0] = SnapGuide{
			.from = points.back(),
			.to = result.point,
			.source = points.size() >= 2 ? GuideSource::DrawnShape : GuideSource::WorldAxes,
			.relation = GuideRelation::AngleStep,
			.distanceFromNodeMeters = static_cast<float>(span),
			.throughPreviousPoint = true,
		};
		result.guideCount = 1;
		return result;
	}

	GuideContext SnapEngine::guideContext(
		const std::vector<::Foundation::Vec2>& points,
		::Foundation::Vec2					   cursor,
		const SnapInput&					   input,
		bool								   wallTool
	) const {
		return GuideContext{
			.snapping = snapping_,
			.world = world_,
			.points = &points,
			.cursor = toVec2d(cursor),
			.aids = input.aids,
			.tool = wallTool ? snap_guides::SnapTool::Wall : snap_guides::SnapTool::Foundation,
			.wallHalfThicknessMm = input.wallHalfThicknessMm,
			// From the registry, not the injected config: the caller doesn't pass the point limit yet.
			.maxPoints = assets::ConstructionRegistry::Get().constraints().maxPoints,
		};
	}

	std::optional<SnapResult> SnapEngine::alignToGuides(
		const GuideContext& context,
		const GuideStudy&	study,
		std::size_t			minClosePoints,
		float				originCloseRadiusMeters
	) const {
		if (!study.winner) {
			return std::nullopt;
		}
		const snap_guides::GuideCandidate& winner = study.candidates[*study.winner];
		const std::vector<Vec2d>&		   points = study.shapePoints;

		SnapResult result;
		Vec2d	   point = winner.point;
		result.kind = winner.isCrossing ? SnapKind::GuideCrossing : SnapKind::GuideLine;
		const float radius = originCloseRadiusMeters >= 0.0F ? originCloseRadiusMeters : snapping_->originCloseRadiusMeters;
		if (points.size() >= minClosePoints && geometry::length(point - points.front()) <= static_cast<double>(radius)) {
			// Closes onto the first point exactly, so the tool sees the close.
			point = points.front();
			result.kind = SnapKind::Origin;
			result.point = context.points->front();
		} else {
			result.point = toVec2(point);
		}
		result.guides[result.guideCount++] = snap_guides::makeGuide(context, study, study.lines[winner.bestLine], point);
		if (winner.isCrossing) {
			result.guides[result.guideCount++] = snap_guides::makeGuide(context, study, study.lines[winner.otherLine], point);
		}
		return result;
	}

	SnapResult SnapEngine::snapToHelpers(
		const std::vector<::Foundation::Vec2>& points,
		::Foundation::Vec2					   cursor,
		const SnapInput&					   input,
		const GuideContext&					   context,
		const GuideStudy&					   study,
		std::size_t							   minClosePoints
	) const {
		if (input.freeform) {
			return {cursor, SnapKind::None};
		}
		if (auto guided = alignToGuides(context, study, minClosePoints, input.originCloseRadiusMeters)) {
			return *guided;
		}
		if (input.aids.angleSteps && !points.empty()) {
			return snapAngle(points, cursor);
		}
		return {cursor, SnapKind::None};
	}

	bool SnapEngine::snapToOrigin(
		const std::vector<::Foundation::Vec2>& points,
		::Foundation::Vec2					   cursor,
		std::size_t							   minPoints,
		float								   radiusMeters
	) const {
		if (points.size() < minPoints) {
			return false;
		}
		const float radius = radiusMeters >= 0.0F ? radiusMeters : snapping_->originCloseRadiusMeters;
		return distanceMeters(cursor, points.front()) <= radius;
	}

	SnapResult SnapEngine::snap(const std::vector<::Foundation::Vec2>& points, ::Foundation::Vec2 cursor, const SnapInput& input) const {
		// Origin-close: once a closeable shape exists (>= 3 points), being within
		// the origin radius snaps onto the first point and signals a close.
		if (snapToOrigin(points, cursor, kMinClosingShapePoints, input.originCloseRadiusMeters)) {
			return {points.front(), SnapKind::Origin};
		}

		// Existing-geometry snapping (always on): vertices beat edges.
		if (const auto corner = nearestFoundationCorner(*world_, cursor, snapping_->vertexSnapRadiusMeters)) {
			return {corner->point, SnapKind::Vertex};
		}

		const GuideContext context = guideContext(points, cursor, input, /*wallTool=*/false);
		GuideStudy&		   study = scratchStudy();
		if (!input.freeform) {
			snap_guides::studyGuides(context, study);
		}

		if (const auto edge = nearestFoundationEdge(*world_, cursor, snapping_->edgeSnapRadiusMeters)) {
			const SnapResult hit{edge->point, SnapKind::Edge};
			if (input.freeform) {
				return hit;
			}
			const geometry::Ring& ring = world_->get(edge->foundation)->ring;
			return squareEdgeHit(
				hit,
				context,
				study,
				meters(ring[edge->edgeIndex]),
				meters(ring[(edge->edgeIndex + 1) % ring.size()]),
				/*strictInterior=*/false
			);
		}

		return snapToHelpers(points, cursor, input, context, study, kMinClosingShapePoints);
	}

	OpeningSnap SnapEngine::snapOpening(::Foundation::Vec2 cursor, float openingWidthMeters) const {
		const float radius = snapping_->edgeSnapRadiusMeters;
		// End margin is a construction CONSTRAINT, not a snapping one; read it from the
		// registry (the production source of truth) the same way the validator resolves
		// config it doesn't directly hold. An unloaded registry yields the struct
		// default (0.3 m), which is deterministic.
		const double margin = static_cast<double>(assets::ConstructionRegistry::Get().constraints().openingMarginMeters);

		float		best = radius;
		OpeningSnap result;
		for (const WallSegment& s : world_->segments()) {
			// Only finished walls can host an opening; a blueprint has no surface yet.
			if (!isBuilt(s)) {
				continue;
			}
			const Vertex* v0 = world_->getVertex(s.v0);
			const Vertex* v1 = world_->getVertex(s.v1);
			if (v0 == nullptr || v1 == nullptr) {
				continue;
			}
			const ::Foundation::Vec2 a = geometry::dequantize(v0->pos);
			const ::Foundation::Vec2 b = geometry::dequantize(v1->pos);
			const ::Foundation::Vec2 c = closestOnSegment(cursor, a, b);
			const float				 d = distanceMeters(cursor, c);
			// Strict-< keeps the lowest-id segment on a tie; segments() is in
			// ascending-id insertion order (deterministic).
			if (d >= best) {
				continue;
			}
			// The range only for a segment nearer than the best so far, so a move
			// resolves the corners of few walls. One too short to host the opening is
			// skipped: a closer short wall must not shadow a farther one that fits.
			const std::optional<OpeningRange> range = openingRange(*world_, s.id, static_cast<double>(openingWidthMeters), margin);
			if (!range || !range->fits()) {
				continue;
			}
			best = d;
			const float t = std::clamp(projectionParam(cursor, a, b), range->minT, range->maxT);
			result.segment = s.id;
			result.t = t;
			result.point = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
			result.valid = true;
		}
		return result;
	}

	SnapResult
	SnapEngine::snapWall(const std::vector<::Foundation::Vec2>& points, ::Foundation::Vec2 cursor, const SnapInput& input) const {
		// Priority, most specific first (design Walls > Drawing), led by the close onto
		// the chain's first point as in snap(). The point comes back exactly, so
		// closesWallChain sees the close.
		if (!input.freeform && snapToOrigin(points, cursor, kMinClosingWallChainPoints, input.originCloseRadiusMeters)) {
			return {points.front(), SnapKind::Origin};
		}

		::Foundation::Vec2 wv{};
		VertexId		   wvId = kInvalidVertex;
		if (snapToWallVertex(cursor, wv, wvId)) {
			SnapResult r;
			r.point = wv;
			r.kind = SnapKind::WallEndpoint;
			r.hitVertex = wvId;
			return r;
		}

		// Foundation corner: proximity is to the raw corner, the point is the
		// outer-face-flush miter so a perimeter wall sits ON the foundation (design
		// "Alignment").
		const std::int64_t insetMm = input.wallHalfThicknessMm;
		if (const auto corner = nearestFoundationCorner(*world_, cursor, snapping_->vertexSnapRadiusMeters)) {
			const ::Foundation::Vec2 point =
				insetMm > 0 ? outerFaceFlushCorner(world_->get(corner->foundation)->ring, corner->vertexIndex, insetMm) : corner->point;
			return {point, SnapKind::Vertex};
		}

		const GuideContext context = guideContext(points, cursor, input, /*wallTool=*/true);
		GuideStudy&		   study = scratchStudy();
		if (!input.freeform) {
			snap_guides::studyGuides(context, study);
		}

		::Foundation::Vec2 ws{};
		SegmentId		   wsId = kInvalidSegment;
		if (snapToWallSegment(cursor, ws, wsId)) {
			SnapResult r;
			r.point = ws;
			r.kind = SnapKind::WallSegment;
			r.hitSegment = wsId;
			if (input.freeform) {
				return r;
			}
			// Squared strictly inside the segment, so the point stays a T-junction.
			const WallSegment* segment = world_->getSegment(wsId);
			return squareEdgeHit(
				r,
				context,
				study,
				meters(world_->getVertex(segment->v0)->pos),
				meters(world_->getVertex(segment->v1)->pos),
				/*strictInterior=*/true
			);
		}

		// Foundation edge: proximity is to the raw edge, the point is the foot moved
		// inward by the half-thickness (outer-face-flush), and squaring is on that inset line.
		if (const auto edge = nearestFoundationEdge(*world_, cursor, snapping_->edgeSnapRadiusMeters)) {
			const geometry::Ring& ring = world_->get(edge->foundation)->ring;
			Vec2d				  offset{};
			if (insetMm > 0) {
				offset = toVec2d(inwardNormalMeters(ring, edge->edgeIndex)) * snap_guides::flushInsetMeters(insetMm);
			}
			const SnapResult hit{toVec2(toVec2d(edge->point) + offset), SnapKind::Edge};
			if (input.freeform) {
				return hit;
			}
			return squareEdgeHit(
				hit,
				context,
				study,
				meters(ring[edge->edgeIndex]) + offset,
				meters(ring[(edge->edgeIndex + 1) % ring.size()]) + offset,
				/*strictInterior=*/false
			);
		}

		// A wall's guide point is NOT inset for outer-face-flush: it lines up with a
		// node's coordinate, not a foundation face, so the centerline lands on the guide.
		return snapToHelpers(points, cursor, input, context, study, kMinClosingWallChainPoints);
	}
} // namespace construction

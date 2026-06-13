#include "ConstructionWorld.h"

#include <predicates/Predicates.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace engine::construction {

	namespace {

		CommitStatus translateBooleanStatus(geometry::BooleanStatus status) {
			switch (status) {
				case geometry::BooleanStatus::Ok:
					return CommitStatus::Ok;
				case geometry::BooleanStatus::InvalidInput:
					return CommitStatus::BooleanInvalidInput;
				case geometry::BooleanStatus::Disjoint:
					return CommitStatus::BooleanDisjoint;
				case geometry::BooleanStatus::PinchVertex:
					return CommitStatus::BooleanPinchVertex;
				case geometry::BooleanStatus::ResultHasHole:
					return CommitStatus::BooleanResultHasHole;
				case geometry::BooleanStatus::ResultSplits:
					return CommitStatus::BooleanResultSplits;
				case geometry::BooleanStatus::ConsumesInput:
					return CommitStatus::BooleanConsumesInput;
				case geometry::BooleanStatus::NoEffect:
					return CommitStatus::BooleanNoEffect;
			}
			return CommitStatus::BooleanInvalidInput;
		}

		// Exact test: does `p` lie strictly on the interior of segment [s0,s1]?
		// Collinear and inside the bounding box, but not coincident with either
		// endpoint. The not-equal-to-endpoint guard is what distinguishes a
		// T-junction (split the host) from a shared-vertex join (no split).
		bool pointOnSegmentInterior(const geometry::Vec2i64& p, const geometry::Vec2i64& s0, const geometry::Vec2i64& s1) {
			if (p == s0 || p == s1) {
				return false;
			}
			if (geometry::orientation(s0, s1, p) != geometry::Orientation::Collinear) {
				return false;
			}
			return p.x >= std::min(s0.x, s1.x) && p.x <= std::max(s0.x, s1.x) && p.y >= std::min(s0.y, s1.y) && p.y <= std::max(s0.y, s1.y);
		}

		// Parameter of a point along a->b as the exact integer dot(p-a, b-a).
		// Monotonic in the true parameter t (it is t scaled by the constant
		// |b-a|^2 > 0), so it orders collinear break points without dividing.
		geometry::Int128 paramAlong(const geometry::Vec2i64& p, const geometry::Vec2i64& a, const geometry::Vec2i64& b) {
			return geometry::dot(p - a, b - a);
		}

		// True split parameter in [0,1] of point `p` on segment [s0,s1] as a
		// float: dot(p-s0, s1-s0) / |s1-s0|^2. Used only to re-scale opening
		// parameters across a split; opening t is float by design (D4), so float
		// here matches the model and the values being rescaled.
		float splitParamFloat(const geometry::Vec2i64& p, const geometry::Vec2i64& s0, const geometry::Vec2i64& s1) {
			const double num = geometry::dot(p - s0, s1 - s0).toDouble();
			const double den = geometry::dot(s1 - s0, s1 - s0).toDouble();
			if (den <= 0.0) {
				return 0.0F;
			}
			return static_cast<float>(num / den);
		}

	} // namespace

	CommitStatus ConstructionWorld::validateStructure(geometry::Ring& ring, FoundationId ignoreId) const {
		if (ring.size() < 3) {
			return CommitStatus::TooFewVertices;
		}
		if (!geometry::isSimple(ring).pass) {
			return CommitStatus::NotSimple;
		}
		// Zero signed area means a degenerate (collinear) ring. Exact in 128-bit.
		if (geometry::signedAreaDoubled(ring).sign() == 0) {
			return CommitStatus::DegenerateArea;
		}

		geometry::ensureCounterClockwise(ring);

		for (const Foundation& other : foundations_) {
			if (other.id == ignoreId) {
				continue;
			}
			if (geometry::ringsInteriorOverlap(ring, other.ring)) {
				return CommitStatus::OverlapsExisting;
			}
		}
		return CommitStatus::Ok;
	}

	CommitResult ConstructionWorld::commitFoundation(geometry::Ring ring, std::string material) {
		const CommitStatus status = validateStructure(ring, kInvalidFoundation);
		if (status != CommitStatus::Ok) {
			return {status, kInvalidFoundation};
		}

		Foundation foundation;
		foundation.id = nextFoundationId_++;
		foundation.ring = std::move(ring);
		foundation.material = std::move(material);
		foundation.state = FoundationState::Blueprint;
		foundation.entity = ecs::kInvalidEntity;

		const FoundationId id = foundation.id;
		foundations_.push_back(std::move(foundation));
		++version_;
		return {CommitStatus::Ok, id};
	}

	CommitResult ConstructionWorld::commitFoundation(const std::vector<::Foundation::Vec2>& meters, std::string material) {
		geometry::Ring ring;
		ring.reserve(meters.size());
		for (const ::Foundation::Vec2& point : meters) {
			ring.push_back(geometry::quantize(point));
		}
		return commitFoundation(std::move(ring), std::move(material));
	}

	CommitStatus ConstructionWorld::addToFoundation(FoundationId id, const geometry::Ring& addend) {
		Foundation* foundation = find(id);
		if (foundation == nullptr) {
			return CommitStatus::UnknownFoundation;
		}

		geometry::BooleanResult result = geometry::unionRings(foundation->ring, addend);
		if (!result.ok()) {
			return translateBooleanStatus(result.status);
		}

		// Re-validate the merged outline against the structural invariants and
		// the other foundations. The booleaned ring is already simple and CCW,
		// but the overlap check must still run against the rest of the store.
		const CommitStatus status = validateStructure(result.ring, id);
		if (status != CommitStatus::Ok) {
			return status;
		}

		foundation->ring = std::move(result.ring);
		++version_;
		return CommitStatus::Ok;
	}

	CommitStatus ConstructionWorld::subtractFromFoundation(FoundationId id, const geometry::Ring& subtrahend) {
		Foundation* foundation = find(id);
		if (foundation == nullptr) {
			return CommitStatus::UnknownFoundation;
		}

		geometry::BooleanResult result = geometry::subtractRings(foundation->ring, subtrahend);
		if (!result.ok()) {
			return translateBooleanStatus(result.status);
		}

		const CommitStatus status = validateStructure(result.ring, id);
		if (status != CommitStatus::Ok) {
			return status;
		}

		foundation->ring = std::move(result.ring);
		++version_;
		return CommitStatus::Ok;
	}

	bool ConstructionWorld::removeFoundation(FoundationId id) {
		const auto it = std::find_if(foundations_.begin(), foundations_.end(), [id](const Foundation& f) { return f.id == id; });
		if (it == foundations_.end()) {
			return false;
		}
		foundations_.erase(it);
		++version_;
		return true;
	}

	// ========================================================================
	// Wall topology
	// ========================================================================

	VertexId ConstructionWorld::findOrCreateVertex(const geometry::Vec2i64& pos) {
		for (const Vertex& v : vertices_) {
			if (v.pos == pos) {
				return v.id;
			}
		}
		Vertex vertex;
		vertex.id = nextVertexId_++;
		vertex.pos = pos;
		const VertexId id = vertex.id;
		vertices_.push_back(std::move(vertex));
		return id;
	}

	void ConstructionWorld::addAdjacency(VertexId vertex, SegmentId segment) {
		Vertex* v = findVertex(vertex);
		if (v != nullptr) {
			v->segments.push_back(segment);
		}
	}

	void ConstructionWorld::removeAdjacency(VertexId vertex, SegmentId segment) {
		Vertex* v = findVertex(vertex);
		if (v == nullptr) {
			return;
		}
		const auto it = std::find(v->segments.begin(), v->segments.end(), segment);
		if (it != v->segments.end()) {
			v->segments.erase(it);
		}
	}

	void ConstructionWorld::pruneVertexIfOrphan(VertexId vertex) {
		const auto it = std::find_if(vertices_.begin(), vertices_.end(), [vertex](const Vertex& v) { return v.id == vertex; });
		if (it != vertices_.end() && it->segments.empty()) {
			vertices_.erase(it);
		}
	}

	VertexId ConstructionWorld::splitSegmentAt(SegmentId host, const geometry::Vec2i64& at) {
		WallSegment* s = findSegment(host);
		if (s == nullptr) {
			return kInvalidVertex;
		}

		// Snapshot what we need before mutating the segment vector.
		const VertexId		  oldV0 = s->v0;
		const VertexId		  oldV1 = s->v1;
		const std::string	  material = s->material;
		const std::string	  preset = s->thicknessPreset;
		const FoundationId	  hostFnd = s->hostFoundation;
		const FoundationState state = s->state;

		const Vertex* v0 = findVertex(oldV0);
		const Vertex* v1 = findVertex(oldV1);
		if (v0 == nullptr || v1 == nullptr) {
			return kInvalidVertex;
		}
		const geometry::Vec2i64 p0 = v0->pos;
		const geometry::Vec2i64 p1 = v1->pos;

		// Re-scale openings into the half they fall in before the segment dies.
		// t < splitParam -> first half (v0..mid), rescaled t/splitParam.
		// t >= splitParam -> second half (mid..v1), rescaled (t-sp)/(1-sp).
		const float splitParam = splitParamFloat(at, p0, p1);

		// Allocate the midpoint vertex and the two replacement segments.
		const VertexId mid = findOrCreateVertex(at);

		WallSegment first;
		first.id = nextSegmentId_++;
		first.v0 = oldV0;
		first.v1 = mid;
		first.material = material;
		first.thicknessPreset = preset;
		first.hostFoundation = hostFnd;
		first.state = state;
		first.entity = ecs::kInvalidEntity;

		WallSegment second;
		second.id = nextSegmentId_++;
		second.v0 = mid;
		second.v1 = oldV1;
		second.material = material;
		second.thicknessPreset = preset;
		second.hostFoundation = hostFnd;
		second.state = state;
		second.entity = ecs::kInvalidEntity;

		const SegmentId firstId = first.id;
		const SegmentId secondId = second.id;

		// Re-attach openings. The old segment's ECS entity is intentionally NOT
		// carried to either half: the topology piece it mirrored no longer
		// exists, so the entity is dropped from the graph here and the caller
		// (ConstructionSystem) is responsible for despawning or re-mapping it
		// against the two new SegmentIds. Openings keep their own entities; only
		// their host segment + parameter change.
		for (Opening& opening : openings_) {
			if (opening.segment != host) {
				continue;
			}
			if (opening.t < splitParam) {
				opening.segment = firstId;
				opening.t = splitParam > 0.0F ? opening.t / splitParam : 0.0F;
			} else {
				opening.segment = secondId;
				const float denom = 1.0F - splitParam;
				opening.t = denom > 0.0F ? (opening.t - splitParam) / denom : 0.0F;
			}
		}

		// Drop the old segment from both endpoints' adjacency and remove it.
		removeAdjacency(oldV0, host);
		removeAdjacency(oldV1, host);
		const auto it = std::find_if(segments_.begin(), segments_.end(), [host](const WallSegment& w) { return w.id == host; });
		if (it != segments_.end()) {
			segments_.erase(it);
		}

		// Insert the replacements and wire adjacency (mid now has degree 2 from
		// the split alone; the incoming new segment will raise it to 3+).
		segments_.push_back(std::move(first));
		segments_.push_back(std::move(second));
		addAdjacency(oldV0, firstId);
		addAdjacency(mid, firstId);
		addAdjacency(mid, secondId);
		addAdjacency(oldV1, secondId);

		return mid;
	}

	SegmentCommitResult ConstructionWorld::commitSegment(
		const geometry::Vec2i64& a,
		const geometry::Vec2i64& b,
		std::string				 material,
		std::string				 thicknessPreset,
		FoundationId			 host
	) {
		if (a == b) {
			return {SegmentStatus::ZeroLength, kInvalidSegment};
		}
		if (host != kInvalidFoundation && find(host) == nullptr) {
			return {SegmentStatus::UnknownHost, kInvalidSegment};
		}

		// X-crossing rejection. A new segment whose interior properly crosses an
		// existing segment's interior is forbidden (D4: no X-crossings in v1;
		// draw across a wall and you make two T-junctions, not a cross). The
		// exact predicate reports ProperCrossing only when each segment strictly
		// straddles the other's supporting line, i.e. an interior-interior
		// crossing; an endpoint landing on a segment is EndpointTouch (a
		// T-junction, handled by splitting) and a shared vertex is also
		// EndpointTouch, so neither trips this gate.
		for (const WallSegment& s : segments_) {
			const Vertex* sv0 = findVertex(s.v0);
			const Vertex* sv1 = findVertex(s.v1);
			if (sv0 == nullptr || sv1 == nullptr) {
				continue;
			}
			if (geometry::intersectSegments(a, b, sv0->pos, sv1->pos).relation == geometry::SegmentRelation::ProperCrossing) {
				return {SegmentStatus::XCrossing, kInvalidSegment};
			}
		}

		// Break the new segment at every existing vertex lying strictly in its
		// interior (a wall drawn through a junction splits there). Endpoints a/b
		// coinciding with existing vertices are ordinary joins, handled by
		// findOrCreateVertex below, not added as interior breaks.
		std::vector<geometry::Vec2i64> breakPoints;
		breakPoints.push_back(a);
		breakPoints.push_back(b);
		for (const Vertex& v : vertices_) {
			if (pointOnSegmentInterior(v.pos, a, b)) {
				breakPoints.push_back(v.pos);
			}
		}
		std::sort(breakPoints.begin(), breakPoints.end(), [&](const geometry::Vec2i64& p, const geometry::Vec2i64& q) {
			return paramAlong(p, a, b) < paramAlong(q, a, b);
		});
		breakPoints.erase(std::unique(breakPoints.begin(), breakPoints.end()), breakPoints.end());

		// Resolve each break point to a vertex id. A break point that is NOT an
		// existing vertex but lands on an existing segment's interior triggers a
		// T-junction split of that host segment (which creates the vertex). The
		// segment list is rescanned each time because a prior split rewrites it.
		std::vector<VertexId> chainVertices;
		chainVertices.reserve(breakPoints.size());
		for (const geometry::Vec2i64& p : breakPoints) {
			VertexId existing = vertexAt(p);
			if (existing != kInvalidVertex) {
				chainVertices.push_back(existing);
				continue;
			}
			SegmentId hostSegment = kInvalidSegment;
			for (const WallSegment& s : segments_) {
				const Vertex* sv0 = findVertex(s.v0);
				const Vertex* sv1 = findVertex(s.v1);
				if (sv0 == nullptr || sv1 == nullptr) {
					continue;
				}
				if (pointOnSegmentInterior(p, sv0->pos, sv1->pos)) {
					hostSegment = s.id;
					break;
				}
			}
			if (hostSegment != kInvalidSegment) {
				chainVertices.push_back(splitSegmentAt(hostSegment, p));
			} else {
				chainVertices.push_back(findOrCreateVertex(p));
			}
		}

		// Create the chain of new segments between consecutive break vertices.
		// A pair already joined by a segment is a duplicate and is skipped (the
		// whole-commit status reports the first such rejection only if nothing
		// else was created; see below).
		SegmentId firstCreated = kInvalidSegment;
		bool	  anyDuplicate = false;
		for (std::size_t i = 0; i + 1 < chainVertices.size(); ++i) {
			const VertexId u = chainVertices[i];
			const VertexId w = chainVertices[i + 1];
			if (u == w) {
				continue;
			}

			bool		  duplicate = false;
			const Vertex* uv = findVertex(u);
			if (uv != nullptr) {
				for (const SegmentId sid : uv->segments) {
					const WallSegment* existing = getSegment(sid);
					if (existing != nullptr && (existing->v0 == w || existing->v1 == w)) {
						duplicate = true;
						break;
					}
				}
			}
			if (duplicate) {
				anyDuplicate = true;
				continue;
			}

			WallSegment segment;
			segment.id = nextSegmentId_++;
			segment.v0 = u;
			segment.v1 = w;
			segment.material = material;
			segment.thicknessPreset = thicknessPreset;
			segment.hostFoundation = host;
			segment.state = FoundationState::Blueprint;
			segment.entity = ecs::kInvalidEntity;

			const SegmentId id = segment.id;
			segments_.push_back(std::move(segment));
			addAdjacency(u, id);
			addAdjacency(w, id);
			if (firstCreated == kInvalidSegment) {
				firstCreated = id;
			}
		}

		if (firstCreated == kInvalidSegment) {
			// Nothing was created. Either every span duplicated an existing
			// segment, or (defensively) the chain collapsed. Roll back any orphan
			// vertices the resolution step created so a rejected commit leaves no
			// trace, and report Duplicate when that was the cause.
			for (const geometry::Vec2i64& p : breakPoints) {
				const VertexId vid = vertexAt(p);
				if (vid != kInvalidVertex) {
					pruneVertexIfOrphan(vid);
				}
			}
			return {anyDuplicate ? SegmentStatus::Duplicate : SegmentStatus::ZeroLength, kInvalidSegment};
		}

		++version_;
		return {SegmentStatus::Ok, firstCreated};
	}

	bool ConstructionWorld::removeSegment(SegmentId id) {
		const auto it = std::find_if(segments_.begin(), segments_.end(), [id](const WallSegment& w) { return w.id == id; });
		if (it == segments_.end()) {
			return false;
		}
		const VertexId v0 = it->v0;
		const VertexId v1 = it->v1;
		segments_.erase(it);

		// Openings on a removed segment go with it (the wall they cut is gone).
		openings_.erase(
			std::remove_if(openings_.begin(), openings_.end(), [id](const Opening& o) { return o.segment == id; }), openings_.end()
		);

		removeAdjacency(v0, id);
		removeAdjacency(v1, id);
		pruneVertexIfOrphan(v0);
		pruneVertexIfOrphan(v1);
		++version_;
		return true;
	}

	SegmentId ConstructionWorld::segmentAt(const geometry::Vec2i64& point, std::int64_t pickRadiusMm) const {
		SegmentId hit = kInvalidSegment;
		for (const WallSegment& s : segments_) {
			const Vertex* v0 = getVertex(s.v0);
			const Vertex* v1 = getVertex(s.v1);
			if (v0 == nullptr || v1 == nullptr) {
				continue;
			}
			if (geometry::withinDistanceOfSegment(point, v0->pos, v1->pos, pickRadiusMm)) {
				// segments_ is in ascending-id order, so the last match is the
				// highest id: the documented tie-break (matches foundationAt).
				hit = s.id;
			}
		}
		return hit;
	}

	VertexId ConstructionWorld::vertexAt(const geometry::Vec2i64& point) const {
		for (const Vertex& v : vertices_) {
			if (v.pos == point) {
				return v.id;
			}
		}
		return kInvalidVertex;
	}

	const WallSegment* ConstructionWorld::getSegment(SegmentId id) const {
		const auto it = std::find_if(segments_.begin(), segments_.end(), [id](const WallSegment& w) { return w.id == id; });
		return it == segments_.end() ? nullptr : &*it;
	}

	const Vertex* ConstructionWorld::getVertex(VertexId id) const {
		const auto it = std::find_if(vertices_.begin(), vertices_.end(), [id](const Vertex& v) { return v.id == id; });
		return it == vertices_.end() ? nullptr : &*it;
	}

	const Opening* ConstructionWorld::getOpening(OpeningId id) const {
		const auto it = std::find_if(openings_.begin(), openings_.end(), [id](const Opening& o) { return o.id == id; });
		return it == openings_.end() ? nullptr : &*it;
	}

	std::vector<SegmentId> ConstructionWorld::segmentsOfVertex(VertexId id) const {
		const Vertex* v = getVertex(id);
		return v == nullptr ? std::vector<SegmentId>{} : v->segments;
	}

	bool ConstructionWorld::setSegmentState(SegmentId id, FoundationState state) {
		WallSegment* s = findSegment(id);
		if (s == nullptr) {
			return false;
		}
		s->state = state;
		++version_;
		return true;
	}

	bool ConstructionWorld::setSegmentEntity(SegmentId id, ecs::EntityID entity) {
		WallSegment* s = findSegment(id);
		if (s == nullptr) {
			return false;
		}
		s->entity = entity;
		++version_;
		return true;
	}

	OpeningId ConstructionWorld::addOpening(SegmentId segment, float t, std::string type, std::string material) {
		if (getSegment(segment) == nullptr) {
			return kInvalidOpening;
		}
		Opening opening;
		opening.id = nextOpeningId_++;
		opening.segment = segment;
		opening.t = std::clamp(t, 0.0F, 1.0F);
		opening.type = std::move(type);
		opening.material = std::move(material);
		opening.state = FoundationState::Blueprint;
		opening.entity = ecs::kInvalidEntity;
		const OpeningId id = opening.id;
		openings_.push_back(std::move(opening));
		++version_;
		return id;
	}

	WallSegment* ConstructionWorld::findSegment(SegmentId id) {
		const auto it = std::find_if(segments_.begin(), segments_.end(), [id](const WallSegment& w) { return w.id == id; });
		return it == segments_.end() ? nullptr : &*it;
	}

	Vertex* ConstructionWorld::findVertex(VertexId id) {
		const auto it = std::find_if(vertices_.begin(), vertices_.end(), [id](const Vertex& v) { return v.id == id; });
		return it == vertices_.end() ? nullptr : &*it;
	}

	FoundationId ConstructionWorld::foundationAt(const geometry::Vec2i64& point) const {
		FoundationId hit = kInvalidFoundation;
		for (const Foundation& foundation : foundations_) {
			const geometry::PointInPolygon where = geometry::pointInPolygon(point, foundation.ring);
			if (where == geometry::PointInPolygon::Outside) {
				continue;
			}
			// Inside or on-boundary counts as a hit. foundations_ is in ascending
			// id order, so taking the last match yields the highest id: the
			// documented tie-break for the shared-edge adjacency case.
			hit = foundation.id;
		}
		return hit;
	}

	const Foundation* ConstructionWorld::get(FoundationId id) const {
		return find(id);
	}

	Aabb ConstructionWorld::footprintAabb(FoundationId id) const {
		const Foundation* foundation = find(id);
		if (foundation == nullptr || foundation->ring.empty()) {
			return {};
		}

		const geometry::Ring& ring = foundation->ring;
		geometry::Vec2i64	  min = ring.front();
		geometry::Vec2i64	  max = ring.front();
		for (const geometry::Vec2i64& v : ring) {
			min.x = std::min(min.x, v.x);
			min.y = std::min(min.y, v.y);
			max.x = std::max(max.x, v.x);
			max.y = std::max(max.y, v.y);
		}
		return {min, max};
	}

	float ConstructionWorld::areaSquareMeters(FoundationId id) const {
		const Foundation* foundation = find(id);
		if (foundation == nullptr) {
			return 0.0F;
		}
		return static_cast<float>(geometry::signedAreaSquareMeters(foundation->ring));
	}

	bool ConstructionWorld::setState(FoundationId id, FoundationState state) {
		Foundation* foundation = find(id);
		if (foundation == nullptr) {
			return false;
		}
		foundation->state = state;
		++version_;
		return true;
	}

	bool ConstructionWorld::setEntity(FoundationId id, ecs::EntityID entity) {
		Foundation* foundation = find(id);
		if (foundation == nullptr) {
			return false;
		}
		foundation->entity = entity;
		++version_;
		return true;
	}

	Foundation* ConstructionWorld::find(FoundationId id) {
		const auto it = std::find_if(foundations_.begin(), foundations_.end(), [id](const Foundation& f) { return f.id == id; });
		return it == foundations_.end() ? nullptr : &*it;
	}

	const Foundation* ConstructionWorld::find(FoundationId id) const {
		const auto it = std::find_if(foundations_.begin(), foundations_.end(), [id](const Foundation& f) { return f.id == id; });
		return it == foundations_.end() ? nullptr : &*it;
	}

} // namespace engine::construction

#include "ConstructionWorld.h"

#include <predicates/Predicates.h>

#include <algorithm>
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
		foundation.id = nextId_++;
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

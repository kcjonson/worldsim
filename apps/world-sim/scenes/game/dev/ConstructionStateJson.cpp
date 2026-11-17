#include "scenes/game/dev/ConstructionStateJson.h"

#include "scenes/game/dev/DevJson.h"

#include <sstream>

namespace world_sim {

	namespace {

		double toMeters(double millimeters) {
			return millimeters / static_cast<double>(geometry::kMillimetersPerMeter);
		}

	} // namespace

	std::string foundationCornersJson(const construction::Foundation& foundation) {
		std::ostringstream out;
		out << "[";
		for (std::size_t corner = 0; corner < foundation.ring.size(); ++corner) {
			out << (corner == 0 ? "" : ",") << "{\"x\":" << jsonNumber(toMeters(static_cast<double>(foundation.ring[corner].x)))
				<< ",\"y\":" << jsonNumber(toMeters(static_cast<double>(foundation.ring[corner].y))) << "}";
		}
		out << "]";
		return out.str();
	}

	std::string openingsJson(const construction::ConstructionWorld& world, const BlueprintFragment& blueprintFragment) {
		std::ostringstream out;
		out << "[";
		bool first = true;
		for (const auto& opening : world.openings()) {
			out << (first ? "" : ",");
			first = false;
			out << "{\"id\":" << static_cast<unsigned long long>(opening.id) << ",\"type\":\"" << jsonEscape(opening.type) << "\",\"material\":\""
				<< jsonEscape(opening.material) << "\",\"wall\":" << static_cast<unsigned long long>(opening.segment)
				<< ",\"fractionAlongWall\":" << jsonNumber(static_cast<double>(opening.t)) << ",\"state\":\""
				<< (opening.state == construction::FoundationState::Built ? "Built" : "Blueprint") << "\",\"entity\":"
				<< static_cast<unsigned long long>(opening.entity);

			const auto* segment = world.getSegment(opening.segment);
			const auto* start = segment != nullptr ? world.getVertex(segment->v0) : nullptr;
			const auto* end = segment != nullptr ? world.getVertex(segment->v1) : nullptr;
			if (start != nullptr && end != nullptr) {
				const double along = static_cast<double>(opening.t);
				out << ",\"x\":" << jsonNumber(toMeters(static_cast<double>(start->pos.x) + (static_cast<double>(end->pos.x) - static_cast<double>(start->pos.x)) * along))
					<< ",\"y\":" << jsonNumber(toMeters(static_cast<double>(start->pos.y) + (static_cast<double>(end->pos.y) - static_cast<double>(start->pos.y)) * along));
			}

			if (opening.entity != ecs::kInvalidEntity) {
				out << blueprintFragment(opening.entity);
			}
			out << "}";
		}
		out << "]";
		return out.str();
	}

} // namespace world_sim

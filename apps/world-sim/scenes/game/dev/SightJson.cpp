#include "scenes/game/dev/SightJson.h"

#include "scenes/game/dev/DevJson.h"

#include <visibility/ForestDepth.h>

namespace world_sim {

	namespace {
		std::string metres(const geometry::Vec2i64& millimetres) {
			return "{\"x\":" + jsonNumber(static_cast<double>(millimetres.x) / 1000.0) + ",\"y\":" +
				   jsonNumber(static_cast<double>(millimetres.y) / 1000.0) + "}";
		}

		std::string bodyJson(const vision::SightBody& body) {
			const bool	placed = body.source == vision::SightBody::Source::Placed;
			std::string out	   = "{\"source\":\"" + std::string(placed ? "placed" : "wall") + "\",\"kind\":\"" +
							  std::string(body.kind == vision::BodyKind::Opaque ? "opaque" : "soft") + "\"";
			out += placed ? ",\"serial\":" + std::to_string(body.id) + ",\"defName\":\"" + jsonEscape(body.defName) + "\""
						  : ",\"wallBody\":" + std::to_string(body.id);
			out += ",\"diameterMm\":" + std::to_string(body.diameterMm) + ",\"centre\":" + metres(body.centre) +
				   ",\"corners\":" + std::to_string(body.edges.size()) +
				   ",\"arrival\":" + jsonNumber(static_cast<double>(body.arrival) / static_cast<double>(geometry::kFractionOne)) + "}";
			return out;
		}
	} // namespace

	std::string sightBodiesJson(std::span<const ColonistSightBodies> colonists) {
		std::string out = "{\"colonists\":[";
		for (std::size_t index = 0; index < colonists.size(); ++index) {
			const ColonistSightBodies& colonist = colonists[index];
			out += (index == 0 ? "" : ",");
			out += "{\"id\":" + std::to_string(colonist.id) + ",\"name\":\"" + jsonEscape(colonist.name) + "\",\"eye\":" +
				   metres(colonist.eyeCentre) + ",\"leadMm\":" + std::to_string(colonist.leadMm) +
				   ",\"reachMm\":" + std::to_string(colonist.reachMm) + ",\"path\":\"" + jsonEscape(colonist.path) + "\",\"rangesMm\":[";
			if (colonist.ranges != nullptr) {
				for (std::size_t side = 0; side < colonist.ranges->size(); ++side) {
					out += (side == 0 ? "" : ",") + std::to_string((*colonist.ranges)[side]);
				}
			}
			out += "],\"bodies\":[";
			if (colonist.bodies != nullptr) {
				for (std::size_t body = 0; body < colonist.bodies->size(); ++body) {
					out += (body == 0 ? "" : ",") + bodyJson((*colonist.bodies)[body]);
				}
			}
			out += "],\"shadowEdges\":[";
			if (colonist.shadowEdges != nullptr) {
				for (std::size_t edge = 0; edge < colonist.shadowEdges->size(); ++edge) {
					const vision::ShadowEdge& shadow = (*colonist.shadowEdges)[edge];
					out += (edge == 0 ? "" : ",");
					out += "{\"body\":" + std::to_string(shadow.body) + ",\"corner\":" + std::to_string(shadow.corner) +
						   ",\"litTurn\":" + std::to_string(shadow.litTurn) + "}";
				}
			}
			out += "]}";
		}
		return out + "]}";
	}

	std::string sightTunablesJson(const vision::SightTunables& tunables) {
		const vision::SightTunables start;
		std::string					out = "{\"tunables\":[";
		bool						first = true;
		for (const vision::SightTunableRange& range : vision::sightTunableRanges()) {
			out += (first ? "" : ",");
			first = false;
			out += "{\"name\":\"" + std::string(range.name) + "\",\"value\":" + std::to_string(tunables.*range.field) +
				   ",\"least\":" + std::to_string(range.least) + ",\"most\":" + std::to_string(range.most) +
				   ",\"start\":" + std::to_string(start.*range.field) + "}";
		}
		return out + "]}";
	}

	bool setSightTunable(vision::SightTunables& tunables, const std::string& name, std::int64_t value) {
		for (const vision::SightTunableRange& range : vision::sightTunableRanges()) {
			if (name == range.name) {
				tunables.*range.field = value;
				return true;
			}
		}
		return false;
	}

} // namespace world_sim

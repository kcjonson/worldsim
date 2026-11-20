#include "scenes/game/dev/SightJson.h"

#include "scenes/game/dev/DevJson.h"

#include <visibility/ForestDepth.h>
#include <visibility/ForestShape.h>
#include <visibility/SightPolygon.h>

#include <algorithm>

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
			// How far the forest shape's support along each side's normal falls short of its range times
			// cos(pi / kSightSides): what convexity takes (sight-softening.md 10, limit 7).
			out += "],\"convexityTakesMm\":[";
			if (colonist.ranges != nullptr) {
				const geometry::Ring forest = geometry::forestShape({0, 0}, *colonist.ranges);
				for (std::size_t side = 0; side < colonist.ranges->size(); ++side) {
					std::int64_t support = 0;
					for (std::size_t vertex = 0; vertex < forest.size(); ++vertex) {
						const std::int64_t along = geometry::kSightSideNormals[side][0] * forest[vertex].x + geometry::kSightSideNormals[side][1] * forest[vertex].y;
						support					 = vertex == 0 ? along : std::max(support, along);
					}
					const std::int64_t rim = (*colonist.ranges)[side] * geometry::kSightCosHalfSide / geometry::kSightDirectionScale;
					out += (side == 0 ? "" : ",") + std::to_string(rim - support / geometry::kSightSideNormalScale);
				}
			}
			out += "],\"rangeChangeMm\":" + std::to_string(colonist.rangeChangeMm) +
				   ",\"greatestRangeChangeMm\":" + std::to_string(colonist.greatestRangeChangeMm) + ",\"bodies\":[";
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

	std::string coverJson(const worldview::VisibilityCoverRenderer& cover, std::int64_t featherMm) {
		constexpr double kDegrees = 180.0 / 3.14159265358979323846;
		const auto		 number	  = [](double value) { return jsonNumber(value); };
		const auto		 count	  = [](std::size_t value) { return std::to_string(value); };
		const worldview::VisibilityCoverRenderer::Counts& counts = cover.counts();
		const worldview::SeenFeather::Counts&			  seen	 = cover.seenFeather().counts();
		std::string out = "{\"drawn\":" + std::string(cover.layers().drawn ? "true" : "false") + ",\"tintAlpha\":" + number(worldview::kSeenDimAlpha);
		out += ",\"counts\":{\"colonists\":" + count(counts.colonists) + ",\"featherPieces\":" + count(counts.featherPieces) +
			   ",\"softBodies\":" + count(counts.softBodies) + ",\"shadowEdges\":" + count(counts.shadowEdges) +
			   ",\"softPieces\":" + count(counts.softPieces) + ",\"bandPieces\":" + count(counts.bandPieces) +
			   ",\"featherRebuilds\":" + count(counts.featherRebuilds) + ",\"softRingsBuilt\":" + count(counts.softRingsBuilt) +
			   ",\"softRingsDue\":" + count(counts.softRingsDue) + ",\"softBodiesBuilt\":" + count(counts.softBodiesBuilt) + ",\"softOldestDueFrames\":" + std::to_string(counts.softOldestDueFrames) +
			   ",\"bandRebuilds\":" + count(counts.bandRebuilds) + ",\"softPiecesBuilt\":" + count(counts.softPiecesBuilt) +
			   ",\"piecesBuilt\":" + count(counts.piecesBuilt) + ",\"meshUploads\":" + count(counts.meshUploads) +
			   ",\"tintElements\":" + count(counts.tintElements) + ",\"unseenElements\":" + count(counts.unseenElements) +
			   ",\"buildMicroseconds\":" + number(counts.buildMicroseconds) + ",\"seenMicroseconds\":" + number(counts.seenMicroseconds) +
			   ",\"uploadMicroseconds\":" + number(counts.uploadMicroseconds) + ",\"brightMicroseconds\":" + number(counts.brightMicroseconds) +
			   ",\"composeMicroseconds\":" + number(counts.composeMicroseconds) + "}";
		out += ",\"seen\":{\"cellsDrawn\":" + count(seen.cellsDrawn) + ",\"cellsVisited\":" + count(seen.cellsVisited) + ",\"cellsRebuilt\":" + count(seen.cellsRebuilt) + ",\"cellsWaiting\":" + count(seen.cellsWaiting) +
			   ",\"cellEdgesRead\":" + count(seen.cellEdgesRead) + ",\"epochCellsLast\":" + count(seen.epochCellsLast) +
			   ",\"epochEdgesLast\":" + count(seen.epochEdgesLast) + ",\"changedBoxes\":" + count(seen.changedBoxes) +
			   ",\"pendingShapes\":" + count(seen.pendingShapes) + ",\"clusters\":" + count(seen.clusters) + ",\"clustersCut\":" + count(seen.clustersCut) +
			   ",\"cutShapes\":" + count(seen.cut.shapes) + ",\"cutShapeSides\":" + count(seen.cut.shapeSides) +
			   ",\"cutRecordEdges\":" + count(seen.cut.recordEdges) + ",\"cutKept\":" + count(seen.cut.kept) +
			   ",\"pendingPieces\":" + count(seen.pendingPieces) + "}";
		out += ",\"colonists\":[";
		bool firstColonist = true;
		for (const worldview::VisibilityCoverRenderer::ColonistCover& colonist : cover.colonists()) {
			out += firstColonist ? "" : ",";
			firstColonist = false;
			const double moved = static_cast<double>(colonist.rimMovedMm);
			out += "{\"id\":" + std::to_string(static_cast<std::uint64_t>(colonist.colonist)) + ",\"brightSides\":" + count(colonist.bright->size()) +
				   ",\"rimSides\":" + count(colonist.rimSides) + ",\"featherPieces\":" + count(colonist.feather != nullptr ? colonist.feather->size() : 0) +
				   ",\"rimMovedMm\":" + std::to_string(colonist.rimMovedMm) +
				   ",\"rimMovedOnScreen\":" + number(moved / static_cast<double>(featherMm) * worldview::kSeenDimAlpha);
			out += ",\"softPieces\":" + count(colonist.shadows != nullptr ? colonist.shadows->softPieceCount() : 0) + ",\"bandPieces\":" + count(colonist.shadows != nullptr ? colonist.shadows->bandPieces().size() : 0) + ",\"soft\":[";
			if (colonist.shadows != nullptr) {
				bool first = true;
				for (std::size_t ring = 0; ring < worldview::DrawnShadows::kSoftRings; ++ring) {
					for (const worldview::DrawnShadows::SoftDrawn& soft : colonist.shadows->rings()[ring].drawn) {
					out += first ? "" : ",";
					first = false;
					out += "{\"serial\":" + std::to_string(soft.bodyId) + ",\"diameterMm\":" + std::to_string(soft.diameterMm) +
						   ",\"widthAsSeenMm\":" + number(soft.shadow.widthAsSeen) + ",\"distanceMm\":" + number(soft.shadow.distance) +
						   ",\"footMask\":" + number(soft.shadow.footValue) + ",\"footOnScreen\":" + number(soft.shadow.footValue * worldview::kSeenDimAlpha) +
						   ",\"shadowLengthMm\":" + number(soft.shadow.shadowLength) + ",\"pieces\":" + std::to_string(soft.pieces) + ",\"ring\":" + std::to_string(ring) + ",\"home\":" + (soft.home ? "true" : "false") + "}";
					}
				}
			}
			out += "],\"edges\":[";
			if (colonist.shadows != nullptr) {
				bool first = true;
				for (const worldview::DrawnShadows::EdgeDrawn& edge : colonist.shadows->edges()) {
					out += first ? "" : ",";
					first = false;
					out += "{\"body\":" + std::to_string(edge.bodyId) + ",\"diameterMm\":" + std::to_string(edge.diameterMm) +
						   ",\"buildCorner\":" + std::to_string(edge.buildCorner) + ",\"followedCorner\":" + std::to_string(edge.followedCorner) +
						   ",\"litTurn\":" + std::to_string(edge.band.litTurn) + ",\"apex\":{\"x\":" + number(edge.band.apex.x / 1000.0) +
						   ",\"y\":" + number(edge.band.apex.y / 1000.0) + "},\"ease\":" + number(edge.ease) +
						   ",\"cornerDistanceMm\":" + number(edge.band.cornerDistance) + ",\"wideningMm\":" + number(edge.band.widening) +
						   ",\"litDegrees\":" + number(edge.band.litAngle * kDegrees) + ",\"shadowDegrees\":" + number(edge.band.shadowAngle * kDegrees) +
						   ",\"finDegrees\":" + number(edge.band.finAngle * kDegrees) + "}";
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

#include "scenes/game/dev/DrawingStateJson.h"

#include "scenes/game/dev/DevJson.h"
#include "scenes/game/world/construction/DrawingStatusHint.h"
#include "scenes/game/world/construction/KeyGuideVisibility.h"

#include <sstream>

namespace world_sim {

	namespace {

		const char* toolName(const std::optional<ToolKind>& tool) {
			if (!tool) {
				return "none";
			}
			switch (*tool) {
				case ToolKind::Foundation: return "foundationFreeform";
				case ToolKind::FoundationRectangle: return "foundationRectangle";
				case ToolKind::FoundationEdit: return "foundationEdit";
				case ToolKind::Wall: return "wall";
				case ToolKind::Opening: return "opening";
			}
			return "none";
		}

		const char* foundationToolName(FoundationTool tool) {
			return tool == FoundationTool::Rectangle ? "rectangle" : "freeform";
		}

		const char* snapKindName(construction::SnapKind kind) {
			switch (kind) {
				case construction::SnapKind::None: return "None";
				case construction::SnapKind::Angle: return "Angle";
				case construction::SnapKind::Vertex: return "Vertex";
				case construction::SnapKind::Edge: return "Edge";
				case construction::SnapKind::Origin: return "Origin";
				case construction::SnapKind::WallEndpoint: return "WallEndpoint";
				case construction::SnapKind::WallSegment: return "WallSegment";
				case construction::SnapKind::GuideLine: return "GuideLine";
				case construction::SnapKind::GuideCrossing: return "GuideCrossing";
			}
			return "None";
		}

		const char* rectangleSnapKindName(construction::RectangleSnapKind kind) {
			switch (kind) {
				case construction::RectangleSnapKind::None: return "None";
				case construction::RectangleSnapKind::Vertex: return "Vertex";
				case construction::RectangleSnapKind::Edge: return "Edge";
				case construction::RectangleSnapKind::Grid: return "Grid";
			}
			return "None";
		}

		const char* guideSourceName(construction::GuideSource source) {
			switch (source) {
				case construction::GuideSource::DrawnShape: return "DrawnShape";
				case construction::GuideSource::NearbyStructure: return "NearbyStructure";
				case construction::GuideSource::WorldAxes: return "WorldAxes";
			}
			return "DrawnShape";
		}

		const char* guideRelationName(construction::GuideRelation relation) {
			switch (relation) {
				case construction::GuideRelation::Parallel: return "Parallel";
				case construction::GuideRelation::Perpendicular: return "Perpendicular";
				case construction::GuideRelation::AngleStep: return "AngleStep";
			}
			return "Parallel";
		}

		const char* lengthSnapName(construction::LengthSnapKind kind) {
			switch (kind) {
				case construction::LengthSnapKind::None: return "None";
				case construction::LengthSnapKind::Rounded: return "Rounded";
				case construction::LengthSnapKind::RoundedAlongGuide: return "RoundedAlongGuide";
				case construction::LengthSnapKind::CommonLength: return "CommonLength";
			}
			return "None";
		}

		std::string pointJson(Foundation::Vec2 point) {
			return "{\"x\":" + jsonNumber(static_cast<double>(point.x)) + ",\"y\":" + jsonNumber(static_cast<double>(point.y)) + "}";
		}

		std::string pointListJson(const std::vector<Foundation::Vec2>& points) {
			std::string out = "[";
			for (std::size_t index = 0; index < points.size(); ++index) {
				out += (index == 0 ? "" : ",") + pointJson(points[index]);
			}
			return out + "]";
		}

		std::string snapJson(const DrawingSnapshot::Snap& snap, bool rectangleTool) {
			std::ostringstream out;
			out << "{\"point\":" << pointJson(snap.point) << ",\"kind\":\""
				<< (rectangleTool ? rectangleSnapKindName(snap.rectangleKind) : snapKindName(snap.kind)) << "\",\"guides\":[";
			for (std::size_t index = 0; index < snap.guides.size(); ++index) {
				const construction::SnapGuide& guide = snap.guides[index];
				out << (index == 0 ? "" : ",") << "{\"source\":\"" << guideSourceName(guide.source) << "\",\"relation\":\""
					<< guideRelationName(guide.relation) << "\",\"from\":" << pointJson(guide.from)
					<< ",\"distanceFromNodeMeters\":" << jsonNumber(static_cast<double>(guide.distanceFromNodeMeters)) << "}";
			}
			out << "],\"lengthSnap\":\"" << lengthSnapName(snap.lengthSnap) << "\",\"lengthMeters\":" << jsonNumber(static_cast<double>(snap.lengthMeters))
				<< ",\"segmentLengthMeters\":" << (snap.segmentLengthMeters ? jsonNumber(static_cast<double>(*snap.segmentLengthMeters)) : "null")
				<< ",\"matchedEdgeCount\":" << snap.matchedEdgeCount << ",\"commonLengthsMeters\":[";
			for (std::size_t index = 0; index < snap.commonLengths.size(); ++index) {
				out << (index == 0 ? "" : ",") << jsonNumber(static_cast<double>(snap.commonLengths[index]));
			}
			out << "],\"closesShape\":" << (snap.closesShape ? "true" : "false") << "}";
			return out.str();
		}

		std::string statusJson(const DrawingSnapshot& snapshot) {
			const DrawingStatus& status = snapshot.status;
			std::ostringstream	 out;
			out << "{\"active\":" << (status.active ? "true" : "false") << ",\"material\":\"" << jsonEscape(status.material)
				<< "\",\"pointCount\":" << status.pointCount << ",\"areaSquareMeters\":" << jsonNumber(static_cast<double>(status.areaSquareMeters))
				<< ",\"valid\":" << (status.valid ? "true" : "false") << ",\"message\":\"" << jsonEscape(status.message)
				<< "\",\"hint\":\"" << jsonEscape(statusHint(status).text) << "\"";
			if (status.wall) {
				out << ",\"thicknessPreset\":\"" << jsonEscape(status.thicknessPreset)
					<< "\",\"segmentLengthMeters\":" << jsonNumber(static_cast<double>(status.segmentLengthMeters))
					<< ",\"totalLengthMeters\":" << jsonNumber(static_cast<double>(status.totalLengthMeters))
					<< ",\"wallCost\":" << jsonNumber(static_cast<double>(status.wallCost)) << ",\"wallWork\":" << jsonNumber(static_cast<double>(status.wallWork));
			}
			if (status.foundationEdit) {
				out << ",\"editLabel\":\"" << jsonEscape(status.editLabel)
					<< "\",\"areaDeltaSquareMeters\":" << jsonNumber(static_cast<double>(status.areaDeltaSquareMeters));
			}
			if (status.opening) {
				out << ",\"openingType\":\"" << jsonEscape(status.openingType)
					<< "\",\"openingWidthMeters\":" << jsonNumber(static_cast<double>(status.openingWidthMeters));
			}
			out << "}";
			return out.str();
		}

		std::string settingsJson(const DrawingSettings& settings) {
			const auto flag = [](bool value) { return value ? "true" : "false"; };
			// Binding every member fails to compile when DrawingSettings gains one, so a new setting cannot go unreported.
			const auto& [angleSteps, guidesFromShape, alignToExistingBuildings, guidesFromWorldAxes, guideCrossings, commonLengths, guideDistances, angleReadout, roundLengths, showKeyGuide, rectangleGridSpacingMeters] =
				settings;
			std::ostringstream out;
			out << "{\"angleSteps\":" << flag(angleSteps) << ",\"guidesFromShape\":" << flag(guidesFromShape)
				<< ",\"alignToExistingBuildings\":" << flag(alignToExistingBuildings)
				<< ",\"guidesFromWorldAxes\":" << flag(guidesFromWorldAxes) << ",\"guideCrossings\":" << flag(guideCrossings)
				<< ",\"commonLengths\":" << flag(commonLengths) << ",\"guideDistances\":" << flag(guideDistances)
				<< ",\"angleReadout\":" << flag(angleReadout) << ",\"roundLengths\":" << flag(roundLengths)
				<< ",\"showKeyGuide\":" << flag(showKeyGuide)
				<< ",\"rectangleGridSpacingMeters\":" << jsonNumber(static_cast<double>(rectangleGridSpacingMeters)) << "}";
			return out.str();
		}

	} // namespace

	std::string drawingStateJson(const DrawingSnapshot& snapshot, const DrawingStateContext& context) {
		const bool rectangleTool = snapshot.tool == ToolKind::FoundationRectangle;
		std::ostringstream out;
		out << "{\"tool\":\"" << toolName(snapshot.tool) << "\",\"lastFoundationTool\":\"" << foundationToolName(snapshot.lastFoundationTool)
			<< "\",\"followingPointer\":" << (snapshot.followingPointer ? "true" : "false")
			<< ",\"pointer\":" << (snapshot.pointer ? pointJson(*snapshot.pointer) : "null") << ",\"points\":" << pointListJson(snapshot.points);

		if (rectangleTool) {
			out << ",\"rectangle\":{\"pressPending\":" << (snapshot.pressPending ? "true" : "false") << ",\"firstCorner\":"
				<< (snapshot.points.empty() ? "null" : pointJson(snapshot.points.front())) << ",\"corners\":" << pointListJson(snapshot.rectangleCorners)
				<< ",\"sizeLabel\":\"" << jsonEscape(snapshot.rectangleLabel) << "\"}";
		}
		if (snapshot.tool == ToolKind::FoundationEdit && snapshot.editMode) {
			out << ",\"edit\":{\"target\":" << static_cast<unsigned long long>(snapshot.editTarget) << ",\"mode\":\""
				<< (*snapshot.editMode == construction::FoundationEditMode::Add ? "Add" : "Subtract") << "\"}";
		}
		if (snapshot.tool == ToolKind::Wall) {
			out << ",\"wall\":{\"hostFoundation\":" << static_cast<unsigned long long>(snapshot.wallHost) << "}";
		}
		if (snapshot.tool == ToolKind::Opening) {
			out << ",\"opening\":{\"type\":\"" << jsonEscape(snapshot.openingType) << "\",\"snap\":";
			if (snapshot.openingSnap && snapshot.openingSnap->valid) {
				out << "{\"wall\":" << static_cast<unsigned long long>(snapshot.openingSnap->segment)
					<< ",\"fractionAlongWall\":" << jsonNumber(static_cast<double>(snapshot.openingSnap->t)) << ",\"point\":" << pointJson(snapshot.openingSnap->point) << "}";
			} else {
				out << "null";
			}
			out << "}";
		}

		out << ",\"snap\":" << (snapshot.snap ? snapJson(*snapshot.snap, rectangleTool) : "null") << ",\"verdict\":{\"valid\":"
			<< (snapshot.verdict.valid ? "true" : "false") << ",\"proposed\":" << (snapshot.verdict.proposed ? "true" : "false")
			<< ",\"reason\":\"" << jsonEscape(snapshot.verdict.reason) << "\"},\"status\":" << statusJson(snapshot)
			<< ",\"settings\":" << settingsJson(context.settings) << ",\"keyGuide\":{\"shown\":"
			<< (keyGuideShown(snapshot.tool, context.settings) ? "true" : "false")
			<< ",\"collapsed\":" << (context.keyGuideCollapsed ? "true" : "false") << "},\"dialogOpen\":" << (context.dialogOpen ? "true" : "false") << "}";
		return out.str();
	}

} // namespace world_sim

#include "RectangleSnap.h"

#include "SnapEngine.h"

#include <assets/ConstructionRegistry.h>
#include <core/Vec2i64.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace construction {

	namespace {

		constexpr double kEdgeAxisToleranceMeters = 1e-9;
		constexpr double kMinimumSideMeters = 0.001;

		// Round half up, the way the grid is defined (not half away from zero), so a
		// negative coordinate halfway between grid lines goes toward +infinity.
		double onGrid(double value, double spacing) {
			return spacing > 0.0 ? std::floor(value / spacing + 0.5) * spacing : value;
		}

		// A side to the millimetre without trailing zeros: "4", "3.5", "0.125". A value
		// exactly halfway between two millimetres (a multiple of 1/16 m that is odd) rounds
		// up, as the prototype's toFixed does; printf would round it to even.
		std::string trimmedMetres(double meters) {
			double rounded = meters;
			const double sixteenths = meters * 16.0;
			if (sixteenths == std::floor(sixteenths) && std::fmod(sixteenths, 2.0) == 1.0) {
				rounded = std::floor(meters * 1000.0 + 0.5) / 1000.0;
			}
			char buffer[48];
			std::snprintf(buffer, sizeof(buffer), "%.3f", rounded);
			std::string text = buffer;
			while (text.back() == '0') {
				text.pop_back();
			}
			if (text.back() == '.') {
				text.pop_back();
			}
			return text + " m";
		}

	} // namespace

	RectangleCornerSnap snapRectangleCorner(
		const ConstructionWorld&	  world,
		const assets::SnappingConfig& snapping,
		const DrawingAids&			  aids,
		::Foundation::Vec2			  cursor,
		bool						  freeform
	) {
		if (freeform) {
			return {cursor, RectangleSnapKind::None};
		}
		const double spacing = static_cast<double>(aids.rectangleGridSpacingMeters);

		if (snapping.rectangleAdjacencySnaps) {
			if (const auto corner = nearestFoundationCorner(world, cursor, snapping.vertexSnapRadiusMeters)) {
				return {corner->point, RectangleSnapKind::Vertex};
			}
			if (const auto edge = nearestFoundationEdge(world, cursor, snapping.edgeSnapRadiusMeters)) {
				const geometry::Ring& ring = world.get(edge->foundation)->ring;
				const ::Foundation::Vec2 start = geometry::dequantize(ring[edge->edgeIndex]);
				const ::Foundation::Vec2 end = geometry::dequantize(ring[(edge->edgeIndex + 1) % ring.size()]);
				const double			 startX = start.x;
				const double			 startY = start.y;
				const double			 endX = end.x;
				const double			 endY = end.y;
				const bool horizontal = std::abs(startY - endY) < kEdgeAxisToleranceMeters;
				const bool vertical = std::abs(startX - endX) < kEdgeAxisToleranceMeters;
				if (horizontal || vertical) {
					const double footX = edge->point.x;
					const double footY = edge->point.y;
					const double slidX = horizontal ? onGrid(footX, spacing) : footX;
					const double slidY = horizontal ? footY : onGrid(footY, spacing);
					const double edgeX = endX - startX;
					const double edgeY = endY - startY;
					const double along = ((slidX - startX) * edgeX + (slidY - startY) * edgeY) / (edgeX * edgeX + edgeY * edgeY);
					if (along >= 0.0 && along <= 1.0) {
						return {{static_cast<float>(slidX), static_cast<float>(slidY)}, RectangleSnapKind::Edge};
					}
				}
				return {edge->point, RectangleSnapKind::Edge};
			}
		}

		return {
			{static_cast<float>(onGrid(cursor.x, spacing)), static_cast<float>(onGrid(cursor.y, spacing))}, RectangleSnapKind::Grid
		};
	}

	std::optional<RectangleShape> rectangleFromCorners(::Foundation::Vec2 firstCorner, ::Foundation::Vec2 secondCorner) {
		const double width = std::abs(static_cast<double>(secondCorner.x) - static_cast<double>(firstCorner.x));
		const double depth = std::abs(static_cast<double>(secondCorner.y) - static_cast<double>(firstCorner.y));
		if (!(width > kMinimumSideMeters && depth > kMinimumSideMeters)) {
			return std::nullopt;
		}
		const float left = std::min(firstCorner.x, secondCorner.x);
		const float right = std::max(firstCorner.x, secondCorner.x);
		const float bottom = std::min(firstCorner.y, secondCorner.y);
		const float top = std::max(firstCorner.y, secondCorner.y);

		RectangleShape shape;
		shape.ring = {
			geometry::quantize({left, bottom}),
			geometry::quantize({right, bottom}),
			geometry::quantize({right, top}),
			geometry::quantize({left, top}),
		};
		shape.widthMeters = static_cast<float>(width);
		shape.depthMeters = static_cast<float>(depth);
		shape.label = trimmedMetres(width) + " x " + trimmedMetres(depth);
		return shape;
	}

} // namespace construction

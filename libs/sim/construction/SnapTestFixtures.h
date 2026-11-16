#pragma once

// Test-only helpers shared by the snap engine tests (building-construction D12, Tests):
// the drawing aids the tuned prototype's cases run with, its polar and rectangle helpers,
// the worked example's points, and a world committed the way the tools commit one.

#include "ConstructionWorld.h"
#include "DrawingAids.h"
#include "SnapEngine.h"
#include "SnapGuidesDetail.h"

#include <assets/ConstructionRegistry.h>
#include <core/Vec2i64.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <numbers>
#include <utility>
#include <vector>

namespace construction::snap_test {

	using ::Foundation::Vec2;

	inline constexpr double		  kRadiansPerDegree = std::numbers::pi / 180.0;
	inline constexpr std::int64_t kWallHalfThicknessMm = 100; // a 0.2 m wall

	/// Every guide helper and the angle step on, common lengths and length rounding off:
	/// the prototype's legacy mode, in which the guide cases are checked.
	inline DrawingAids guideAidsOn() {
		DrawingAids aids;
		aids.angleSteps = true;
		aids.guidesFromShape = true;
		aids.alignToExistingBuildings = true;
		aids.guidesFromWorldAxes = true;
		aids.guideCrossings = true;
		return aids;
	}

	/// Every aid on, common lengths and length rounding included: the prototype's page
	/// defaults, in which D12's length cases are checked.
	inline DrawingAids everyAidOn() {
		DrawingAids aids = guideAidsOn();
		aids.commonLengths = true;
		aids.roundLengths = true;
		return aids;
	}

	inline DrawingAids allAidsOff() {
		return DrawingAids{};
	}

	/// What a player starts with: the engine-facing part of the shipped Drawing Settings'
	/// in-code defaults, so a test built on it follows the defaults when they change.
	inline DrawingAids shippedAids() {
		const assets::DrawingSettingsDefaults defaults;
		return DrawingAids{
			.angleSteps = defaults.angleSteps,
			.guidesFromShape = defaults.guidesFromShape,
			.alignToExistingBuildings = defaults.alignToExistingBuildings,
			.guidesFromWorldAxes = defaults.guidesFromWorldAxes,
			.guideCrossings = defaults.guideCrossings,
			.commonLengths = defaults.commonLengths,
			.roundLengths = defaults.roundLengths,
			.rectangleGridSpacingMeters = defaults.rectangleGridSpacingMeters,
		};
	}

	inline SnapInput foundationInput(const DrawingAids& aids = guideAidsOn()) {
		return SnapInput{.aids = aids};
	}

	inline SnapInput wallInput(const DrawingAids& aids = guideAidsOn()) {
		return SnapInput{.aids = aids, .wallHalfThicknessMm = kWallHalfThicknessMm};
	}

	/// The point `distance` from `origin` at `angleDegrees` from +x.
	inline Vec2 polar(Vec2 origin, double angleDegrees, double distance) {
		return {
			static_cast<float>(static_cast<double>(origin.x) + std::cos(angleDegrees * kRadiansPerDegree) * distance),
			static_cast<float>(static_cast<double>(origin.y) + std::sin(angleDegrees * kRadiansPerDegree) * distance)
		};
	}

	/// Meters to millimeters, halves rounding up, as the prototype quantizes.
	inline geometry::Vec2i64 millimeters(double x, double y) {
		return {static_cast<std::int64_t>(std::floor(x * 1000.0 + 0.5)), static_cast<std::int64_t>(std::floor(y * 1000.0 + 0.5))};
	}

	inline Vec2 meters(const geometry::Vec2i64& point) {
		return geometry::dequantize(point);
	}

	/// The ring (x, y), then `length` along `angleDegrees`, then `depth` along the angle plus
	/// 90 degrees, then back, each corner rounded to the millimeter. Counter-clockwise.
	inline geometry::Ring rectangle(double x, double y, double angleDegrees, double length, double depth) {
		const double alongX = std::cos(angleDegrees * kRadiansPerDegree);
		const double alongY = std::sin(angleDegrees * kRadiansPerDegree);
		const double acrossX = -alongY;
		const double acrossY = alongX;
		return {
			millimeters(x, y),
			millimeters(x + alongX * length, y + alongY * length),
			millimeters(x + alongX * length + acrossX * depth, y + alongY * length + acrossY * depth),
			millimeters(x + acrossX * depth, y + acrossY * depth),
		};
	}

	/// How far `point` is from the line through `node` at `angleDegrees`.
	inline double distanceFromLine(Vec2 node, double angleDegrees, Vec2 point) {
		const double offsetX = static_cast<double>(point.x) - static_cast<double>(node.x);
		const double offsetY = static_cast<double>(point.y) - static_cast<double>(node.y);
		return std::abs(offsetX * std::sin(angleDegrees * kRadiansPerDegree) - offsetY * std::cos(angleDegrees * kRadiansPerDegree));
	}

	inline double distanceBetween(Vec2 first, Vec2 second) {
		return std::hypot(
			static_cast<double>(first.x) - static_cast<double>(second.x), static_cast<double>(first.y) - static_cast<double>(second.y)
		);
	}

	/// The angle (degrees) at `corner` between the edges toward `previous` and `next`.
	inline double cornerDegrees(Vec2 previous, Vec2 corner, Vec2 next) {
		const double towardPreviousX = static_cast<double>(previous.x) - static_cast<double>(corner.x);
		const double towardPreviousY = static_cast<double>(previous.y) - static_cast<double>(corner.y);
		const double towardNextX = static_cast<double>(next.x) - static_cast<double>(corner.x);
		const double towardNextY = static_cast<double>(next.y) - static_cast<double>(corner.y);
		const double crossProduct = towardPreviousX * towardNextY - towardPreviousY * towardNextX;
		const double dotProduct = towardPreviousX * towardNextX + towardPreviousY * towardNextY;
		return std::abs(std::atan2(crossProduct, dotProduct)) / kRadiansPerDegree;
	}

	/// Placed points round to the millimeter, as a click commits them.
	inline Vec2 placed(Vec2 point) {
		return geometry::dequantize(geometry::quantize(point));
	}

	// The worked example: a 6 m by 4 m rectangle at 30 degrees, each corner to the millimeter.
	inline const Vec2 kWorkedPoint0{0.0F, 0.0F};
	inline const Vec2 kWorkedPoint1{5.196F, 3.0F};
	inline const Vec2 kWorkedPoint2{3.196F, 6.464F};
	inline const Vec2 kWorkedPoint3{-2.0F, 3.464F};

	/// A construction world built as the tools build one: foundations committed in order
	/// (ids ascending), walls committed as free-standing chains.
	struct SnapWorld {
		ConstructionWorld world;

		FoundationId addFoundation(const geometry::Ring& ring) {
			const CommitResult result = world.commitFoundation(ring, "Wood");
			EXPECT_TRUE(result.ok());
			return result.id;
		}

		FoundationId addFoundation(std::initializer_list<std::pair<double, double>> corners) {
			geometry::Ring ring;
			for (const auto& [x, y] : corners) {
				ring.push_back(millimeters(x, y));
			}
			return addFoundation(ring);
		}

		void addWall(const std::vector<geometry::Vec2i64>& chain) {
			for (std::size_t index = 0; index + 1 < chain.size(); ++index) {
				EXPECT_TRUE(world.commitSegment(chain[index], chain[index + 1], "Wood", "Standard", kInvalidFoundation).ok());
			}
		}
	};

	/// The guide study a snap reads, for asserting what a stage decided.
	inline snap_guides::GuideStudy studyGuides(
		const assets::SnappingConfig& snapping,
		const ConstructionWorld&	  world,
		const std::vector<Vec2>&	  points,
		Vec2						  cursor,
		const DrawingAids&			  aids,
		snap_guides::SnapTool		  tool = snap_guides::SnapTool::Foundation
	) {
		const snap_guides::GuideContext context{
			.snapping = &snapping,
			.world = &world,
			.points = &points,
			.cursor = {static_cast<double>(cursor.x), static_cast<double>(cursor.y)},
			.aids = aids,
			.tool = tool,
			.wallHalfThicknessMm = tool == snap_guides::SnapTool::Wall ? kWallHalfThicknessMm : 0,
		};
		snap_guides::GuideStudy study;
		snap_guides::studyGuides(context, study);
		return study;
	}

} // namespace construction::snap_test

#include "ItemSnap.h"

#include "OpeningGeometry.h"
#include "SnapEngine.h"

#include <assets/ItemView.h>
#include <core/Vec2i64.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <numbers>

namespace construction {

	namespace {

		constexpr auto	   kQuantaPerTurn = static_cast<int64_t>(assets::kTurnQuantaPerTurn);
		constexpr uint32_t kQuarterTurnQuanta = assets::kTurnQuantaPerTurn / 4;

		uint32_t wrapQuanta(int64_t quanta) {
			return static_cast<uint32_t>(((quanta % kQuantaPerTurn) + kQuantaPerTurn) % kQuantaPerTurn);
		}

		// How far `to` is from `from` going one way round, in quanta: clockwise for +1, counter-clockwise for -1.
		int64_t quantaAround(uint32_t from, uint32_t to, int direction) {
			const int64_t forward = static_cast<int64_t>(to) - static_cast<int64_t>(from);
			return wrapQuanta(direction > 0 ? forward : -forward);
		}

		int64_t quantaBetween(uint32_t first, uint32_t second) {
			const int64_t clockwise = quantaAround(first, second, 1);
			return std::min(clockwise, kQuantaPerTurn - clockwise);
		}

		int64_t quantaOfDegrees(double degrees) {
			return std::llround(degrees / assets::kTurnQuantumDegrees);
		}

		::Foundation::Vec2 closestOnSegment(::Foundation::Vec2 point, ::Foundation::Vec2 start, ::Foundation::Vec2 end) {
			const ::Foundation::Vec2 run = end - start;
			const float				 lengthSquared = glm::dot(run, run);
			if (lengthSquared <= 0.0F) {
				return start;
			}
			const float along = std::clamp(glm::dot(point - start, run) / lengthSquared, 0.0F, 1.0F);
			return start + run * along;
		}

		uint32_t directionQuanta(::Foundation::Vec2 start, ::Foundation::Vec2 end) {
			const double degrees =
				std::atan2(static_cast<double>(end.y - start.y), static_cast<double>(end.x - start.x)) * 180.0 / std::numbers::pi;
			return assets::turnQuantaFromDegrees(degrees);
		}

	} // namespace

	std::optional<uint32_t>
	itemAlignmentQuanta(const ConstructionWorld& world, ::Foundation::Vec2 pivot, const assets::SnappingConfig& snapping) {
		const float				range = snapping.itemAlignRangeMeters;
		std::optional<uint32_t> direction;
		float					best = std::numeric_limits<float>::infinity();
		if (const std::optional<FoundationEdgeHit> edge = nearestFoundationEdge(world, pivot, range)) {
			const geometry::Ring& ring = world.get(edge->foundation)->ring;
			direction = directionQuanta(
				geometry::dequantize(ring[edge->edgeIndex]), geometry::dequantize(ring[(edge->edgeIndex + 1) % ring.size()])
			);
			best = edge->distanceMeters;
		}
		for (const WallSegment& wall : world.segments()) {
			const Vertex* first = world.getVertex(wall.v0);
			const Vertex* second = world.getVertex(wall.v1);
			if (first == nullptr || second == nullptr) {
				continue;
			}
			const ::Foundation::Vec2 start = geometry::dequantize(first->pos);
			const ::Foundation::Vec2 end = geometry::dequantize(second->pos);
			const float				 halfThickness = static_cast<float>(halfThicknessMm(wall)) / 1000.0F;
			const float				 toFace = std::max(0.0F, glm::distance(pivot, closestOnSegment(pivot, start, end)) - halfThickness);
			if (toFace <= range && toFace < best) {
				best = toFace;
				direction = directionQuanta(start, end);
			}
		}
		return direction;
	}

	uint32_t
	snapItemTurn(double rawDegrees, std::optional<uint32_t> alignmentQuanta, const assets::SnappingConfig& snapping, bool freeform) {
		const uint32_t raw = assets::turnQuantaFromDegrees(rawDegrees);
		if (freeform) {
			return raw;
		}
		if (alignmentQuanta.has_value()) {
			const int64_t			window = quantaOfDegrees(snapping.effectiveGuideAngleWindowDegrees());
			std::optional<uint32_t> nearest;
			int64_t					nearestDistance = window;
			for (uint32_t quarter = 0; quarter < 4; ++quarter) {
				const uint32_t stop =
					wrapQuanta(static_cast<int64_t>(*alignmentQuanta) + static_cast<int64_t>(quarter * kQuarterTurnQuanta));
				const int64_t distance = quantaBetween(raw, stop);
				if (distance <= nearestDistance) {
					nearestDistance = distance;
					nearest = stop;
				}
			}
			if (nearest.has_value()) {
				return *nearest;
			}
		}
		const int64_t step = quantaOfDegrees(snapping.angleIncrementDegrees);
		if (step <= 0) {
			return raw;
		}
		return wrapQuanta(((static_cast<int64_t>(raw) + (step / 2)) / step) * step);
	}

	uint32_t stepItemTurn(
		uint32_t					  turnQuanta,
		int							  direction,
		std::optional<uint32_t>		  alignmentQuanta,
		const assets::SnappingConfig& snapping,
		bool						  freeform
	) {
		constexpr int64_t kFreeStepQuanta = 100; // one degree
		if (freeform) {
			return wrapQuanta(static_cast<int64_t>(turnQuanta) + (direction > 0 ? kFreeStepQuanta : -kFreeStepQuanta));
		}
		std::optional<int64_t> nearest;
		const auto			   consider = [&](uint32_t stop) {
			const int64_t around = quantaAround(turnQuanta, stop, direction);
			if (around > 0 && (!nearest.has_value() || around < *nearest)) {
				nearest = around;
			}
		};
		const int64_t step = quantaOfDegrees(snapping.angleIncrementDegrees);
		if (step > 0) {
			for (int64_t stop = 0; stop < kQuantaPerTurn; stop += step) {
				consider(static_cast<uint32_t>(stop));
			}
		}
		if (alignmentQuanta.has_value()) {
			for (uint32_t quarter = 0; quarter < 4; ++quarter) {
				consider(wrapQuanta(static_cast<int64_t>(*alignmentQuanta) + static_cast<int64_t>(quarter * kQuarterTurnQuanta)));
			}
		}
		if (!nearest.has_value()) {
			return turnQuanta;
		}
		return wrapQuanta(static_cast<int64_t>(turnQuanta) + (direction > 0 ? *nearest : -*nearest));
	}

	double turnDegreesToward(::Foundation::Vec2 pivot, ::Foundation::Vec2 pointer) {
		const double across = static_cast<double>(pointer.x) - static_cast<double>(pivot.x);
		const double down = static_cast<double>(pointer.y) - static_cast<double>(pivot.y);
		return std::atan2(-across, down) * 180.0 / std::numbers::pi;
	}

	std::vector<FlushFace> wallFaces(const ConstructionWorld& world, ::Foundation::Vec2 point, float rangeMeters) {
		std::vector<FlushFace> faces;
		for (const WallSegment& wall : world.segments()) {
			const Vertex* first = world.getVertex(wall.v0);
			const Vertex* second = world.getVertex(wall.v1);
			if (first == nullptr || second == nullptr) {
				continue;
			}
			const ::Foundation::Vec2 start = geometry::dequantize(first->pos);
			const ::Foundation::Vec2 end = geometry::dequantize(second->pos);
			const float				 length = glm::distance(start, end);
			if (length <= 0.0F || glm::distance(point, closestOnSegment(point, start, end)) > rangeMeters) {
				continue;
			}
			const ::Foundation::Vec2 along = (end - start) / length;
			const ::Foundation::Vec2 left{-along.y, along.x};
			const float				 halfThickness = static_cast<float>(halfThicknessMm(wall)) / 1000.0F;
			faces.push_back({.start = start + left * halfThickness, .end = end + left * halfThickness, .outwardNormal = left});
			faces.push_back({.start = start - left * halfThickness, .end = end - left * halfThickness, .outwardNormal = -left});
		}
		return faces;
	}

	void appendRingFaces(const std::vector<::Foundation::Vec2>& ring, std::vector<FlushFace>& faces) {
		double doubledArea = 0.0;
		for (std::size_t i = 0; i < ring.size(); ++i) {
			const ::Foundation::Vec2& from = ring[i];
			const ::Foundation::Vec2& to = ring[(i + 1) % ring.size()];
			doubledArea += (static_cast<double>(from.x) * to.y) - (static_cast<double>(to.x) * from.y);
		}
		// A ring of positive area runs counter-clockwise in the maths' frame (clockwise on the screen, +y down), so
		// its outside is on the right of each edge: (dy, -dx).
		const float outside = doubledArea > 0.0 ? 1.0F : -1.0F;
		for (std::size_t i = 0; i < ring.size(); ++i) {
			const ::Foundation::Vec2 start = ring[i];
			const ::Foundation::Vec2 end = ring[(i + 1) % ring.size()];
			const float				 length = glm::distance(start, end);
			if (length <= 0.0F) {
				continue;
			}
			const ::Foundation::Vec2 along = (end - start) / length;
			faces.push_back({.start = start, .end = end, .outwardNormal = ::Foundation::Vec2{along.y, -along.x} * outside});
		}
	}

	::Foundation::Vec2 flushSlide(
		const std::vector<std::vector<::Foundation::Vec2>>& reservedRings,
		const std::vector<FlushFace>&						faces,
		const assets::SnappingConfig&						snapping
	) {
		const float			   parallelSine = std::sin(snapping.guideDirectionToleranceDegrees * std::numbers::pi_v<float> / 180.0F);
		const float			   radius = snapping.edgeSnapRadiusMeters;
		std::vector<FlushFace> edges;
		for (const std::vector<::Foundation::Vec2>& ring : reservedRings) {
			appendRingFaces(ring, edges);
		}
		::Foundation::Vec2 best{0.0F, 0.0F};
		float			   bestDistance = std::numeric_limits<float>::infinity();
		for (const FlushFace& face : faces) {
			const float faceLength = glm::distance(face.start, face.end);
			if (faceLength <= 0.0F) {
				continue;
			}
			const ::Foundation::Vec2 faceAlong = (face.end - face.start) / faceLength;
			for (const FlushFace& edge : edges) {
				const ::Foundation::Vec2 edgeAlong = glm::normalize(edge.end - edge.start);
				const bool				 parallel = std::abs((edgeAlong.x * faceAlong.y) - (edgeAlong.y * faceAlong.x)) <= parallelSine;
				if (!parallel || glm::dot(edge.outwardNormal, face.outwardNormal) >= 0.0F) {
					continue;
				}
				const float startAlong = glm::dot(edge.start - face.start, faceAlong);
				const float endAlong = glm::dot(edge.end - face.start, faceAlong);
				if (std::max(startAlong, endAlong) <= 0.0F || std::min(startAlong, endAlong) >= faceLength) {
					continue;
				}
				const float startOff = glm::dot(edge.start - face.start, face.outwardNormal);
				const float endOff = glm::dot(edge.end - face.start, face.outwardNormal);
				const float nearer = std::abs(startOff) <= std::abs(endOff) ? startOff : endOff;
				if (std::abs(nearer) <= radius && std::abs(nearer) < bestDistance) {
					bestDistance = std::abs(nearer);
					best = -nearer * face.outwardNormal;
				}
			}
		}
		return best;
	}

} // namespace construction

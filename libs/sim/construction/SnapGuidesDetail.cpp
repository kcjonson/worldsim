#include "SnapGuidesDetail.h"

#include <assets/ConstructionRegistry.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace construction::snap_guides {

	namespace {

		constexpr double kRadiansPerDegree = std::numbers::pi / 180.0;
		constexpr double kQuarterTurn = std::numbers::pi / 2.0;
		constexpr double kHalfTurn = std::numbers::pi;
		// Two directions closer than this to parallel have no crossing.
		constexpr double kParallelCross = 1e-12;
		// Slack on an angle compared against a boundary (half a turn, the crossing minimum), so an
		// input on the boundary stays on the side the prototype puts it despite rounding.
		constexpr double kAngleSlackRadians = 1e-12;
		// A corner whose edges meet at a half-angle sine under this is a spike: its inset has no reach.
		constexpr double kSpikeHalfAngleSine = 1e-9;
		// A cursor this close to an anchor has no direction from it, so no angle to catch.
		constexpr double kNoDirectionMeters = 1e-6;

		double cross(Vec2d first, Vec2d second) {
			return first.x * second.y - first.y * second.x;
		}

		Vec2d unit(Vec2d vector) {
			const double size = geometry::length(vector);
			return size > 0.0 ? vector * (1.0 / size) : Vec2d{};
		}

		double angleOf(Vec2d vector) {
			return std::atan2(vector.y, vector.x);
		}

		// Distance from `point` to the axis-aligned bounds of `ring`: no ring vertex or edge
		// is nearer, so a ring farther than the range is out of range whole.
		double distanceToBounds(Vec2d point, const geometry::Ring& ring) {
			std::int64_t minimumX = std::numeric_limits<std::int64_t>::max();
			std::int64_t minimumY = std::numeric_limits<std::int64_t>::max();
			std::int64_t maximumX = std::numeric_limits<std::int64_t>::min();
			std::int64_t maximumY = std::numeric_limits<std::int64_t>::min();
			for (const geometry::Vec2i64& vertex : ring) {
				minimumX = std::min(minimumX, vertex.x);
				minimumY = std::min(minimumY, vertex.y);
				maximumX = std::max(maximumX, vertex.x);
				maximumY = std::max(maximumY, vertex.y);
			}
			const Vec2d	 low = meters({minimumX, minimumY});
			const Vec2d	 high = meters({maximumX, maximumY});
			const double outsideX = std::max({low.x - point.x, 0.0, point.x - high.x});
			const double outsideY = std::max({low.y - point.y, 0.0, point.y - high.y});
			return std::sqrt(outsideX * outsideX + outsideY * outsideY);
		}

		// How far a corner's outer-face-flush inset can sit from the raw corner: the two
		// inset lines meet on the bisector at inset / sin(half the angle between the edges).
		// Infinite for a spike, so such a corner is always computed.
		double insetReach(Vec2d previous, Vec2d corner, Vec2d next, double insetMeters) {
			const Vec2d	 towardPrevious = unit(previous - corner);
			const Vec2d	 towardNext = unit(next - corner);
			const double halfAngleSine = std::sqrt(std::max(0.0, (1.0 - dot(towardPrevious, towardNext)) / 2.0));
			return halfAngleSine > kSpikeHalfAngleSine ? insetMeters / halfAngleSine : std::numeric_limits<double>::infinity();
		}

		double directionToleranceRadians(const GuideContext& context) {
			return static_cast<double>(context.snapping->guideDirectionToleranceDegrees) * kRadiansPerDegree;
		}

		bool contains(const std::vector<StructureKey>& keys, std::size_t first, std::size_t count, StructureKey key) {
			const auto begin = keys.begin() + static_cast<std::ptrdiff_t>(first);
			return std::find(begin, begin + static_cast<std::ptrdiff_t>(count), key) != begin + static_cast<std::ptrdiff_t>(count);
		}

		std::optional<Vec2d> intersect(const GuideLine& first, const GuideLine& second) {
			const double denominator = cross(first.direction, second.direction);
			if (std::abs(denominator) < kParallelCross) {
				return std::nullopt;
			}
			const double parameter = cross(second.node.point - first.node.point, second.direction) / denominator;
			return first.node.point + first.direction * parameter;
		}

		int nodeRank(const GuideLine& line) {
			return line.node.kind == GuideNodeKind::ShapePoint ? 0 : 1;
		}

		// Source, then shape point before committed corner, then generation order.
		bool ranksBefore(const GuideLine& first, const GuideLine& second) {
			if (first.source != second.source) {
				return first.source < second.source;
			}
			if (nodeRank(first) != nodeRank(second)) {
				return nodeRank(first) < nodeRank(second);
			}
			return first.id < second.id;
		}

		bool passesThrough(Vec2d node, const GuideLine& line) {
			return lineDistance(node, line) <= kCoincidenceMeters;
		}

		bool sharesGeneratingNode(const GuideStudy& study, std::size_t firstLine, std::size_t secondLine) {
			const GuideLine& first = study.lines[firstLine];
			const GuideLine& second = study.lines[secondLine];
			const auto		 onBoth = [&](Vec2d node) {
				  return passesThrough(node, first) && passesThrough(node, second);
			};
			for (const std::size_t owner : {firstLine, secondLine}) {
				if (onBoth(study.lines[owner].node.point)) {
					return true;
				}
				for (const std::size_t merged : study.captured) {
					const GuideLine& line = study.lines[merged];
					if (line.status == GuideLineStatus::Merged && line.mergedInto == owner && onBoth(line.node.point)) {
						return true;
					}
				}
			}
			return false;
		}

		// Negative when `first` ranks ahead of `second`, positive when behind, zero on a full tie.
		int compareCandidates(
			const GuideStudy&		  study,
			assets::GuidePriorityRule rule,
			const GuideCandidate&	  first,
			const GuideCandidate&	  second
		) {
			const auto order = [](auto left, auto right) {
				return left < right ? -1 : (right < left ? 1 : 0);
			};
			const auto kind = [](const GuideCandidate& candidate) {
				return candidate.isCrossing ? 0 : 1;
			};
			const GuideLine& firstBest = study.lines[first.bestLine];
			const GuideLine& secondBest = study.lines[second.bestLine];
			if (rule == assets::GuidePriorityRule::SourceFirst) {
				if (const int bySource = order(firstBest.source, secondBest.source); bySource != 0) {
					return bySource;
				}
			}
			if (const int byKind = order(kind(first), kind(second)); byKind != 0) {
				return byKind;
			}
			if (rule == assets::GuidePriorityRule::SourceFirst) {
				if (const int byNode = order(nodeRank(firstBest), nodeRank(secondBest)); byNode != 0) {
					return byNode;
				}
			}
			if (std::abs(first.distanceMeters - second.distanceMeters) > kDistanceTieMeters) {
				return first.distanceMeters < second.distanceMeters ? -1 : 1;
			}
			// A single line has no second generation; it sorts ahead of every line id.
			const auto secondGeneration = [](const GuideCandidate& candidate) {
				return candidate.isCrossing ? static_cast<std::int64_t>(candidate.secondGeneration) : std::int64_t{-1};
			};
			if (const int byGeneration = order(first.firstGeneration, second.firstGeneration); byGeneration != 0) {
				return byGeneration;
			}
			return order(secondGeneration(first), secondGeneration(second));
		}

	} // namespace

	double dot(Vec2d first, Vec2d second) {
		return first.x * second.x + first.y * second.y;
	}

	double distanceBetween(Vec2d first, Vec2d second) {
		return geometry::length(first - second);
	}

	Vec2d meters(const geometry::Vec2i64& millimeters) {
		const auto perMeter = static_cast<double>(geometry::kMillimetersPerMeter);
		return {static_cast<double>(millimeters.x) / perMeter, static_cast<double>(millimeters.y) / perMeter};
	}

	Vec2d toVec2d(::Foundation::Vec2 point) {
		return {static_cast<double>(point.x), static_cast<double>(point.y)};
	}

	double flushInsetMeters(std::int64_t halfThicknessMm) {
		return static_cast<double>(halfThicknessMm + kFlushInsetSafetyMm) / static_cast<double>(geometry::kMillimetersPerMeter);
	}

	double normalizeLineAngle(double angle) {
		double result = std::fmod(angle, kHalfTurn);
		if (result < 0.0) {
			result += kHalfTurn;
		}
		if (result >= kHalfTurn - kAngleSlackRadians) {
			result = 0.0;
		}
		return result;
	}

	double angularDifference(double first, double second, double period) {
		double difference = std::fmod(first - second, period);
		if (difference < 0.0) {
			difference += period;
		}
		return std::min(difference, period - difference);
	}

	double lineDistance(Vec2d point, const GuideLine& line) {
		return std::abs(cross(point - line.node.point, line.direction));
	}

	Vec2d footOnLine(Vec2d point, const GuideLine& line) {
		return line.node.point + line.direction * dot(point - line.node.point, line.direction);
	}

	// A foot clamped to an end is that end exactly, so two edges whose nearest point is
	// their shared corner tie exactly and fall to walk order.
	double distanceToSegment(Vec2d point, Vec2d start, Vec2d end) {
		const Vec2d	 edge = end - start;
		const double lengthSquared = dot(edge, edge);
		const double parameter = lengthSquared > 0.0 ? dot(point - start, edge) / lengthSquared : 0.0;
		if (parameter <= 0.0) {
			return distanceBetween(point, start);
		}
		if (parameter >= 1.0) {
			return distanceBetween(point, end);
		}
		return distanceBetween(point, start + edge * parameter);
	}

	double roundHalfUp(double value) {
		return std::floor(value + 0.5);
	}

	void collectShapeNodes(const GuideContext& context, GuideStudy& study) {
		study.shapePoints.clear();
		study.shapeNodes.clear();
		for (const ::Foundation::Vec2& point : *context.points) {
			study.shapePoints.push_back(toVec2d(point));
		}
		const std::size_t count = study.shapePoints.size();
		const auto		  maxPoints = static_cast<std::size_t>(std::max(2, context.maxPoints));
		if (context.tool != SnapTool::Wall || count <= maxPoints) {
			for (std::size_t index = 0; index < count; ++index) {
				study.shapeNodes.push_back(index);
			}
			return;
		}
		study.shapeNodes.push_back(0);
		for (std::size_t index = count - (maxPoints - 1); index < count; ++index) {
			study.shapeNodes.push_back(index);
		}
	}

	void collectCorners(const GuideContext& context, GuideStudy& study) {
		study.corners.clear();
		study.cornerStructures.clear();
		study.cornerEdgeDirections.clear();
		study.cornersUsed = 0;
		if (!context.aids.alignToExistingBuildings) {
			return;
		}
		const double range = static_cast<double>(context.snapping->guideRangeMeters);
		const bool	 wallTool = context.tool == SnapTool::Wall;
		const bool	 inset = wallTool && context.wallHalfThicknessMm > 0;
		const double insetMeters = inset ? flushInsetMeters(context.wallHalfThicknessMm) : 0.0;
		std::size_t	 order = 0;
		const auto	 push = [&](Vec2d point, double distance) {
			  study.corners.push_back(
				  GuideCorner{
					  .point = point,
					  .distanceMeters = distance,
					  .order = order,
					  .firstStructure = study.cornerStructures.size(),
					  .firstEdgeDirection = study.cornerEdgeDirections.size(),
				  }
			  );
		};

		for (const Foundation& foundation : context.world->foundations()) {
			const geometry::Ring& ring = foundation.ring;
			const std::size_t	  count = ring.size();
			if (distanceToBounds(context.cursor, ring) > range) {
				order += count;
				continue;
			}
			for (std::size_t index = 0; index < count; ++index, ++order) {
				const Vec2d raw = meters(ring[index]);
				const Vec2d previous = meters(ring[(index + count - 1) % count]);
				const Vec2d next = meters(ring[(index + 1) % count]);
				Vec2d		point = raw;
				if (inset) {
					// Only a corner that could be in range pays for its inset (two point-in-polygon tests).
					if (distanceBetween(context.cursor, raw) - insetReach(previous, raw, next, insetMeters) > range) {
						continue;
					}
					point = toVec2d(outerFaceFlushCorner(ring, index, context.wallHalfThicknessMm));
				}
				const double distance = distanceBetween(context.cursor, point);
				if (distance > range) {
					continue;
				}
				push(point, distance);
				study.cornerStructures.push_back({StructureKind::Foundation, foundation.id});
				study.cornerEdgeDirections.push_back(unit(previous - raw));
				study.cornerEdgeDirections.push_back(unit(next - raw));
				study.corners.back().structureCount = 1;
				study.corners.back().edgeDirectionCount = 2;
			}
		}

		if (wallTool) {
			for (const Vertex& vertex : context.world->vertices()) {
				const Vec2d	 point = meters(vertex.pos);
				const double distance = distanceBetween(context.cursor, point);
				if (distance <= range) {
					push(point, distance);
					study.vertexSegments.assign(vertex.segments.begin(), vertex.segments.end());
					std::sort(study.vertexSegments.begin(), study.vertexSegments.end());
					for (const SegmentId segmentId : study.vertexSegments) {
						const WallSegment* segment = context.world->getSegment(segmentId);
						if (segment == nullptr) {
							continue;
						}
						const Vertex* other = context.world->getVertex(segment->v0 == vertex.id ? segment->v1 : segment->v0);
						if (other == nullptr) {
							continue;
						}
						study.cornerStructures.push_back({StructureKind::WallSegment, segmentId});
						study.cornerEdgeDirections.push_back(unit(meters(other->pos) - point));
					}
					GuideCorner& corner = study.corners.back();
					corner.structureCount = study.cornerStructures.size() - corner.firstStructure;
					corner.edgeDirectionCount = study.cornerEdgeDirections.size() - corner.firstEdgeDirection;
				}
				++order;
			}
		}

		std::sort(study.corners.begin(), study.corners.end(), [](const GuideCorner& first, const GuideCorner& second) {
			if (first.distanceMeters != second.distanceMeters) {
				return first.distanceMeters < second.distanceMeters;
			}
			return first.order < second.order;
		});
		study.cornersUsed = std::min(study.corners.size(), static_cast<std::size_t>(std::max(0, context.snapping->guideCornerLimit)));
	}

	void collectNearbyDirections(const GuideContext& context, GuideStudy& study) {
		study.edges.clear();
		study.walk.clear();
		study.structuresInRange.clear();
		study.directions.clear();
		study.matchingStructures.clear();
		if (!context.aids.alignToExistingBuildings) {
			return;
		}
		const double range = static_cast<double>(context.snapping->guideRangeMeters);
		const double tolerance = directionToleranceRadians(context);
		std::size_t	 order = 0;
		const auto	 addEdge = [&](StructureKey structure, Vec2d start, Vec2d end) {
			  if (distanceBetween(start, end) > 0.0) {
				  study.edges.push_back(
					  NearbyEdge{
						  .structure = structure,
						  .angle = normalizeLineAngle(angleOf(end - start)),
						  .distanceMeters = distanceToSegment(context.cursor, start, end),
						  .order = order,
					  }
				  );
			  }
			  ++order;
		};

		for (const Foundation& foundation : context.world->foundations()) {
			const geometry::Ring& ring = foundation.ring;
			const std::size_t	  count = ring.size();
			if (distanceToBounds(context.cursor, ring) > range) {
				order += count;
				continue;
			}
			for (std::size_t index = 0; index < count; ++index) {
				addEdge({StructureKind::Foundation, foundation.id}, meters(ring[index]), meters(ring[(index + 1) % count]));
			}
		}
		if (context.tool == SnapTool::Wall) {
			for (const WallSegment& segment : context.world->segments()) {
				const Vertex* start = context.world->getVertex(segment.v0);
				const Vertex* end = context.world->getVertex(segment.v1);
				if (start != nullptr && end != nullptr) {
					addEdge({StructureKind::WallSegment, segment.id}, meters(start->pos), meters(end->pos));
				}
			}
		}

		for (std::size_t index = 0; index < study.edges.size(); ++index) {
			if (study.edges[index].distanceMeters <= range) {
				study.walk.push_back({.edge = index});
			}
		}
		std::sort(study.walk.begin(), study.walk.end(), [&](const NearbyWalkEntry& first, const NearbyWalkEntry& second) {
			const NearbyEdge& firstEdge = study.edges[first.edge];
			const NearbyEdge& secondEdge = study.edges[second.edge];
			if (firstEdge.distanceMeters != secondEdge.distanceMeters) {
				return firstEdge.distanceMeters < secondEdge.distanceMeters;
			}
			return firstEdge.order < secondEdge.order;
		});

		const auto limit = static_cast<std::size_t>(std::max(0, context.snapping->nearbyGuideDirectionLimit));
		for (NearbyWalkEntry& entry : study.walk) {
			const NearbyEdge& edge = study.edges[entry.edge];
			if (!contains(study.structuresInRange, 0, study.structuresInRange.size(), edge.structure)) {
				study.structuresInRange.push_back(edge.structure);
			}
			if (angularDifference(edge.angle, 0.0, kQuarterTurn) <= tolerance) {
				entry.status = NearbyEdgeStatus::AxisAligned;
				continue;
			}
			const bool duplicate = std::any_of(study.directions.begin(), study.directions.end(), [&](const NearbyDirection& direction) {
				return angularDifference(edge.angle, direction.angle, kQuarterTurn) <= tolerance;
			});
			if (duplicate) {
				entry.status = NearbyEdgeStatus::Duplicate;
			} else if (study.directions.size() >= limit) {
				entry.status = NearbyEdgeStatus::OverLimit;
			} else {
				study.directions.push_back({.angle = edge.angle, .supplier = edge.structure});
				entry.status = NearbyEdgeStatus::Kept;
			}
		}

		// A kept direction also belongs to every structure in range with an edge along it,
		// so a building whose own edge was skipped as a duplicate still gets the lines.
		for (NearbyDirection& direction : study.directions) {
			direction.firstMatchingStructure = study.matchingStructures.size();
			for (const NearbyEdge& edge : study.edges) {
				if (angularDifference(edge.angle, direction.angle, kQuarterTurn) > tolerance ||
					!contains(study.structuresInRange, 0, study.structuresInRange.size(), edge.structure) ||
					contains(
						study.matchingStructures,
						direction.firstMatchingStructure,
						study.matchingStructures.size() - direction.firstMatchingStructure,
						edge.structure
					)) {
					continue;
				}
				study.matchingStructures.push_back(edge.structure);
			}
			direction.matchingStructureCount = study.matchingStructures.size() - direction.firstMatchingStructure;
		}
	}

	void generateLines(const GuideContext& context, GuideStudy& study) {
		study.lines.clear();
		const double tolerance = directionToleranceRadians(context);
		const auto	 addPair = [&](GuideSource source, GuideNode node, double angle) {
			  for (const auto& [relation, lineAngle] :
				   {std::pair{GuideRelation::Parallel, angle}, std::pair{GuideRelation::Perpendicular, angle + kQuarterTurn}}) {
				  const double normalized = normalizeLineAngle(lineAngle);
				  study.lines.push_back(
					  GuideLine{
						  .id = study.lines.size(),
						  .source = source,
						  .relation = relation,
						  .node = node,
						  .angle = normalized,
						  .direction = {std::cos(normalized), std::sin(normalized)},
					  }
				  );
			  }
		};
		const auto shapeNode = [&](std::size_t index) {
			return GuideNode{.point = study.shapePoints[index], .kind = GuideNodeKind::ShapePoint, .index = index};
		};
		const auto cornerNode = [&](std::size_t index) {
			return GuideNode{.point = study.corners[index].point, .kind = GuideNodeKind::CommittedCorner, .index = index};
		};
		const std::vector<Vec2d>& points = study.shapePoints;
		const std::size_t		  count = points.size();

		if (context.aids.guidesFromShape && count >= 2) {
			const double firstAngle = normalizeLineAngle(angleOf(points[1] - points[0]));
			for (const std::size_t index : study.shapeNodes) {
				addPair(GuideSource::DrawnShape, shapeNode(index), firstAngle);
			}
			if (count >= 3) {
				const double previousAngle = normalizeLineAngle(angleOf(points[count - 1] - points[count - 2]));
				if (angularDifference(previousAngle, firstAngle, kQuarterTurn) > tolerance) {
					addPair(GuideSource::DrawnShape, shapeNode(count - 1), previousAngle);
				}
			}
		}
		if (context.aids.alignToExistingBuildings) {
			for (const NearbyDirection& direction : study.directions) {
				if (count > 0) {
					addPair(GuideSource::NearbyStructure, shapeNode(count - 1), direction.angle);
				}
				for (std::size_t index = 0; index < study.cornersUsed; ++index) {
					const GuideCorner& corner = study.corners[index];
					for (std::size_t structure = 0; structure < corner.structureCount; ++structure) {
						if (contains(
								study.matchingStructures,
								direction.firstMatchingStructure,
								direction.matchingStructureCount,
								study.cornerStructures[corner.firstStructure + structure]
							)) {
							addPair(GuideSource::NearbyStructure, cornerNode(index), direction.angle);
							break;
						}
					}
				}
			}
		}
		if (context.aids.guidesFromWorldAxes) {
			for (const std::size_t index : study.shapeNodes) {
				addPair(GuideSource::WorldAxes, shapeNode(index), 0.0);
			}
			for (std::size_t index = 0; index < study.cornersUsed; ++index) {
				addPair(GuideSource::WorldAxes, cornerNode(index), 0.0);
			}
		}
	}

	void captureLines(const GuideContext& context, GuideStudy& study) {
		const double			  tolerance = static_cast<double>(context.snapping->guideToleranceMeters);
		const double			  window = static_cast<double>(context.snapping->effectiveGuideAngleWindowDegrees()) * kRadiansPerDegree;
		const std::vector<Vec2d>& points = study.shapePoints;
		// The previous point, then the first: the shape's lines through them also catch by angle.
		std::array<Vec2d, 2> anchors{};
		std::size_t			 anchorCount = 0;
		if (!points.empty()) {
			anchors[anchorCount++] = points.back();
			if (points.size() > 1) {
				anchors[anchorCount++] = points.front();
			}
		}
		for (GuideLine& line : study.lines) {
			line.distanceMeters = lineDistance(context.cursor, line);
			line.foot = footOnLine(context.cursor, line);
			line.capturedByDistance = line.distanceMeters <= tolerance;
			line.capturedByAngle = false;
			line.status = GuideLineStatus::Missed;
			if (line.source != GuideSource::DrawnShape) {
				continue;
			}
			for (std::size_t anchor = 0; anchor < anchorCount && !line.capturedByAngle; ++anchor) {
				if (!passesThrough(anchors[anchor], line)) {
					continue;
				}
				const Vec2d offset = context.cursor - anchors[anchor];
				if (geometry::length(offset) < kNoDirectionMeters) {
					continue;
				}
				line.capturedByAngle = angularDifference(angleOf(offset), line.angle, kHalfTurn) <= window;
			}
		}
	}

	void mergeLines(const GuideContext& context, GuideStudy& study) {
		study.captured.clear();
		study.kept.clear();
		for (std::size_t index = 0; index < study.lines.size(); ++index) {
			if (study.lines[index].captured()) {
				study.captured.push_back(index);
			}
		}
		std::sort(study.captured.begin(), study.captured.end(), [&](std::size_t first, std::size_t second) {
			return ranksBefore(study.lines[first], study.lines[second]);
		});
		const double tolerance = directionToleranceRadians(context);
		const double mergeDistance = static_cast<double>(context.snapping->effectiveGuideMergeDistanceMeters());
		for (const std::size_t index : study.captured) {
			GuideLine& line = study.lines[index];
			const auto into = std::find_if(study.kept.begin(), study.kept.end(), [&](std::size_t keptIndex) {
				const GuideLine& keptLine = study.lines[keptIndex];
				return angularDifference(line.angle, keptLine.angle, kHalfTurn) <= tolerance &&
					   lineDistance(keptLine.foot, line) < mergeDistance;
			});
			if (into != study.kept.end()) {
				line.status = GuideLineStatus::Merged;
				line.mergedInto = *into;
			} else {
				line.status = GuideLineStatus::Kept;
				study.kept.push_back(index);
			}
		}
	}

	void formCrossings(const GuideContext& context, GuideStudy& study) {
		study.crossings.clear();
		const double minimumAngle = static_cast<double>(context.snapping->effectiveGuideCrossingMinAngleDegrees()) * kRadiansPerDegree;
		const double radius = static_cast<double>(context.snapping->guideCrossingRadiusMeters);
		for (std::size_t firstIndex = 0; firstIndex < study.kept.size(); ++firstIndex) {
			for (std::size_t secondIndex = firstIndex + 1; secondIndex < study.kept.size(); ++secondIndex) {
				const std::size_t	 firstLine = std::min(study.kept[firstIndex], study.kept[secondIndex]);
				const std::size_t	 secondLine = std::max(study.kept[firstIndex], study.kept[secondIndex]);
				const GuideLine&	 first = study.lines[firstLine];
				const GuideLine&	 second = study.lines[secondLine];
				const double		 angle = angularDifference(first.angle, second.angle, kHalfTurn);
				const auto			 point = intersect(first, second);
				GuideCrossingRecord& record = study.crossings.emplace_back(
					GuideCrossingRecord{
						.firstLine = firstLine,
						.secondLine = secondLine,
						.point = point.value_or(Vec2d{}),
						.distanceMeters = point ? distanceBetween(*point, context.cursor) : std::numeric_limits<double>::infinity(),
					}
				);
				if (!context.aids.guideCrossings) {
					record.status = CrossingStatus::CrossingsOff;
				} else if (angle < minimumAngle - kAngleSlackRadians || !point) {
					record.status = CrossingStatus::TooShallow;
				} else if (record.distanceMeters > radius) {
					record.status = CrossingStatus::TooFar;
				} else if (sharesGeneratingNode(study, firstLine, secondLine)) {
					record.status = CrossingStatus::SharedGeneratingNode;
				} else {
					record.status = CrossingStatus::Kept;
				}
			}
		}
	}

	void rankCandidates(const GuideContext& context, GuideStudy& study) {
		study.candidates.clear();
		study.winner.reset();
		for (const std::size_t index : study.kept) {
			const GuideLine& line = study.lines[index];
			study.candidates.push_back(
				GuideCandidate{
					.bestLine = index,
					.otherLine = index,
					.point = line.foot,
					.distanceMeters = line.distanceMeters,
					.firstGeneration = line.id,
				}
			);
		}
		for (const GuideCrossingRecord& crossing : study.crossings) {
			if (crossing.status != CrossingStatus::Kept) {
				continue;
			}
			const bool firstLeads = ranksBefore(study.lines[crossing.firstLine], study.lines[crossing.secondLine]);
			study.candidates.push_back(
				GuideCandidate{
					.isCrossing = true,
					.bestLine = firstLeads ? crossing.firstLine : crossing.secondLine,
					.otherLine = firstLeads ? crossing.secondLine : crossing.firstLine,
					.point = crossing.point,
					.distanceMeters = crossing.distanceMeters,
					.firstGeneration = study.lines[crossing.firstLine].id,
					.secondGeneration = study.lines[crossing.secondLine].id,
				}
			);
		}
		const assets::GuidePriorityRule rule = context.snapping->priorityRule;
		for (std::size_t index = 0; index < study.candidates.size(); ++index) {
			if (!study.winner || compareCandidates(study, rule, study.candidates[index], study.candidates[*study.winner]) < 0) {
				study.winner = index;
			}
		}
	}

	void studyGuides(const GuideContext& context, GuideStudy& study) {
		collectShapeNodes(context, study);
		collectCorners(context, study);
		collectNearbyDirections(context, study);
		generateLines(context, study);
		captureLines(context, study);
		mergeLines(context, study);
		formCrossings(context, study);
		rankCandidates(context, study);
	}

	std::optional<SquaredPoint>
	squareOntoEdge(const GuideContext& context, const GuideStudy& study, Vec2d start, Vec2d end, bool strictInterior) {
		const Vec2d	 edge = end - start;
		const double edgeAngle = normalizeLineAngle(angleOf(edge));
		const double minimumAngle = static_cast<double>(context.snapping->effectiveGuideCrossingMinAngleDegrees()) * kRadiansPerDegree;
		const double radius = static_cast<double>(context.snapping->edgeSnapRadiusMeters);
		std::optional<SquaredPoint> best;
		double						bestDistance = 0.0;
		for (std::size_t index = 0; index < study.lines.size(); ++index) {
			const GuideLine& line = study.lines[index];
			if (!line.captured() || line.source != GuideSource::DrawnShape) {
				continue;
			}
			if (angularDifference(line.angle, edgeAngle, kHalfTurn) < minimumAngle - kAngleSlackRadians) {
				continue;
			}
			const double denominator = cross(edge, line.direction);
			if (std::abs(denominator) < kParallelCross) {
				continue;
			}
			const double parameter = cross(line.node.point - start, line.direction) / denominator;
			const bool	 offEdge = strictInterior ? (parameter <= 0.0 || parameter >= 1.0) : (parameter < 0.0 || parameter > 1.0);
			if (offEdge) {
				continue;
			}
			const Vec2d	 point = start + edge * parameter;
			const double distance = distanceBetween(point, context.cursor);
			if (distance > radius) {
				continue;
			}
			if (!best || distance < bestDistance - kDistanceTieMeters) {
				best = SquaredPoint{.point = point, .line = index};
				bestDistance = distance;
			}
		}
		return best;
	}

	SnapGuide makeGuide(const GuideContext& context, const GuideStudy& study, const GuideLine& line, Vec2d to) {
		const std::vector<Vec2d>& points = study.shapePoints;
		const double			  tolerance = directionToleranceRadians(context);
		const double			  perpendicular = line.angle + kQuarterTurn;
		SnapGuide				  guide{
							.from = {static_cast<float>(line.node.point.x), static_cast<float>(line.node.point.y)},
							.to = {static_cast<float>(to.x), static_cast<float>(to.y)},
							.source = line.source,
							.relation = line.relation,
							.distanceFromNodeMeters = static_cast<float>(distanceBetween(line.node.point, to)),
							.throughPreviousPoint = !points.empty() && distanceBetween(line.node.point, points.back()) <= kCoincidenceMeters,
		};
		// The marker goes on the first edge at the node that the guide meets square: for a
		// shape point the edge into it, then out of it; for a corner its edges in order.
		const auto tryEdge = [&](Vec2d direction) {
			if (guide.rightAngle || geometry::length(direction) <= 0.0 ||
				angularDifference(angleOf(direction), perpendicular, kHalfTurn) > tolerance) {
				return;
			}
			guide.rightAngle = true;
			guide.rightAngleEdgeDirection = {static_cast<float>(direction.x), static_cast<float>(direction.y)};
		};
		if (line.node.kind == GuideNodeKind::ShapePoint) {
			const std::size_t index = line.node.index;
			if (index > 0) {
				tryEdge(unit(points[index - 1] - points[index]));
			}
			if (index + 1 < points.size()) {
				tryEdge(unit(points[index + 1] - points[index]));
			}
		} else {
			const GuideCorner& corner = study.corners[line.node.index];
			for (std::size_t edge = 0; edge < corner.edgeDirectionCount; ++edge) {
				tryEdge(study.cornerEdgeDirections[corner.firstEdgeDirection + edge]);
			}
		}
		return guide;
	}

} // namespace construction::snap_guides

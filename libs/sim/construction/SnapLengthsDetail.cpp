#include "SnapLengthsDetail.h"

#include <assets/ConstructionRegistry.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace construction::snap_lengths {

	using snap_guides::distanceBetween;
	using snap_guides::distanceToBounds;
	using snap_guides::distanceToBox;
	using snap_guides::distanceToSegment;
	using snap_guides::dot;
	using snap_guides::GuideContext;
	using snap_guides::meters;
	using snap_guides::toVec2d;

	namespace {

		// The wall tool's chains: segments() grouped by placement order, each chain at the
		// position of its first segment, its segments in id order.
		void collectWallChains(const ConstructionWorld& world, LengthStudy& study) {
			study.chainSegments.clear();
			study.chains.clear();
			const std::vector<WallSegment>& segments = world.segments();
			for (std::size_t index = 0; index < segments.size(); ++index) {
				if (world.getVertex(segments[index].v0) != nullptr && world.getVertex(segments[index].v1) != nullptr) {
					study.chainSegments.emplace_back(segments[index].placementOrder, index);
				}
			}
			std::sort(study.chainSegments.begin(), study.chainSegments.end());
			for (std::size_t index = 0; index < study.chainSegments.size(); ++index) {
				if (index == 0 || study.chainSegments[index].first != study.chainSegments[index - 1].first) {
					study.chains.push_back({.first = index});
				}
				++study.chains.back().count;
			}
			std::sort(study.chains.begin(), study.chains.end(), [&](const ChainRun& first, const ChainRun& second) {
				return study.chainSegments[first.first].second < study.chainSegments[second.first].second;
			});
		}

	} // namespace

	void studyCommonLengths(const GuideContext& context, LengthStudy& study) {
		study.entries.clear();
		study.walk.clear();
		study.groups.clear();
		study.ranked.clear();
		study.keptCount = 0;

		const assets::SnappingConfig& snapping = *context.snapping;
		const ConstructionWorld&	  world = *context.world;
		const double				  minimum = static_cast<double>(snapping.commonLengthMinimumMeters);
		const double				  range = static_cast<double>(snapping.commonLengthRangeMeters);
		const bool					  everywhere = snapping.commonLengthAllBuildings;
		// Where the range applies, an owner whose bounds lie past it is skipped unread.
		const bool	 culling = !everywhere;
		const double cullBeyond = range + kRangeCullSlackMeters;
		// The range applies to every owner but the shape.
		const auto add = [&](Vec2d start, Vec2d end, bool ranged, std::uint64_t recency, double ownerMeters) {
			LengthEntry& entry = study.entries.emplace_back(
				LengthEntry{
					.start = start,
					.end = end,
					.lengthMeters = distanceBetween(start, end),
					.recency = recency,
					.ownerMeters = ownerMeters,
					.order = study.entries.size(),
				}
			);
			if (entry.lengthMeters < minimum) {
				entry.status = LengthEntryStatus::UnderMinimum;
			} else if (ranged && !everywhere && ownerMeters > range) {
				entry.status = LengthEntryStatus::OutOfRange;
			}
		};

		if (snapping.commonLengthSourceShape) {
			const std::vector<::Foundation::Vec2>& points = *context.points;
			for (std::size_t index = 0; index + 1 < points.size(); ++index) {
				add(toVec2d(points[index]), toVec2d(points[index + 1]), false, kShapeRecency, 0.0);
			}
		}

		if (snapping.commonLengthSourceBuildings) {
			const bool wallTool = context.tool == snap_guides::SnapTool::Wall;
			const bool inset = wallTool && context.wallHalfThicknessMm > 0 && snapping.commonLengthWallsReadInsetFoundation;
			for (const Foundation& foundation : world.foundations()) {
				const geometry::Ring& ring = foundation.ring;
				const std::size_t	  count = ring.size();
				if (count == 0 || (culling && distanceToBounds(context.cursor, ring) > cullBeyond)) {
					continue;
				}
				double				  ownerMeters = std::numeric_limits<double>::infinity();
				for (std::size_t index = 0; index < count; ++index) {
					ownerMeters = std::min(ownerMeters, distanceToSegment(context.cursor, meters(ring[index]), meters(ring[(index + 1) % count])));
				}
				// Only an owner in reach pays for its insets; the others are dropped whatever their length.
				const bool reachable = everywhere || ownerMeters <= range;
				const auto corner = [&](std::size_t index) {
					return inset && reachable ? snap_guides::outerFaceFlushCornerDouble(ring, index, context.wallHalfThicknessMm)
											  : meters(ring[index]);
				};
				const Vec2d first = corner(0);
				Vec2d		start = first;
				for (std::size_t index = 0; index < count; ++index) {
					const Vec2d end = index + 1 < count ? corner(index + 1) : first;
					add(start, end, true, foundation.placementOrder, ownerMeters);
					start = end;
				}
			}

			if (wallTool) {
				collectWallChains(world, study);
				const std::vector<WallSegment>& segments = world.segments();
				const auto						endpoints = [&](std::size_t segmentIndex) {
					   return std::pair{
						   meters(world.getVertex(segments[segmentIndex].v0)->pos), meters(world.getVertex(segments[segmentIndex].v1)->pos)
					   };
				};
				for (const ChainRun& chain : study.chains) {
					double ownerMeters = std::numeric_limits<double>::infinity();
					bool   anyNear = false;
					for (std::size_t member = chain.first; member < chain.first + chain.count; ++member) {
						const auto [start, end] = endpoints(study.chainSegments[member].second);
						const Vec2d low{std::min(start.x, end.x), std::min(start.y, end.y)};
						const Vec2d high{std::max(start.x, end.x), std::max(start.y, end.y)};
						if (culling && distanceToBox(context.cursor, low, high) > cullBeyond) {
							continue;
						}
						anyNear = true;
						ownerMeters = std::min(ownerMeters, distanceToSegment(context.cursor, start, end));
					}
					// Every segment past the range: the chain lends nothing.
					if (!anyNear) {
						continue;
					}
					const std::uint64_t placementOrder = study.chainSegments[chain.first].first;
					for (std::size_t member = chain.first; member < chain.first + chain.count; ++member) {
						const auto [start, end] = endpoints(study.chainSegments[member].second);
						add(start, end, true, placementOrder, ownerMeters);
					}
				}
			}
		}

		for (std::size_t index = 0; index < study.entries.size(); ++index) {
			if (study.entries[index].status == LengthEntryStatus::Member) {
				study.walk.push_back(index);
			}
		}
		// Newest owner first (the shape, then the highest placement order), then nearest, then
		// generation order. Recency leads because the first entry of a group sets its length: a
		// building just placed is the one being matched, so its edge wins a merge over an older,
		// nearer one's.
		std::sort(study.walk.begin(), study.walk.end(), [&](std::size_t first, std::size_t second) {
			const LengthEntry& firstEntry = study.entries[first];
			const LengthEntry& secondEntry = study.entries[second];
			if (firstEntry.recency != secondEntry.recency) {
				return firstEntry.recency > secondEntry.recency;
			}
			if (firstEntry.ownerMeters != secondEntry.ownerMeters) {
				return firstEntry.ownerMeters < secondEntry.ownerMeters;
			}
			return firstEntry.order < secondEntry.order;
		});

		const double mergeMeters = static_cast<double>(snapping.commonLengthMergeMeters) + snap_guides::kDistanceTieMeters;
		for (const std::size_t index : study.walk) {
			LengthEntry& entry = study.entries[index];
			const auto	 into = std::find_if(study.groups.begin(), study.groups.end(), [&](const LengthGroup& group) {
				  return std::abs(group.lengthMeters - entry.lengthMeters) <= mergeMeters;
			  });
			if (into == study.groups.end()) {
				entry.group = study.groups.size();
				study.groups.push_back(
					LengthGroup{
						.lengthMeters = entry.lengthMeters,
						.memberCount = 1,
						.newest = entry.recency,
						.nearestOwnerMeters = entry.ownerMeters,
					}
				);
				continue;
			}
			entry.group = static_cast<std::size_t>(into - study.groups.begin());
			if (std::abs(into->lengthMeters - entry.lengthMeters) > snap_guides::kCoincidenceMeters) {
				entry.status = LengthEntryStatus::Merged;
			}
			++into->memberCount;
			into->newest = std::max(into->newest, entry.recency);
			into->nearestOwnerMeters = std::min(into->nearestOwnerMeters, entry.ownerMeters);
		}

		for (std::size_t index = 0; index < study.groups.size(); ++index) {
			study.ranked.push_back(index);
		}
		// Newest member, then most members, then nearest owner, then shortest; full ties keep creation order.
		std::stable_sort(study.ranked.begin(), study.ranked.end(), [&](std::size_t first, std::size_t second) {
			const LengthGroup& firstGroup = study.groups[first];
			const LengthGroup& secondGroup = study.groups[second];
			if (firstGroup.newest != secondGroup.newest) {
				return firstGroup.newest > secondGroup.newest;
			}
			if (firstGroup.memberCount != secondGroup.memberCount) {
				return firstGroup.memberCount > secondGroup.memberCount;
			}
			if (firstGroup.nearestOwnerMeters != secondGroup.nearestOwnerMeters) {
				return firstGroup.nearestOwnerMeters < secondGroup.nearestOwnerMeters;
			}
			return firstGroup.lengthMeters < secondGroup.lengthMeters;
		});
		// Never more than the result reports, so no length catches without its label.
		const auto maxCount = std::min(static_cast<std::size_t>(std::max(0, snapping.commonLengthMaxCount)), kMaxReportedCommonLengths);
		study.keptCount = std::min(study.groups.size(), maxCount);
	}

	std::optional<Vec2d> targetAtLength(Vec2d segmentStart, double length, Vec2d lineOrigin, Vec2d lineDirection, Vec2d nearPoint) {
		const Vec2d	 foot = lineOrigin + lineDirection * dot(segmentStart - lineOrigin, lineDirection);
		const double offLine = distanceBetween(segmentStart, foot);
		if (offLine > length) {
			return std::nullopt;
		}
		const double along = std::sqrt(std::max(0.0, length * length - offLine * offLine));
		const Vec2d	 forward = foot + lineDirection * along;
		const Vec2d	 backward = foot + lineDirection * -along;
		return distanceBetween(forward, nearPoint) <= distanceBetween(backward, nearPoint) ? forward : backward;
	}

	LengthStep prepareLengthStep(const GuideContext& context, bool freeform, LengthStudy& scratch) {
		LengthStep step{
			.snapping = context.snapping,
			.rounding = !freeform && context.aids.roundLengths && context.snapping->lengthIncrementMeters > 0.0F,
		};
		if (!freeform && context.aids.commonLengths) {
			studyCommonLengths(context, scratch);
			step.study = &scratch;
		}
		if (!context.points->empty()) {
			step.segmentStart = toVec2d(context.points->back());
		}
		return step;
	}

	std::optional<LengthRefinement>
	refineLength(const LengthStep& step, Vec2d lineOrigin, Vec2d lineDirection, Vec2d currentPoint, bool allowRounding) {
		if (!step.segmentStart) {
			return std::nullopt;
		}
		const Vec2d start = *step.segmentStart;
		if (step.study != nullptr) {
			const LengthStudy&				study = *step.study;
			const double					catchMeters = static_cast<double>(step.snapping->commonLengthCatchMeters);
			std::optional<LengthRefinement> caught;
			// Rank order, so the smaller slide wins and an equal slide keeps the better rank.
			for (std::size_t rank = 0; rank < study.keptCount; ++rank) {
				const std::size_t	 group = study.ranked[rank];
				const double		 length = study.groups[group].lengthMeters;
				const std::optional<Vec2d> target = targetAtLength(start, length, lineOrigin, lineDirection, currentPoint);
				if (!target) {
					continue;
				}
				const double slide = distanceBetween(*target, currentPoint);
				if (slide <= catchMeters && (!caught || slide < caught->slideMeters)) {
					caught = LengthRefinement{
						.kind = LengthSnapKind::CommonLength,
						.point = *target,
						.lengthMeters = length,
						.measuredFrom = start,
						.group = group,
						.slideMeters = slide,
					};
				}
			}
			if (caught) {
				return caught;
			}
		}
		if (!allowRounding || !step.rounding) {
			return std::nullopt;
		}
		const double increment = static_cast<double>(step.snapping->lengthIncrementMeters);
		const double rounded = snap_guides::roundHalfUp(distanceBetween(start, currentPoint) / increment) * increment;
		if (rounded <= 0.0) {
			return std::nullopt;
		}
		const std::optional<Vec2d> target = targetAtLength(start, rounded, lineOrigin, lineDirection, currentPoint);
		if (!target) {
			return std::nullopt;
		}
		return LengthRefinement{
			.kind = LengthSnapKind::Rounded,
			.point = *target,
			.lengthMeters = rounded,
			.measuredFrom = start,
			.slideMeters = distanceBetween(*target, currentPoint),
		};
	}

	std::optional<LengthRefinement> roundAlongGuide(const LengthStep& step, Vec2d node, Vec2d direction, Vec2d currentPoint) {
		const double increment = static_cast<double>(step.snapping->lengthIncrementMeters);
		const double along = dot(currentPoint - node, direction);
		const double rounded = snap_guides::roundHalfUp(std::abs(along) / increment) * increment;
		if (rounded <= 0.0) {
			return std::nullopt;
		}
		const Vec2d target = node + direction * ((along > 0.0 ? 1.0 : -1.0) * rounded);
		return LengthRefinement{
			.kind = LengthSnapKind::RoundedAlongGuide,
			.point = target,
			.lengthMeters = rounded,
			.measuredFrom = node,
			.slideMeters = distanceBetween(target, currentPoint),
		};
	}

} // namespace construction::snap_lengths

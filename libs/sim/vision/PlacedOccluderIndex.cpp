#include "PlacedOccluderIndex.h"

#include <predicates/Predicates.h>

#include <algorithm>

namespace vision {

	namespace {
		// The bucket column or row holding `coordinate`, rounding toward minus infinity.
		std::int64_t bucketOf(std::int64_t coordinate) {
			return coordinate >= 0 ? coordinate / kOccluderBucketMm : -((-coordinate + kOccluderBucketMm - 1) / kOccluderBucketMm);
		}

		// Twice the ring's signed area: positive counter-clockwise.
		geometry::Int128 doubledArea(const geometry::Ring& ring) {
			geometry::Int128 sum(0);
			for (std::size_t index = 0; index < ring.size(); ++index) {
				sum = sum + geometry::cross(ring[index], ring[(index + 1) % ring.size()]);
			}
			return sum;
		}

		// Whether a counter-clockwise ring turns clockwise at any corner.
		bool concave(const geometry::Ring& ring) {
			for (std::size_t index = 0; index < ring.size(); ++index) {
				const geometry::Vec2i64& previous = ring[(index + ring.size() - 1) % ring.size()];
				const geometry::Vec2i64& next	  = ring[(index + 1) % ring.size()];
				if (geometry::cross(ring[index] - previous, next - ring[index]).sign() < 0) {
					return true;
				}
			}
			return false;
		}

		BodyKind placedKind(const PlacedOccluder& entry, std::int64_t opaqueLeastMm) {
			return entry.concave ? BodyKind::Opaque : kindOf(entry.diameterSquared, opaqueLeastMm);
		}
	} // namespace

	bool PlacedOccluder::within(geometry::Vec2i64 point, std::int64_t radiusMm) const {
		if (point.x + radiusMm < low.x || point.x - radiusMm > high.x || point.y + radiusMm < low.y || point.y - radiusMm > high.y) {
			return false;
		}
		for (std::size_t index = 0; index < outline.size(); ++index) {
			if (geometry::withinDistanceOfSegment(point, outline[index], outline[(index + 1) % outline.size()], radiusMm)) {
				return true;
			}
		}
		return false;
	}

	PlacedOccluderIndex::BucketKey PlacedOccluderIndex::bucketKey(std::int64_t column, std::int64_t row) {
		return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(column)) << 32) | static_cast<std::uint32_t>(row);
	}

	template <typename Visit> void PlacedOccluderIndex::eachBucket(geometry::Vec2i64 low, geometry::Vec2i64 high, Visit&& visit) {
		for (std::int64_t column = bucketOf(low.x); column <= bucketOf(high.x); ++column) {
			for (std::int64_t row = bucketOf(low.y); row <= bucketOf(high.y); ++row) {
				visit(bucketKey(column, row));
			}
		}
	}

	void PlacedOccluderIndex::add(geometry::nav::RingId ring, const geometry::Ring& outline, std::string defName, std::int64_t clockMs) {
		if (ring == geometry::nav::kNoRing || outline.size() < 3) {
			return;
		}
		// A ring id handed out again while still listed replaces its old entry, buckets and all.
		remove(ring);
		PlacedOccluder entry{.serial = nextSerial++, .ring = ring, .outline = outline, .arrivedMs = clockMs, .defName = std::move(defName)};
		if (doubledArea(entry.outline).sign() < 0) {
			std::reverse(entry.outline.begin(), entry.outline.end());
		}
		entry.low			  = entry.outline.front();
		entry.high			  = entry.outline.front();
		geometry::Vec2i64 sum{0, 0};
		for (std::size_t index = 0; index < entry.outline.size(); ++index) {
			const geometry::Vec2i64& corner = entry.outline[index];
			const geometry::Vec2i64	 side	= entry.outline[(index + 1) % entry.outline.size()] - corner;
			entry.low						= {std::min(entry.low.x, corner.x), std::min(entry.low.y, corner.y)};
			entry.high						= {std::max(entry.high.x, corner.x), std::max(entry.high.y, corner.y)};
			entry.perimeterMm += wholeSquareRoot(geometry::dot(side, side));
			sum = sum + corner;
		}
		const auto count	  = static_cast<std::int64_t>(entry.outline.size());
		entry.centre		  = {sum.x / count, sum.y / count};
		entry.diameterSquared = diameterSquared(entry.outline);
		entry.diameterMm	  = wholeSquareRoot(entry.diameterSquared);
		entry.concave		  = concave(entry.outline);
		entry.kind			  = placedKind(entry, opaqueLeastMm);
		eachBucket(entry.low, entry.high, [&](BucketKey key) { buckets[key].push_back(entry.serial); });
		serialOfRing.emplace(ring, entry.serial);
		bySerial.emplace(entry.serial, std::move(entry));
	}

	void PlacedOccluderIndex::remove(geometry::nav::RingId ring) {
		const auto found = serialOfRing.find(ring);
		if (found == serialOfRing.end()) {
			return;
		}
		const std::uint64_t	  serial = found->second;
		const PlacedOccluder& entry	 = bySerial.at(serial);
		eachBucket(entry.low, entry.high, [&](BucketKey key) {
			std::vector<std::uint64_t>& listed = buckets.at(key);
			std::erase(listed, serial);
			if (listed.empty()) {
				buckets.erase(key);
			}
		});
		bySerial.erase(serial);
		serialOfRing.erase(found);
	}

	void PlacedOccluderIndex::setOpaqueLeastDiameterMm(std::int64_t leastMm) {
		if (leastMm == opaqueLeastMm) {
			return;
		}
		opaqueLeastMm = leastMm;
		for (auto& [serial, entry] : bySerial) {
			entry.kind = placedKind(entry, opaqueLeastMm);
		}
	}

	void PlacedOccluderIndex::within(geometry::Vec2i64 point, std::int64_t radiusMm, std::vector<const PlacedOccluder*>& out) const {
		out.clear();
		if (radiusMm < 0 || bySerial.empty()) {
			return;
		}
		std::vector<std::uint64_t> serials;
		eachBucket({point.x - radiusMm, point.y - radiusMm}, {point.x + radiusMm, point.y + radiusMm}, [&](BucketKey key) {
			const auto bucket = buckets.find(key);
			if (bucket != buckets.end()) {
				serials.insert(serials.end(), bucket->second.begin(), bucket->second.end());
			}
		});
		std::ranges::sort(serials);
		const auto [duplicates, end] = std::ranges::unique(serials);
		serials.erase(duplicates, end);
		for (const std::uint64_t serial : serials) {
			const PlacedOccluder& entry = bySerial.at(serial);
			if (entry.within(point, radiusMm)) {
				out.push_back(&entry);
			}
		}
	}

	const PlacedOccluder* PlacedOccluderIndex::byRing(geometry::nav::RingId ring) const {
		const auto found = serialOfRing.find(ring);
		return found != serialOfRing.end() ? &bySerial.at(found->second) : nullptr;
	}

} // namespace vision

#pragma once

// The placed occluder index (sight-softening.md 2.4): every placed ring in the nav mesh as sight reads
// it, held by NavigationSystem on the main thread so vision never reads the mesh, whose patch job may
// still be running on its worker. NavigationSystem adds a ring when the patch that added it is
// collected, and drops one when the patch that removed it is, in patch order, so a ring id freed and
// handed to a new ring in one job leaves the old entry before the new one comes in. Every entry has a
// serial that never repeats; the index and every cache keyed on it go by serial, never by ring id.
//
// The ground is bucketed by kOccluderBucketMm squares, each ring listed in every bucket its box meets,
// so a query reads the buckets its box meets and no others.

#include "SightBody.h"
#include "SightTunables.h"

#include <core/Int128.h>
#include <core/Vec2i64.h>
#include <nav/NavMesh.h>
#include <visibility/Visibility.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace vision {

	constexpr std::int64_t kOccluderBucketMm = 16000;

	struct PlacedOccluder {
		std::uint64_t			serial = 0;
		geometry::nav::RingId	ring   = geometry::nav::kNoRing;
		// The ring's outline, counter-clockwise.
		geometry::Ring	  outline;
		geometry::Vec2i64 low;
		geometry::Vec2i64 high;
		// The mean of its vertices.
		geometry::Vec2i64 centre;
		// The sum of its edges' lengths, each the whole square root of its squared length.
		std::int64_t	 perimeterMm = 0;
		geometry::Int128 diameterSquared;
		std::int64_t	 diameterMm = 0;
		BodyKind		 kind		= BodyKind::Soft;
		// The sim clock (TimeSystem::simulationSeconds, in ms) when the index took it.
		std::int64_t arrivedMs = 0;
		std::string	 defName;

		// Whether any of its outline lies within radiusMm of `point`.
		[[nodiscard]] bool within(geometry::Vec2i64 point, std::int64_t radiusMm) const;
	};

	class PlacedOccluderIndex {
	  public:
		// Takes the ring `ring` with `outline` (either winding) under a new serial, its kind by
		// opaqueLeastDiameterMm. A ring the patch refused (geometry::nav::kNoRing) never enters, and an
		// entry already under `ring` leaves first.
		void add(geometry::nav::RingId ring, const geometry::Ring& outline, std::string defName, std::int64_t clockMs);
		// Drops the entry for `ring`, if there is one.
		void remove(geometry::nav::RingId ring);

		// Re-decides every entry's kind when the threshold moves.
		void setOpaqueLeastDiameterMm(std::int64_t leastMm);
		[[nodiscard]] std::int64_t opaqueLeastDiameterMm() const { return opaqueLeastMm; }

		// Every entry with some of its outline within radiusMm of `point` into `out` (cleared first),
		// in serial order. Reads the buckets the query's box meets.
		void within(geometry::Vec2i64 point, std::int64_t radiusMm, std::vector<const PlacedOccluder*>& out) const;

		[[nodiscard]] std::size_t			size() const { return bySerial.size(); }
		[[nodiscard]] const PlacedOccluder* byRing(geometry::nav::RingId ring) const;

	  private:
		using BucketKey = std::uint64_t;
		[[nodiscard]] static BucketKey bucketKey(std::int64_t column, std::int64_t row);
		template <typename Visit> static void eachBucket(geometry::Vec2i64 low, geometry::Vec2i64 high, Visit&& visit);

		std::int64_t											 opaqueLeastMm = SightTunables{}.placedOpaqueLeastDiameterMm;
		std::uint64_t											 nextSerial	   = 1;
		std::unordered_map<std::uint64_t, PlacedOccluder>		 bySerial;
		std::unordered_map<geometry::nav::RingId, std::uint64_t> serialOfRing;
		std::unordered_map<BucketKey, std::vector<std::uint64_t>> buckets;
	};

} // namespace vision

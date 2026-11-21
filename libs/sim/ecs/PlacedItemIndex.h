#pragma once

// The placed item index (rotatable-placed-items.md 11.3): the installed parts items and the pending install targets,
// each with its collider and reserved region (collider unioned with clearance, section 4.2) in the world. Placement,
// the snap engine, the wall tool, the dev spawn, and a crafted crate's install target read it through query(); nav,
// collision, and sight never do (installed items reach them as obstacles through ColliderEntityIndex), so a pending
// target is never an obstacle.
//
// Derived data, never saved: the installed items are the parts entries of ColliderEntityIndex, re-read when its
// revision moves, and the pending targets the crates the ECS holds with a target, diffed in place each refresh. A
// target handed out before its crate exists is held (hold) until the next refresh, which by then sees the crate.
// revision() moves on any change, so a reader settling on it (the still placement ghost) needs nothing else to notice
// an item installed, moved, turned, targeted, or removed.

#include "EntityID.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace assets {
	class AssetRegistry;
	struct AssetDefinition;
}

namespace ecs {

	class ColliderEntityIndex;
	class World;

	/// An item's outlines, each ring wound either way: its collider rings and its reserved region's outer rings
	/// (a hole in the region is taken as filled, as a hole in the collider is).
	struct ItemOutlines {
		std::vector<std::vector<glm::vec2>> collider;
		std::vector<std::vector<glm::vec2>> reserved;

		[[nodiscard]] bool empty() const { return collider.empty() && reserved.empty(); }
	};

	/// A parts def's outlines in the item's frame at turn 0; empty for any other def.
	[[nodiscard]] ItemOutlines itemOutlines(const assets::AssetDefinition& def);

	/// `local` put in the world at `position` turned `turnRadians` (assets::InstanceTransform at scale 1).
	[[nodiscard]] ItemOutlines placedOutlines(const ItemOutlines& local, glm::vec2 position, float turnRadians);

	class PlacedItemIndex {
	  public:
		struct Entry {
			EntityID	  entity = kInvalidEntity; ///< kInvalidEntity for a held target
			std::string	  defName;
			glm::vec2	  position{0.0F, 0.0F};
			float		  turnRadians = 0.0F;
			bool		  pending = false; ///< an install target, not yet installed
			ItemOutlines  world;			///< in the world
			glm::vec2	  lower{0.0F, 0.0F}; ///< the box of every outline, or the position when it has none
			glm::vec2	  upper{0.0F, 0.0F};
			std::uint64_t pass = 0;			 ///< the refresh that last saw it
		};

		/// Brings the index to the parts items `obstacles` holds and the crates with a target `world` holds, dropping
		/// the held targets, and moves revision() when anything changed.
		void refresh(const ColliderEntityIndex& obstacles, World& world, const assets::AssetRegistry& registry);

		/// Holds an item given a spot before a refresh can see it (a crafted crate's install target before the crate
		/// spawns, an item a dev spawn has just put down), as a pending entry, until the next refresh.
		void hold(const std::string& defName, glm::vec2 position, float turnRadians, const assets::AssetRegistry& registry);

		/// The entries whose box meets the box from `lower` to `upper`, appended to `out`: installed and pending in
		/// entity order, then the held targets.
		void query(glm::vec2 lower, glm::vec2 upper, std::vector<const Entry*>& out) const;

		/// `entity`'s entry, installed or pending.
		[[nodiscard]] const Entry*	find(EntityID entity) const;
		[[nodiscard]] std::size_t	size() const { return entries.size() + heldTargets.size(); }
		[[nodiscard]] std::uint64_t revision() const { return revisionCount; }

		/// `defName`'s outlines at turn 0 (itemOutlines), kept until the registry reloads.
		[[nodiscard]] const ItemOutlines& outlinesOf(const std::string& defName, const assets::AssetRegistry& registry) const;

	  private:
		struct Key {
			EntityID entity = kInvalidEntity;
			bool	 pending = false;
			bool	 operator<(const Key& other) const;
		};
		using BucketKey = std::uint64_t;
		[[nodiscard]] static BucketKey bucketKey(std::int32_t column, std::int32_t row);
		template <typename Visit> static void eachBucket(glm::vec2 lower, glm::vec2 upper, Visit&& visit);

		/// Puts `defName` at the spot under `key`, replacing an entry there that stands elsewhere; true when it changed.
		bool place(Key key, const std::string& defName, glm::vec2 position, float turnRadians, const assets::AssetRegistry& registry);
		void unlist(const Key& key, const Entry& entry);

		std::map<Key, Entry>							   entries;
		std::vector<Entry>								   heldTargets;
		std::unordered_map<BucketKey, std::vector<Key>> buckets;
		std::uint64_t									   revisionCount = 0;
		std::uint64_t									   pass = 0;
		std::uint64_t									   obstaclesSeen = UINT64_MAX;
		std::uint64_t									   registrySeen = UINT64_MAX;

		mutable std::unordered_map<std::string, ItemOutlines> outlinesByDef;
		mutable std::uint64_t								  outlinesGeneration = UINT64_MAX;
	};

} // namespace ecs

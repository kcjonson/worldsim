#pragma once

// The placed item index (rotatable-placed-items.md 11.2, 11.3): every installed parts item and every pending
// install target, a crate's Packaged.targetPosition and targetRotation, with each entry's collider rings and
// reserved region (collider unioned with clearance, section 4.2) in the world. Placement, the snap engine, the
// wall tool, and the dev spawn read it through query(); nav, collision, and sight never do (installed items reach
// them as obstacles through ColliderEntityIndex), so a pending target is never an obstacle.
//
// Derived data, like ColliderEntityIndex: refresh() reads the ECS and diffs it against what the index held, so it
// is never saved and a loaded world is indexed by its first refresh. revision() moves on any change, so a reader
// settling on it (the still placement ghost) needs nothing else to notice an item installed, moved, turned,
// targeted, or removed.

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
			EntityID	 entity = kInvalidEntity;
			std::string	 defName;
			glm::vec2	 position{0.0F, 0.0F};
			float		 turnRadians = 0.0F;
			bool		 pending = false; ///< an install target, not yet installed
			ItemOutlines world;			  ///< in the world
			glm::vec2	 lower{0.0F, 0.0F}; ///< the box of every outline, or the position when it has none
			glm::vec2	 upper{0.0F, 0.0F};
		};

		/// Brings the index to what `world` holds, moving revision() when anything changed.
		void refresh(World& world, const assets::AssetRegistry& registry);

		/// The entries whose box meets the box from `lower` to `upper`, in entity order, appended to `out`.
		void query(glm::vec2 lower, glm::vec2 upper, std::vector<const Entry*>& out) const;

		[[nodiscard]] const Entry* find(EntityID entity) const;
		[[nodiscard]] std::size_t  size() const { return entries.size(); }
		[[nodiscard]] std::uint64_t revision() const { return changes; }

		/// `defName`'s outlines at turn 0 (itemOutlines), kept until the registry reloads.
		[[nodiscard]] const ItemOutlines& outlinesOf(const std::string& defName, const assets::AssetRegistry& registry) const;

	  private:
		using BucketKey = std::uint64_t;
		[[nodiscard]] static BucketKey bucketKey(std::int32_t column, std::int32_t row);
		template <typename Visit> static void eachBucket(glm::vec2 lower, glm::vec2 upper, Visit&& visit);
		void								  list(const Entry& entry);
		void								  unlist(const Entry& entry);

		std::map<EntityID, Entry>					   entries;
		std::unordered_map<BucketKey, std::vector<EntityID>> buckets;
		std::uint64_t								   changes = 0;

		// Whether each entity's def is a parts def, looked up in the registry only when the entity is new or its def
		// name changed, so the colonists, piles, and props cost a compare a refresh.
		struct DefSeen {
			std::string	  defName;
			bool		  parts = false;
			std::uint64_t pass = 0;
		};
		std::unordered_map<EntityID, DefSeen> defsSeen;
		std::uint64_t						  pass = 0;

		mutable std::unordered_map<std::string, ItemOutlines> outlinesByDef;
		mutable std::uint64_t								  outlinesGeneration = 0;
	};

} // namespace ecs

#pragma once

// The placed item index (rotatable-placed-items.md 11.3): the installed parts items and the pending install targets,
// each with its collider, reserved region (collider unioned with clearance, section 4.2), and use areas in the world.
// Placement, the snap engine, the wall tool, the dev spawn, a crafted crate's install target, and whatever sends a
// colonist to use an item read it; nav, collision, and sight never do (installed items reach them as obstacles through
// ColliderEntityIndex), so a pending target is never an obstacle.
//
// Derived data, never saved: the installed items are the entities with a position and a parts def, packaged ones
// aside, flat items with no collider included, and the pending targets the crates the ECS holds with a target, all
// diffed in place each refresh. A target handed out before its crate exists is held (hold) until the next refresh,
// which by then sees the crate. revision() moves on any change, so a reader settling on it (the still placement ghost)
// needs nothing else to notice an item installed, moved, turned, targeted, removed, blocked, or cleared.
//
// Each entry carries whether a colonist can use it (section 12): its standing region and its use state, which
// PlacedItemSystem works out and sets (setUse); nothing else decides them.

#include "EntityID.h"

#include <assets/PartsAsset.h>
#include <assets/UseAreaRoom.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace assets {
	class AssetRegistry;
	struct AssetDefinition;
}

namespace ecs {

	class World;

	/// An item's outlines: its collider rings and its reserved region's outer rings, each wound either way (a hole in the
	/// region is taken as filled, as a hole in the collider is), and each use area's polygons.
	struct ItemOutlines {
		std::vector<std::vector<glm::vec2>>		  collider;
		std::vector<std::vector<glm::vec2>>		  reserved;
		std::vector<std::vector<assets::AreaPolygon>> useAreas;

		[[nodiscard]] bool empty() const { return collider.empty() && reserved.empty() && useAreas.empty(); }
	};

	/// A parts def's outlines in the item's frame at turn 0; empty for any other def.
	[[nodiscard]] ItemOutlines itemOutlines(const assets::AssetDefinition& def);

	/// `local` put in the world at `position` turned `turnRadians` (assets::InstanceTransform at scale 1).
	[[nodiscard]] ItemOutlines placedOutlines(const ItemOutlines& local, glm::vec2 position, float turnRadians);

	/// Whether a colonist can use an item (section 12): some use area has room; none has and some use area ground is
	/// unexplored or not meshed; or none has (blocked).
	enum class UseState : std::uint8_t { Usable, Unexplored, Blocked };

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
			glm::vec2	  useLower{0.0F, 0.0F}; ///< the box of its use areas, or the position when it has none
			glm::vec2	  useUpper{0.0F, 0.0F};
			UseState	  use = UseState::Usable;
			/// Where a colonist may stand to use it, every use area's standing region together, as last worked out.
			assets::StandingRegion standing;
			std::uint64_t pass = 0; ///< the refresh that last saw it
		};

		/// Brings the index to the parts items and the crates with a target `world` holds, dropping the held targets, and
		/// moves revision() when anything changed.
		void refresh(World& world, const assets::AssetRegistry& registry);

		/// Holds an item given a spot before a refresh can see it (a crafted crate's install target before the crate
		/// spawns, an item a dev spawn has just put down), as a pending entry, until the next refresh.
		void hold(const std::string& defName, glm::vec2 position, float turnRadians, const assets::AssetRegistry& registry);

		/// The entries whose box (outlines and use areas) meets the box from `lower` to `upper`, appended to `out`:
		/// installed and pending in entity order, then the held targets.
		void query(glm::vec2 lower, glm::vec2 upper, std::vector<const Entry*>& out) const;

		/// `entity`'s entry, installed or pending.
		[[nodiscard]] const Entry* find(EntityID entity) const;
		/// The installed item standing exactly at `position`, the point a task aims at for it; null for none.
		[[nodiscard]] const Entry* installedAt(glm::vec2 position) const;
		/// Every installed and pending entry, in entity order.
		template <typename Visit> void forEach(Visit&& visit) const {
			for (const auto& [key, entry] : entries) {
				visit(entry);
			}
		}

		[[nodiscard]] std::size_t	size() const { return entries.size() + heldTargets.size(); }
		[[nodiscard]] std::uint64_t revision() const { return revisionCount; }

		/// The boxes of the entries the last refresh added, moved, or removed: where an item's standing region may have
		/// changed because of them.
		[[nodiscard]] const std::vector<std::pair<glm::vec2, glm::vec2>>& changedBoxes() const { return changed; }

		/// Sets the use state and standing region of `entity`'s entry, installed or pending, moving revision() when its
		/// state changes.
		void setUse(EntityID entity, bool pending, UseState use, assets::StandingRegion standing);

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
		std::vector<std::pair<glm::vec2, glm::vec2>>	   changed;
		std::uint64_t									   revisionCount = 0;
		std::uint64_t									   pass = 0;
		std::uint64_t									   registrySeen = UINT64_MAX;

		mutable std::unordered_map<std::string, ItemOutlines> outlinesByDef;
		mutable std::uint64_t								  outlinesGeneration = UINT64_MAX;
	};

} // namespace ecs

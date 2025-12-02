#pragma once

// Placement System Types
// Data structures for entity placement relationships and spawn rules.
// Used by the placement system to determine spawn order and probability modifiers.

#include <string>

namespace engine::assets {

	/// How to reference an entity in relationships
	struct EntityRef {
		enum class Type {
			DefName, // Reference by specific asset defName (e.g., "Flora_TreeOak")
			Group,	 // Reference by group name (e.g., "trees", "flowers")
			Same	 // Reference to same type as self (for avoids type="same")
		};
		Type		type = Type::DefName;
		std::string value; // defName or group name (empty if Same)
	};

	/// Relationship rule kinds
	enum class RelationshipKind {
		Requires, // Must have nearby entity to spawn (hard dependency)
		Affinity, // More likely to spawn near entity (soft preference)
		Avoids	  // Less likely to spawn near entity (soft avoidance)
	};

	/// A single relationship rule parsed from asset XML.
	/// Defines how this asset relates to other entities for spawn probability.
	struct PlacementRelationship {
		RelationshipKind kind = RelationshipKind::Affinity;
		EntityRef		 target;		 // What entity/group we relate to
		float			 distance = 5.0F; // Radius in tiles for neighbor check
		float			 strength = 1.5F; // Multiplier for affinity (>1 = more likely)
		float			 penalty = 0.5F;  // Multiplier for avoids (<1 = less likely)
		bool			 required = false; // For Requires: must exist or spawn fails
	};

} // namespace engine::assets

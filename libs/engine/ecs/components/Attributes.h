#pragma once

// Attributes Component - a colonist's static physical and mental attributes.
// Assigned at generation, 0.0-20.0 scale (mirrors Skills). Today only Strength is wired:
// it derives hand-carry capacity. The rest are defined so upcoming systems (melee, labor
// speed, social) have a home; backstory/trait-driven rolls come later.
//
// See /docs/design/game-systems/colonists/attributes.md for design details.

namespace ecs {

	/// Static colonist attributes (0.0 = none, 20.0 = exceptional). Rolled at generation.
	struct Attributes {
		// Physical
		float strength = 10.0F;		// Hand-carry capacity (wired); melee/labor speed (future)
		float agility = 10.0F;		// Movement speed, dexterity for fine work (future)
		float intelligence = 10.0F; // Learning speed, research (future)

		// Mental / social
		float civility = 10.0F;
		float sanity = 10.0F;
		float kindness = 10.0F;
		float workEthic = 10.0F;
		float socialNeed = 10.0F;

		/// Hand-carry weight cap (kg) derived from Strength: 20kg base + 1.5kg per point. An
		/// average colonist (strength 10) carries 35kg (the prior flat value); the 0-20 range
		/// spans 20-50kg.
		[[nodiscard]] static float carryCapacityKg(float strengthLevel) {
			return 20.0F + 1.5F * strengthLevel;
		}
	};

} // namespace ecs

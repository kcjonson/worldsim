#pragma once

// Attributes Component - a colonist's static physical and mental attributes.
// Rolled at spawn from assets/config/colonists/attributes, 0.0-20.0 scale (mirrors Skills). Only
// Strength is read by a system directly: it derives hand-carry capacity. Thoughts read the rest through
// attribute(name) conditions. Traits never change an attribute; they act through conditions.
//
// See /docs/design/game-systems/colonists/attributes.md for design details.

#include <assets/AttributeConfig.h>

#include <array>
#include <random>
#include <string_view>

namespace ecs {

	/// Static colonist attributes (0.0 = none, 20.0 = exceptional). Built from a roll, never from defaults.
	struct Attributes {
		// Physical
		float strength;		 // Hand-carry capacity (wired); melee/labor speed (future)
		float agility;		 // Movement speed, dexterity for fine work (future)
		float intelligence;	 // Learning speed, research (future)

		// Mental / social
		float civility;
		float sanity;
		float kindness;
		float workEthic;
		float socialNeed;

		/// Hand-carry weight cap (kg) derived from Strength: 20kg base + 1.5kg per point. An
		/// average colonist (strength 10) carries 35kg (the prior flat value); the 0-20 range
		/// spans 20-50kg.
		[[nodiscard]] static float carryCapacityKg(float strengthLevel) {
			return 20.0F + 1.5F * strengthLevel;
		}
	};

	/// One attribute: the name conditions and the config write (attribute(work_ethic)), and its member.
	/// The one table every reader of attributes goes through.
	struct AttributeField {
		std::string_view   id;
		float Attributes::* value;
	};

	inline constexpr std::array<AttributeField, 8> kAttributeFields = {{
		{"strength", &Attributes::strength},
		{"agility", &Attributes::agility},
		{"intelligence", &Attributes::intelligence},
		{"civility", &Attributes::civility},
		{"sanity", &Attributes::sanity},
		{"kindness", &Attributes::kindness},
		{"work_ethic", &Attributes::workEthic},
		{"social_need", &Attributes::socialNeed},
	}};

	/// The range each attribute is rolled in, in kAttributeFields order. Loaded by loadColonistRules.
	struct AttributeRolls {
		std::array<assets::AttributeRange, kAttributeFields.size()> ranges{};
	};

	/// Roll every attribute from `rng`, one draw each in kAttributeFields order, evenly across its range.
	[[nodiscard]] Attributes rollAttributes(const AttributeRolls& rolls, std::mt19937& rng);

} // namespace ecs

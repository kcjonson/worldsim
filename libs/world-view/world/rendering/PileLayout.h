#pragma once

#include "assets/AssetDefinition.h"
#include "assets/placement/SpatialIndex.h"	 // PlacedEntity
#include "assets/placement/WorldDepthSort.h" // computeAnchorY
#include "graphics/Rect.h"
#include "world/rendering/AssetInstanceTransform.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

// Layout for a loose ground pile (an entity with a ResourceStack): 1 to 5 copies of the
// item's sprite heaped around the entity position, so a 30-wood pile reads bigger than a
// single log. Shared by the dynamic render path, the selection hit-test, the selection
// outline, and the drop footprint so all four agree on the sprites and can't drift.
//
// Each sprite is a position-as-origin instance (rotation 0): its bbox center lands at
// entity position + offset through the same dynamic (bbox-centered) transform a lone
// dynamic entity uses, so a pile of one draws exactly where the entity used to.

namespace worldview {

	inline constexpr std::size_t kMaxPileSprites = 5;

	struct PileLayout {
		std::array<assets::PlacedEntity, kMaxPileSprites> sprites;
		std::size_t										   count = 0;
	};

	/// The item properties a pile's layout reads: its worldHeight (sizes the heap offsets) and
	/// stackSize (0 = unbounded). Defaults stand in for an unknown def.
	struct PileItem {
		float		  worldHeight = 0.6F;
		std::uint32_t stackSize = 0;
	};

	[[nodiscard]] inline PileItem pileItem(const assets::AssetDefinition* def) {
		PileItem item;
		if (def != nullptr) {
			item.worldHeight = def->worldHeight;
			if (def->itemProperties.has_value()) {
				item.stackSize = def->itemProperties->stackSize;
			}
		}
		return item;
	}

	/// How many sprites a pile of `quantity` draws: 1 for a single unit, otherwise the fill
	/// fraction of a stack mapped onto 2..5. An unbounded stack (stackSize 0 or UINT32_MAX)
	/// draws one sprite per unit, up to 5.
	[[nodiscard]] inline std::size_t pileSpriteCount(std::uint32_t quantity, std::uint32_t stackSize) {
		if (quantity <= 1) {
			return 1;
		}
		if (stackSize == 0 || stackSize == UINT32_MAX) {
			return quantity < kMaxPileSprites ? static_cast<std::size_t>(quantity) : kMaxPileSprites;
		}
		const std::uint64_t scaled =
			(static_cast<std::uint64_t>(quantity) * kMaxPileSprites + stackSize - 1) / stackSize; // ceil(fill * 5)
		if (scaled < 2) {
			return 2;
		}
		return scaled > kMaxPileSprites ? kMaxPileSprites : static_cast<std::size_t>(scaled);
	}

	/// `itemBounds` is the item template's local bounds in meters (templateBounds).
	[[nodiscard]] inline PileLayout pileLayout(
		glm::vec2				posValue,
		const std::string&		defName,
		float					scale,
		glm::vec4				colorTint,
		const Foundation::Rect& itemBounds,
		const PileItem&			item,
		std::uint32_t			quantity
	) {
		// Rows stepped in Y, overlapping so each front row covers the bottom of the one behind
		// (reads as a stack, not a scatter), with a small alternating X stagger. Units of the
		// item's worldHeight times `scale`, so a 0.4 m log heaps 5 deep inside ~0.5 m and the
		// whole heap (and its footprint radius) scales linearly with the sprites.
		constexpr float								  kRowStep = 0.3F;
		constexpr std::array<float, kMaxPileSprites> kStagger{0.0F, 0.12F, -0.10F, 0.08F, -0.12F};

		const glm::vec2 boundsCenter{itemBounds.x + itemBounds.width * 0.5F, itemBounds.y + itemBounds.height * 0.5F};
		const float		meshMaxY = itemBounds.y + itemBounds.height;

		PileLayout out;
		out.count = pileSpriteCount(quantity, item.stackSize);
		const float unit = item.worldHeight * scale;
		const float firstRow = -0.5F * static_cast<float>(out.count - 1) * kRowStep;
		for (std::size_t i = 0; i < out.count; ++i) {
			const glm::vec2 center{
				posValue.x + kStagger[i] * unit,
				posValue.y + (firstRow + static_cast<float>(i) * kRowStep) * unit,
			};
			assets::PlacedEntity& sprite = out.sprites[i];
			sprite.defName = defName;
			sprite.position = dynamicInstanceTransform(center, scale, boundsCenter).origin;
			sprite.rotation = 0.0F;
			sprite.scale = scale;
			sprite.colorTint = colorTint;
			sprite.anchorY = assets::computeAnchorY(sprite.position.y, meshMaxY, scale);
		}
		return out;
	}

	/// Radius of the circle around a pile's position that holds every sprite of the FULLEST pile
	/// of this item (a full stack; one sprite for an unstackable item), so a pile that grows by
	/// top-ups never outgrows the room a drop left it. The drop planner keeps footprints apart.
	[[nodiscard]] inline float pileFootprintRadius(const Foundation::Rect& itemBounds, float scale, const PileItem& item) {
		const std::uint32_t fullStack =
			(item.stackSize == 0 || item.stackSize == UINT32_MAX) ? static_cast<std::uint32_t>(kMaxPileSprites) : item.stackSize;
		const PileLayout pile = pileLayout({0.0F, 0.0F}, std::string{}, scale, {1.0F, 1.0F, 1.0F, 1.0F}, itemBounds, item, fullStack);
		float			 reachX = 0.0F;
		float			 reachY = 0.0F;
		for (std::size_t i = 0; i < pile.count; ++i) {
			const glm::vec2 lo = pile.sprites[i].position + glm::vec2{itemBounds.x, itemBounds.y} * scale;
			const glm::vec2 hi = lo + glm::vec2{itemBounds.width, itemBounds.height} * scale;
			reachX = std::max({reachX, std::abs(lo.x), std::abs(hi.x)});
			reachY = std::max({reachY, std::abs(lo.y), std::abs(hi.y)});
		}
		return std::hypot(reachX, reachY);
	}

} // namespace worldview

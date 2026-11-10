#pragma once

#include "assets/placement/SpatialIndex.h" // PlacedEntity
#include "graphics/Rect.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <string>

// Layout for packaged furniture resting on the ground: a side-view crate drawn in two layers
// with the item fitted inside between them (back crate, item, front crate). Shared by the
// dynamic render path, the selection hit-test, and the selection outline so all three place
// the same sprites and can't drift.
//
// The crate is PackagingCrate (back) + PackagingCrateFront: both SVGs span 0..100 x 0..75 at
// worldHeight 0.75, so one SVG unit is 1 cm and the constants below are those coordinates in
// meters, measured from the crate's top-left. The crate, not the item, sets the footprint.

namespace worldview {

	inline constexpr float kCrateWidth = 1.0F;
	inline constexpr float kCrateHeight = 0.75F;
	inline constexpr float kCrateInnerLeft = 0.095F;  // inside edge of the left corner post
	inline constexpr float kCrateInnerRight = 0.905F; // inside edge of the right corner post
	inline constexpr float kCrateInnerTop = 0.06F;	  // just under the back wall's top rim
	inline constexpr float kCrateFloorY = 0.70F;	  // where the item's bottom rests
	inline constexpr float kCrateItemMargin = 0.03F;  // kept between the item and the posts / rim

	struct PackagedLayout {
		assets::PlacedEntity crateBack;
		assets::PlacedEntity item;
		assets::PlacedEntity crateFront;
	};

	/// The packaged item at `posValue` (bottom-center of the crate). `itemBounds` is the item
	/// template's local bounds (templateBounds): the item is drawn at `itemScale` (its placed size)
	/// or smaller, uniformly shrunk to fit the crate's opening, centered horizontally, resting on
	/// the crate floor. All three sprites share the ground baseline as anchorY; equal keys + the
	/// stable depth sort keep them in build order (back, item, front).
	[[nodiscard]] inline PackagedLayout packagedLayout(
		glm::vec2 posValue, const std::string& itemDefName, float itemScale, glm::vec4 itemColorTint, const Foundation::Rect& itemBounds
	) {
		const float		bottomY = posValue.y;
		const glm::vec2 crateTopLeft{posValue.x - kCrateWidth * 0.5F, bottomY - kCrateHeight};

		const float openingWidth = (kCrateInnerRight - kCrateInnerLeft) - 2.0F * kCrateItemMargin;
		const float openingHeight = (kCrateFloorY - kCrateInnerTop) - kCrateItemMargin;
		float		fit = itemScale;
		if (itemBounds.width > 0.0F) {
			fit = std::min(fit, openingWidth / itemBounds.width);
		}
		if (itemBounds.height > 0.0F) {
			fit = std::min(fit, openingHeight / itemBounds.height);
		}

		auto crateSprite = [&](const char* defName) {
			assets::PlacedEntity sprite;
			sprite.defName = defName;
			sprite.position = crateTopLeft;
			sprite.rotation = 0.0F;
			sprite.scale = 1.0F;
			sprite.colorTint = {1.0F, 1.0F, 1.0F, 1.0F};
			sprite.anchorY = bottomY;
			return sprite;
		};

		PackagedLayout out;
		out.crateBack = crateSprite("PackagingCrate");
		out.crateFront = crateSprite("PackagingCrateFront");

		// Position-as-origin instance: world = local * fit + position. Center the scaled bbox on
		// the crate and put its bottom on the floor line.
		const float itemCenterLocalX = itemBounds.x + itemBounds.width * 0.5F;
		const float itemBottomLocalY = itemBounds.y + itemBounds.height;
		out.item.defName = itemDefName;
		out.item.position = {posValue.x - itemCenterLocalX * fit, crateTopLeft.y + kCrateFloorY - itemBottomLocalY * fit};
		out.item.rotation = 0.0F;
		out.item.scale = fit;
		out.item.colorTint = itemColorTint;
		out.item.anchorY = bottomY;
		return out;
	}

	/// The crate's drawn footprint as a circle (center, radius) around the box it fills, for
	/// keeping ground drops clear of it.
	[[nodiscard]] inline glm::vec2 packagedFootprintCenter(glm::vec2 posValue) {
		return {posValue.x, posValue.y - kCrateHeight * 0.5F};
	}
	inline constexpr float kPackagedFootprintRadius = 0.625F; // half the crate's diagonal
	static_assert(
		kPackagedFootprintRadius * kPackagedFootprintRadius
		== (kCrateWidth * 0.5F) * (kCrateWidth * 0.5F) + (kCrateHeight * 0.5F) * (kCrateHeight * 0.5F)
	);

} // namespace worldview

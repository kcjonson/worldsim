#pragma once

#include "assets/placement/SpatialIndex.h" // PlacedEntity

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <string>

// Layout for a packaged item resting on the ground: a crate drawn behind, the
// shrunk item in front, both bottom-aligned at the entity position. Extracted so
// the dynamic render path, the selection hit-test, and the selection outline all
// place the crate + item identically and can't drift (the item width is an
// estimate from its height, so a single source is essential).

namespace engine::world {

	struct PackagedLayout {
		engine::assets::PlacedEntity crate;
		engine::assets::PlacedEntity item;
	};

	inline PackagedLayout packagedLayout(
		glm::vec2		   posValue,
		const std::string& itemDefName,
		float			   itemScale,
		glm::vec4		   itemColorTint,
		float			   itemWorldHeight
	) {
		constexpr float kPackagedScaleFactor = 0.85F; // packaged items render at 85% of tile size
		constexpr float kCrateWorldHeight	 = 0.2F;  // PackagingCrate's worldHeight
		constexpr float kCrateWidth			 = 1.0F;  // PackagingCrate is 1 m wide
		constexpr float kItemLiftOffset		 = 0.03F; // lift item ~2px at typical zoom
		constexpr float kItemAspectRatio	 = 1.4F;  // estimate item width from height (~BasicBox 40x28)

		const float scaledItemHeight = itemWorldHeight * kPackagedScaleFactor;
		const float scaledItemWidth	 = itemWorldHeight * kItemAspectRatio * kPackagedScaleFactor;
		const float bottomY			 = posValue.y; // shared ground baseline
		const float crateCenterX	 = posValue.x;

		PackagedLayout out;
		out.crate.defName	= "PackagingCrate";
		out.crate.position	= {crateCenterX - kCrateWidth * 0.5F, bottomY - kCrateWorldHeight};
		out.crate.rotation	= 0.0F;
		out.crate.scale		= 1.0F;
		out.crate.colorTint = {1.0F, 1.0F, 1.0F, 1.0F};
		// Crate + item share the ground baseline; equal anchorY + stable sort keeps the
		// crate (built first) behind the item after the global depth sort.
		out.crate.anchorY = bottomY;

		out.item.defName   = itemDefName;
		out.item.position  = {crateCenterX - scaledItemWidth * 0.5F, bottomY - scaledItemHeight - kItemLiftOffset};
		out.item.rotation  = 0.0F;
		out.item.scale	   = itemScale * kPackagedScaleFactor;
		out.item.colorTint = itemColorTint;
		out.item.anchorY   = bottomY;
		return out;
	}

} // namespace engine::world

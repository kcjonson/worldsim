#include "world/rendering/PackagedLayout.h"

#include "assets/AssetRegistry.h"
#include "assets/placement/PlacementExecutor.h"
#include "assets/placement/RenderContext.h"
#include "assets/placement/WorldDepthSort.h"
#include "world/camera/WorldCamera.h"

#include <gtest/gtest.h>

#include <unordered_set>
#include <vector>

using namespace worldview;

namespace {

	// The item's world box under its position-as-origin transform.
	struct Box {
		float minX, minY, maxX, maxY;
	};
	Box itemBox(const assets::PlacedEntity& item, const Foundation::Rect& b) {
		return {
			item.position.x + b.x * item.scale,
			item.position.y + b.y * item.scale,
			item.position.x + (b.x + b.width) * item.scale,
			item.position.y + (b.y + b.height) * item.scale,
		};
	}

} // namespace

// Draw order is back crate, item, front crate, all on one ground baseline.
TEST(PackagedLayout, BackItemFrontShareBaseline) {
	const auto pl = packagedLayout({3.0F, 7.0F}, "BasicBox", 1.0F, {1, 1, 1, 1}, Foundation::Rect{0.0F, 0.0F, 0.857F, 0.6F});
	EXPECT_EQ(pl.crateBack.defName, "PackagingCrate");
	EXPECT_EQ(pl.crateFront.defName, "PackagingCrateFront");
	EXPECT_FLOAT_EQ(pl.crateBack.anchorY, 7.0F);
	EXPECT_FLOAT_EQ(pl.item.anchorY, 7.0F);
	EXPECT_FLOAT_EQ(pl.crateFront.anchorY, 7.0F);
	EXPECT_FLOAT_EQ(pl.crateBack.position.x, 3.0F - kCrateWidth * 0.5F);
	EXPECT_FLOAT_EQ(pl.crateBack.position.y, 7.0F - kCrateHeight);
}

// A wide item is shrunk uniformly to fit inside the posts, centered, bottom on the crate floor.
TEST(PackagedLayout, WideItemFitsInsideCrate) {
	const Foundation::Rect bounds{-0.6F, -1.0F, 1.6F, 0.5F}; // off-origin template bbox
	const auto			   pl = packagedLayout({0.0F, 0.0F}, "Bench", 1.0F, {1, 1, 1, 1}, bounds);
	const Box			   box = itemBox(pl.item, bounds);
	const float			   crateLeft = -kCrateWidth * 0.5F;
	const float			   crateTop = -kCrateHeight;

	EXPECT_LT(pl.item.scale, 1.0F);
	EXPECT_GE(box.minX, crateLeft + kCrateInnerLeft - 1e-5F);
	EXPECT_LE(box.maxX, crateLeft + kCrateInnerRight + 1e-5F);
	EXPECT_GE(box.minY, crateTop + kCrateInnerTop - 1e-5F);
	EXPECT_NEAR(box.maxY, crateTop + kCrateFloorY, 1e-5F);
	EXPECT_NEAR((box.minX + box.maxX) * 0.5F, 0.0F, 1e-5F);
	EXPECT_NEAR((box.maxX - box.minX) / (box.maxY - box.minY), 1.6F / 0.5F, 1e-4F) << "uniform scale";
}

// A small item is never blown up to fill the crate.
TEST(PackagedLayout, SmallItemKeepsItsSize) {
	const auto pl = packagedLayout({0.0F, 0.0F}, "Stool", 1.0F, {1, 1, 1, 1}, Foundation::Rect{0.0F, 0.0F, 0.3F, 0.3F});
	EXPECT_FLOAT_EQ(pl.item.scale, 1.0F);
	const auto half = packagedLayout({0.0F, 0.0F}, "Stool", 0.5F, {1, 1, 1, 1}, Foundation::Rect{0.0F, 0.0F, 0.3F, 0.3F});
	EXPECT_FLOAT_EQ(half.item.scale, 0.5F) << "drawn at its placed size, not the crate's";
}

// The layout's three sprites go through the real depth sort with an unrelated entity on the
// same baseline ahead of them, and still come out back, item, front.
TEST(PackagedLayout, LayersStayOrderedThroughDepthSort) {
	const auto			 pl = packagedLayout({2.0F, 5.0F}, "BasicBox", 1.0F, {1, 1, 1, 1}, Foundation::Rect{0.0F, 0.0F, 0.857F, 0.6F});
	assets::PlacedEntity other;
	other.defName = "Rock";
	other.position = {2.0F, 4.0F};
	other.anchorY = pl.item.anchorY;

	const std::vector<assets::PlacedEntity>			dynamics = {other, pl.crateBack, pl.item, pl.crateFront};
	const assets::PlacementExecutor					executor(assets::AssetRegistry::Get());
	const std::unordered_set<world::ChunkCoordinate> noChunks;
	const world::WorldCamera						camera;
	const assets::RenderContext						ctx{executor, noChunks, &dynamics, camera, 800, 600, 16.0F, 1};
	assets::WorldDepthGather						gather;
	std::vector<assets::DepthSortItem>				items;
	gather.gather(ctx, items);

	ASSERT_EQ(items.size(), 4U);
	auto indexOf = [&](const assets::PlacedEntity* e) {
		for (int i = 0; i < static_cast<int>(items.size()); ++i) {
			if (items[i].upright->entity == e) {
				return i;
			}
		}
		return -1;
	};
	EXPECT_LT(indexOf(&dynamics[1]), indexOf(&dynamics[2]));
	EXPECT_LT(indexOf(&dynamics[2]), indexOf(&dynamics[3]));
}

// Unit tests for the GL-free core of world-space 2.5D depth sorting: the
// canonical anchorY derivation (bottom-most ground-contact world-Y) and the
// stable ascending merge/sort. These are the pure pieces the render paths share;
// the gather + emit steps need a GL context and are exercised in-game.

#include "world/rendering/WorldDepthSort.h"

#include <vector/Types.h>

#include <gtest/gtest.h>

#include <vector>

namespace engine::world {
	namespace {

		// Build a template mesh whose local-Y spans [minLocalY, maxLocalY]. Under
		// the art convention +local-Y is toward the ground (south), so maxLocalY is
		// the ground-contact line (trunk base / feet).
		renderer::TessellatedMesh makeMesh(float minLocalY, float maxLocalY) {
			renderer::TessellatedMesh mesh;
			mesh.vertices.emplace_back(-0.5F, minLocalY);
			mesh.vertices.emplace_back(0.5F, minLocalY);
			mesh.vertices.emplace_back(0.0F, maxLocalY);
			return mesh;
		}

	} // namespace

	TEST(WorldDepthSort, MeshYExtentScansVertices) {
		const renderer::TessellatedMesh mesh = makeMesh(-3.0F, 0.5F);
		const MeshYExtent				ext = meshYExtent(&mesh);
		EXPECT_FLOAT_EQ(ext.minY, -3.0F);
		EXPECT_FLOAT_EQ(ext.maxY, 0.5F);
	}

	TEST(WorldDepthSort, MeshYExtentNullOrEmptyIsZero) {
		EXPECT_FLOAT_EQ(meshYExtent(nullptr).maxY, 0.0F);
		const renderer::TessellatedMesh empty;
		EXPECT_FLOAT_EQ(meshYExtent(&empty).minY, 0.0F);
		EXPECT_FLOAT_EQ(meshYExtent(&empty).maxY, 0.0F);
	}

	// Static occluder: anchorY = position.y + templateMaxY * scale (the trunk base
	// in world space, the bottom-most world-Y of the rendered mesh).
	TEST(WorldDepthSort, StaticTreeAnchorIsTrunkBaseWorldY) {
		const renderer::TessellatedMesh tree = makeMesh(-3.0F, 0.5F);
		const MeshYExtent				ext = meshYExtent(&tree);
		const float						positionY = 10.0F;
		const float						scale = 2.0F;
		const float						anchorY = computeAnchorY(positionY, ext.maxY, scale);
		EXPECT_FLOAT_EQ(anchorY, 11.0F); // 10 + 0.5 * 2
	}

	// Dynamic colonist: anchorY = position.y + meshMaxY * scale (its feet).
	TEST(WorldDepthSort, ColonistAnchorIsFeetWorldY) {
		const renderer::TessellatedMesh body = makeMesh(-0.9F, 0.9F);
		const MeshYExtent				ext = meshYExtent(&body);
		const float						placedPositionY = 20.0F;
		const float						scale = 1.0F;
		const float						anchorY = computeAnchorY(placedPositionY, ext.maxY, scale);
		EXPECT_FLOAT_EQ(anchorY, 20.9F);
	}

	// Packaged crate + item share the ground baseline; equal anchorY + stable sort
	// keeps the crate (added first) behind the item after the global sort.
	TEST(WorldDepthSort, PackagedPairSharesBaselineAndStaysOrdered) {
		const float bottomY = 5.0F;

		assets::PlacedEntity crate;
		crate.defName = "PackagingCrate";
		crate.anchorY = bottomY; // == computeAnchorY(bottomY, 0, 1)
		assets::PlacedEntity item;
		item.defName = "BasicBox";
		item.anchorY = bottomY;
		assets::PlacedEntity other; // unrelated entity at the same baseline
		other.defName = "Rock";
		other.anchorY = bottomY;

		EXPECT_FLOAT_EQ(crate.anchorY, item.anchorY);

		// Interleave an unrelated equal-key entity ahead of the pair; the pair must
		// still emerge crate-before-item.
		std::vector<DepthSortItem> items = {
			{other.anchorY, &other, false},
			{crate.anchorY, &crate, false},
			{item.anchorY, &item, false},
		};
		sortByAnchorY(items);

		ASSERT_EQ(items.size(), 3U);
		int crateIdx = -1;
		int itemIdx = -1;
		for (int i = 0; i < static_cast<int>(items.size()); ++i) {
			if (items[i].entity == &crate) {
				crateIdx = i;
			}
			if (items[i].entity == &item) {
				itemIdx = i;
			}
		}
		EXPECT_LT(crateIdx, itemIdx);
	}

	TEST(WorldDepthSort, SortIsAscendingStableAndDeterministic) {
		assets::PlacedEntity a;
		a.defName = "A";
		assets::PlacedEntity b;
		b.defName = "B";
		assets::PlacedEntity c;
		c.defName = "C";
		assets::PlacedEntity d;
		d.defName = "D";

		// Two entries share anchorY == 1.0 (b then d) to check stability.
		std::vector<DepthSortItem> items = {
			{3.0F, &a, false},
			{1.0F, &b, false},
			{2.0F, &c, false},
			{1.0F, &d, false},
		};

		std::vector<DepthSortItem> once = items;
		sortByAnchorY(once);

		// Ascending by anchorY.
		for (size_t i = 1; i < once.size(); ++i) {
			EXPECT_LE(once[i - 1].anchorY, once[i].anchorY);
		}
		// Stable: equal keys keep insertion order (b before d).
		EXPECT_EQ(once[0].entity, &b);
		EXPECT_EQ(once[1].entity, &d);
		EXPECT_EQ(once[2].entity, &c);
		EXPECT_EQ(once[3].entity, &a);

		// Deterministic: sorting the same input again yields the same order.
		std::vector<DepthSortItem> twice = items;
		sortByAnchorY(twice);
		ASSERT_EQ(once.size(), twice.size());
		for (size_t i = 0; i < once.size(); ++i) {
			EXPECT_EQ(once[i].entity, twice[i].entity);
			EXPECT_FLOAT_EQ(once[i].anchorY, twice[i].anchorY);
		}
	}

} // namespace engine::world

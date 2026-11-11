// Unit tests for the GL-free core of world-space 2.5D depth sorting: the
// canonical anchorY derivation (bottom-most ground-contact world-Y) and the
// stable ascending merge/sort. These are the pure pieces the render paths share;
// the gather + emit steps need a GL context and are exercised in-game.

#include "assets/placement/WorldDepthSort.h"

#include "assets/placement/FloraRouting.h"
#include "assets/placement/RenderContext.h"

#include <vector/Types.h>

#include <gtest/gtest.h>

#include <vector>

namespace assets {
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

	// Tall or short routing is one answer per def, from its mature variant 0 at its
	// unscaled height (FloraRouting.h). These pin the rule the bake and the sort share.
	TEST(WorldDepthSort, RoutesTallFromMatureHeight) {
		const renderer::TessellatedMesh tree = makeMesh(-3.0F, 0.5F); // 3.5 m
		const MeshYExtent				treeExt = meshYExtent(&tree);
		EXPECT_TRUE(routesTall(treeExt.maxY - treeExt.minY, /*isGroundcover=*/false));

		const renderer::TessellatedMesh flora = makeMesh(-0.3F, 0.1F); // 0.4 m
		const MeshYExtent				floraExt = meshYExtent(&flora);
		EXPECT_FALSE(routesTall(floraExt.maxY - floraExt.minY, /*isGroundcover=*/false));
	}

	// Exactly at the threshold counts as tall.
	TEST(WorldDepthSort, RoutesTallThresholdIsInclusive) {
		EXPECT_TRUE(routesTall(kShortFloraMaxHeight, /*isGroundcover=*/false));
	}

	// Groundcover is never a tall occluder even when its mesh clears the threshold:
	// it renders on the instanced path, so it must stay out of the sorted stream.
	TEST(WorldDepthSort, RoutesTallNeverForGroundcover) {
		EXPECT_FALSE(routesTall(3.5F, /*isGroundcover=*/true));
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

	// The gather's padding: the largest template reach, never under the 2 m every path had before
	// reach-based margins, so a wide short template still gathers with no tall one loaded.
	TEST(WorldDepthSort, VisibleBoundsPadByAtLeastTwoMetersAndByAWiderReach) {
		const world::WorldCamera camera;
		const VisibleBounds		 base  = computeVisibleBounds(camera, 800, 600, 16.0F, 0.0F);
		const auto				 padOf = [&](float reachMeters) {
			 const VisibleBounds vis = computeVisibleBounds(camera, 800, 600, 16.0F, entityCullMarginMeters(reachMeters));
			 EXPECT_FLOAT_EQ(base.minX - vis.minX, vis.maxX - base.maxX);
			 return base.minX - vis.minX;
		};
		EXPECT_FLOAT_EQ(padOf(0.0F), 2.0F);
		EXPECT_FLOAT_EQ(padOf(1.5F), 2.0F);
		EXPECT_FLOAT_EQ(padOf(6.5F), 6.5F);
	}

} // namespace assets

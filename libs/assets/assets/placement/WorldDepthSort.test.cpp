// Unit tests for the GL-free core of world-space 2.5D depth sorting: the
// canonical anchorY derivation (bottom-most ground-contact world-Y) and the
// gather's stable ascending merge; the emit step needs a GL context and is
// exercised in-game.

#include "assets/placement/WorldDepthSort.h"

#include "assets/AssetDefinition.h"
#include "assets/AssetRegistry.h"

#include "assets/placement/FloraRouting.h"
#include "assets/placement/RenderContext.h"

#include <vector/Types.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
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

	// A def with a footprint anchors at the footprint's lower edge for the instance's age class (a
	// tree's trunk foot), not at the lowest thing it draws (contact shadow, grass); without one, at the
	// mesh's bottom.
	TEST(WorldDepthSort, AnchorFollowsFootprintLowerEdge) {
		AssetDefinition tree;
		tree.ageClasses = {{.name = "sapling", .weight = 0.5F, .variants = 1}, {.name = "mature", .weight = 0.5F, .variants = 1}};
		tree.setCollision(0, {.type = CollisionShapeType::Rect, .offsetMeters = {0.0F, 0.0F}, .halfExtentsMeters = {0.04F, 0.04F}});
		tree.setCollision(1, {.type = CollisionShapeType::Rect, .offsetMeters = {0.0F, 0.05F}, .halfExtentsMeters = {0.1F, 0.1F}});
		const MeshYExtent mesh{.minY = -3.0F, .maxY = 0.6F};
		EXPECT_FLOAT_EQ(anchorLocalY(&tree, 0, mesh), 0.04F);
		EXPECT_FLOAT_EQ(anchorLocalY(&tree, 1, mesh), 0.15F);
		EXPECT_FLOAT_EQ(computeAnchorY(10.0F, anchorLocalY(&tree, 1, mesh), 2.0F), 10.3F);

		const AssetDefinition bush;
		EXPECT_FLOAT_EQ(anchorLocalY(&bush, 0, mesh), 0.6F);
		EXPECT_FLOAT_EQ(anchorLocalY(nullptr, 0, mesh), 0.6F);
	}

	// A simple (SVG) def's metadata collider is authored about the drawing's bbox centre, and its
	// mesh is drawn from the SVG's origin; the anchor is the collider's lower edge where the mesh
	// draws it. The drawing spans y 10 to 100 at 1.5 m, so 1/60 m a unit, and the collider's lower
	// edge is y 70: 70/60 m below the origin, not the 0.25 m below the bbox centre it is authored at.
	TEST(WorldDepthSort, SimpleDefAnchorsAtItsMetadataColliderInTheMeshFrame) {
		namespace fs = std::filesystem;
		const fs::path root = fs::temp_directory_path() / "world_depth_sort_simple_anchor";
		fs::remove_all(root);
		fs::create_directories(root / "Boulder");
		std::ofstream(root / "Boulder" / "boulder.svg")
			<< "<?xml version=\"1.0\"?><svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 200 200\" width=\"200\" height=\"200\">"
			   "<metadata><collision><shape type=\"aabb\" min=\"10,40\" max=\"50,70\"/></collision></metadata>"
			   "<path d=\"M0,10 L60,10 L60,100 L0,100 Z\" fill=\"#808080\"/></svg>";
		std::ofstream(root / "Boulder" / "Boulder.xml")
			<< "<?xml version=\"1.0\"?><AssetDefinitions><AssetDef><defName>Boulder</defName><assetType>simple</assetType>"
			   "<svgPath>boulder.svg</svgPath><worldHeight>1.5</worldHeight></AssetDef></AssetDefinitions>";

		AssetRegistry& registry = AssetRegistry::Get();
		registry.clearDefinitions();
		registry.loadDefinitionsFromFolder(root.string());
		const AssetDefinition* def = registry.getDefinition("Boulder");
		ASSERT_NE(def, nullptr);
		ASSERT_EQ(def->collisionFor(0).type, CollisionShapeType::Rect);
		const MeshYExtent mesh = meshYExtent(registry.getTemplate("Boulder", {}));
		EXPECT_NEAR(mesh.maxY, 100.0F / 60.0F, 1e-5F);
		EXPECT_NEAR(anchorLocalY(def, 0, mesh), 70.0F / 60.0F, 1e-5F);

		registry.clearDefinitions();
		fs::remove_all(root);
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


	// The gather keeps each chunk's statics sorted between frames, culls the stretch the view's rows
	// reach, and merges them; its stream must be the one a stable sort of every chunk's query, in
	// (y, x) chunk order, then the dynamic entities, gives, ties between chunks and with actors
	// included, and must follow an edit, a chunk unloading and reloading, and the camera moving.
	TEST(WorldDepthSort, GatherMatchesOneStableSortAcrossChunksAndEdits) {
		PlacementExecutor executor(AssetRegistry::Get());
		std::unordered_set<world::ChunkCoordinate> processed;
		const auto addChunk = [&](world::ChunkCoordinate coord, const std::vector<std::pair<glm::vec2, float>>& statics) {
			SpatialIndex index;
			for (const auto& [position, anchorY] : statics) {
				PlacedEntity entity;
				entity.defName = "Tree";
				entity.position = position;
				entity.anchorY = anchorY;
				entity.isTallOccluder = true;
				index.insert(entity);
			}
			PlacedEntity shortFlora;
			shortFlora.defName = "Bush";
			shortFlora.position = statics.front().first;
			index.insert(shortFlora);
			AsyncChunkPlacementResult result;
			result.coord = coord;
			result.spatialIndex = std::move(index);
			executor.storeChunkResult(std::move(result));
			processed.insert(coord);
		};
		// Both sides of the origin, so the view spans four chunks; anchorY 3 and 5 tie across chunks. The
		// static at (5, 8) has its anchorY far below the view's rows, which the cull must still reach.
		addChunk({0, 0}, {{{4.0F, 3.0F}, 3.0F}, {{9.0F, 1.0F}, 5.0F}, {{1.0F, 8.0F}, 8.0F}, {{2.0F, 2.0F}, 1.0F}, {{5.0F, 8.0F}, 45.0F}});
		addChunk({-1, 0}, {{{-4.0F, 3.0F}, 3.0F}, {{-2.0F, 6.0F}, 6.0F}, {{-900.0F, 3.0F}, 2.0F}});
		addChunk({0, -1}, {{{3.0F, -5.0F}, -4.0F}, {{6.0F, -1.0F}, 5.0F}});
		addChunk({-1, -1}, {{{-3.0F, -3.0F}, 3.0F}});

		std::vector<PlacedEntity> actors(2);
		actors[0].position = {0.5F, 2.5F};
		actors[0].anchorY = 3.0F;
		actors[1].position = {-1.0F, -1.0F};
		actors[1].anchorY = -1.0F;

		world::WorldCamera camera;
		camera.setPosition({0.0F, 0.0F});
		const RenderContext ctx{executor, processed, &actors, camera, 800, 600, 16.0F, 1};

		const auto expected = [&]() {
			const VisibleBounds vis = computeVisibleBounds(camera, 800, 600, 16.0F, entityCullMarginMeters(AssetRegistry::Get().maxReachMeters()));
			std::vector<world::ChunkCoordinate> order(processed.begin(), processed.end());
			std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
			std::vector<std::pair<float, const PlacedEntity*>> entities;
			for (const auto& coord : order) {
				for (const PlacedEntity* entity : executor.getChunkIndex(coord)->queryRect(vis.minX, vis.minY, vis.maxX, vis.maxY)) {
					if (entity->isTallOccluder) {
						entities.emplace_back(entity->anchorY, entity);
					}
				}
			}
			for (const PlacedEntity& actor : actors) {
				if (actor.position.x >= vis.minX && actor.position.x <= vis.maxX && actor.position.y >= vis.minY && actor.position.y <= vis.maxY) {
					entities.emplace_back(actor.anchorY, &actor);
				}
			}
			std::stable_sort(entities.begin(), entities.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
			std::vector<const PlacedEntity*> sorted;
			for (const auto& entry : entities) {
				sorted.push_back(entry.second);
			}
			return sorted;
		};
		// Each item's upright carries its entity's draw fields.
		const auto entitiesOf = [](const std::vector<DepthSortItem>& items) {
			std::vector<const PlacedEntity*> entities;
			for (const DepthSortItem& item : items) {
				const Upright& upright = *item.upright;
				EXPECT_EQ(item.anchorY, upright.entity->anchorY);
				EXPECT_EQ(upright.position, upright.entity->position);
				EXPECT_EQ(upright.templateId, upright.entity->templateId);
				entities.push_back(upright.entity);
			}
			return entities;
		};

		WorldDepthGather gather;
		std::vector<DepthSortItem> out;
		gather.gather(ctx, out);
		EXPECT_EQ(out.size(), 12U); // the far static at x -900 is culled
		EXPECT_EQ(entitiesOf(out), expected());

		ASSERT_TRUE(executor.removeEntity({0, 0}, {9.0F, 1.0F}, "Tree"));
		gather.gather(ctx, out);
		EXPECT_EQ(out.size(), 11U);
		EXPECT_EQ(entitiesOf(out), expected());

		// A chunk that unloads leaves the stream, and comes back with the index it reloads with.
		processed.erase({-1, 0});
		gather.gather(ctx, out);
		EXPECT_EQ(out.size(), 9U);
		EXPECT_EQ(entitiesOf(out), expected());
		addChunk({-1, 0}, {{{-4.0F, 3.0F}, 7.0F}});
		gather.gather(ctx, out);
		EXPECT_EQ(out.size(), 10U);
		EXPECT_EQ(entitiesOf(out), expected());

		// Moving the camera culls the same statics differently, then changes which chunks reach the view.
		camera.setPosition({10.0F, 0.0F});
		gather.gather(ctx, out);
		EXPECT_EQ(entitiesOf(out), expected());
		camera.setPosition({-890.0F, 0.0F});
		processed.erase({-1, 0});
		addChunk({-1, 0}, {{{-4.0F, 3.0F}, 7.0F}, {{-900.0F, 3.0F}, 2.0F}});
		gather.gather(ctx, out);
		EXPECT_EQ(out.size(), 1U);
		EXPECT_EQ(entitiesOf(out), expected());
		camera.setPosition({0.0F, 0.0F});
		gather.gather(ctx, out);
		EXPECT_EQ(entitiesOf(out), expected());
	}

} // namespace assets

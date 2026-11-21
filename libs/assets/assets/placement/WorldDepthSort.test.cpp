// Unit tests for the GL-free core of world-space 2.5D depth sorting: the
// canonical anchorY derivation (bottom-most ground-contact world-Y) and the
// gather's stable ascending merge; the emit step needs a GL context and is
// exercised in-game.

#include "assets/placement/WorldDepthSort.h"

#include "assets/AssetDefinition.h"
#include "assets/AssetRegistry.h"
#include "assets/ItemView.h"
#include "assets/PartsAsset.test.h"

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
		EXPECT_FLOAT_EQ(anchorLocalY(&tree, 0, mesh, 0.0F), 0.04F);
		EXPECT_FLOAT_EQ(anchorLocalY(&tree, 1, mesh, 0.0F), 0.15F);
		EXPECT_FLOAT_EQ(computeAnchorY(10.0F, anchorLocalY(&tree, 1, mesh, 0.0F), 2.0F), 10.3F);

		const AssetDefinition bush;
		EXPECT_FLOAT_EQ(anchorLocalY(&bush, 0, mesh, 0.0F), 0.6F);
		EXPECT_FLOAT_EQ(anchorLocalY(nullptr, 0, mesh, 0.0F), 0.6F);
	}

	// A simple (SVG) def's mesh and metadata collider are both about its pivot, the drawing's bbox
	// centre. The drawing spans x 0 to 60, y 10 to 100 at 1.5 m, so 1/60 m a unit about (30, 55); the
	// collider, x 10 to 50 and y 40 to 70, has its lower edge 15/60 m below the pivot, and turned a
	// quarter its lower edge is its 20/60 m half width.
	TEST(WorldDepthSort, SimpleDefAnchorsAtItsMetadataColliderAboutThePivot) {
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
		EXPECT_NEAR(mesh.maxY, 45.0F / 60.0F, 1e-5F);
		EXPECT_NEAR(anchorLocalY(def, 0, mesh, 0.0F), 15.0F / 60.0F, 1e-5F);
		EXPECT_NEAR(anchorLocalY(def, 0, mesh, 3.14159265F / 2.0F), 20.0F / 60.0F, 1e-5F);

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
		const DrawBound			everywhere = DrawBound::everywhere();
		const RenderContext ctx{executor, processed, &actors, camera, 800, 600, 16.0F, 1, everywhere};

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

	// Under the cover only uprights within reach of what the bound holds are gathered, statics and
	// actors alike, in the order the whole view gives them; an empty bound gathers nothing.
	TEST(WorldDepthSort, GatherCullsToTheDrawBound) {
		PlacementExecutor executor(AssetRegistry::Get());
		SpatialIndex	  index;
		for (const auto& [position, anchorY] : std::vector<std::pair<glm::vec2, float>>{{{2.0F, 2.0F}, 2.0F}, {{1.0F, 4.0F}, 4.0F}, {{20.0F, 2.0F}, 1.0F}}) {
			PlacedEntity entity;
			entity.defName = "Tree";
			entity.position = position;
			entity.anchorY = anchorY;
			entity.isTallOccluder = true;
			index.insert(entity);
		}
		AsyncChunkPlacementResult result;
		result.coord = {0, 0};
		result.spatialIndex = std::move(index);
		executor.storeChunkResult(std::move(result));
		const std::unordered_set<world::ChunkCoordinate> processed{{0, 0}};

		std::vector<PlacedEntity> actors(2);
		actors[0].position = {3.0F, 3.0F};
		actors[0].anchorY = 3.0F;
		actors[1].position = {-20.0F, 3.0F};
		actors[1].anchorY = 3.0F;

		world::WorldCamera camera;
		camera.setPosition({0.0F, 0.0F});
		const float reach = entityCullMarginMeters(AssetRegistry::Get().maxReachMeters());
		DrawBound	seen;
		seen.add({0.0F, 5.0F, 0.0F, 5.0F});
		const RenderContext ctx{executor, processed, &actors, camera, 8000, 6000, 16.0F, 1, seen};
		WorldDepthGather	gather;
		std::vector<DepthSortItem> out;
		gather.gather(ctx, out);
		std::vector<glm::vec2> positions;
		for (const DepthSortItem& item : out) {
			positions.push_back(item.upright->position);
		}
		// The tree at x 20 and the actor at x -20 lie past the box's reach.
		ASSERT_LT(reach, 10.0F);
		EXPECT_EQ(positions, (std::vector<glm::vec2>{{2.0F, 2.0F}, {3.0F, 3.0F}, {1.0F, 4.0F}}));

		// Just inside the reach still gathers.
		DrawBound near;
		near.add({20.0F - reach + 0.01F, 30.0F, 0.0F, 5.0F});
		const RenderContext nearCtx{executor, processed, &actors, camera, 8000, 6000, 16.0F, 1, near};
		gather.gather(nearCtx, out);
		ASSERT_EQ(out.size(), 1U);
		EXPECT_EQ(out.front().upright->position, glm::vec2(20.0F, 2.0F));

		// The view alone holds all five, so the culls above are the bound's.
		const DrawBound		everywhere = DrawBound::everywhere();
		const RenderContext wholeCtx{executor, processed, &actors, camera, 8000, 6000, 16.0F, 1, everywhere};
		gather.gather(wholeCtx, out);
		EXPECT_EQ(out.size(), 5U);

		const DrawBound		nothing;
		const RenderContext emptyCtx{executor, processed, &actors, camera, 8000, 6000, 16.0F, 1, nothing};
		gather.gather(emptyCtx, out);
		EXPECT_TRUE(out.empty());
	}

	// ─── Parts items (rotatable-placed-items 8 and 6.9) ──────────────────────────────────────────────────

	namespace {

		// A 3 m bench with a 2.2 m chimney against its east end: its reach is the chimney's top, 2.59 m, past the
		// 2 m least margin and past the turn-0 template's own reach.
		class PartsGatherTest : public parts_fixtures::PartsTest {
		  protected:
			void SetUp() override {
				PartsTest::SetUp();
				using parts_fixtures::block;
				using parts_fixtures::place;
				writeDef(
					"Forge", "<parts>" + block("bench", -1.5F, -0.5F, 1.5F, 0.5F, 0.0F, 0.8F) + block("chimney", 1.5F, -0.15F, 1.8F, 0.15F, 0.0F, 2.2F) +
								 "</parts><placements>" + place("bench") + place("chimney") + "</placements>"
				);
				ASSERT_NE(load("Forge"), nullptr);
				forge = AssetRegistry::Get().templateId("Forge", {});
				ASSERT_NE(forge, 0U);
			}

			// One record a strip, as DynamicEntityRenderSystem makes them.
			void addItem(glm::vec2 position, double degrees) {
				const ItemLayout* layout = AssetRegistry::Get().stripLayout(forge, turnQuantaFromDegrees(degrees));
				ASSERT_NE(layout, nullptr);
				for (uint32_t strip = 0; strip < layout->layout.strips.size(); ++strip) {
					PlacedEntity record;
					record.templateId = forge;
					record.position = position;
					record.anchorY = computeAnchorY(position.y, layout->layout.strips[strip].anchorLocalY, 1.0F);
					record.itemLayout = layout;
					record.strip = strip;
					actors.push_back(record);
				}
			}

			uint32_t gathered() {
				PlacementExecutor								executor(AssetRegistry::Get());
				const std::unordered_set<world::ChunkCoordinate> processed;
				const RenderContext ctx{executor, processed, &actors, camera, 800, 600, 16.0F, 1, DrawBound::everywhere()};
				std::vector<DepthSortItem> out;
				gather.gather(ctx, out);
				EXPECT_EQ(gather.stripUprightCount(), out.size()) << "every upright here is a strip";
				return gather.stripUprightCount();
			}

			uint32_t				  forge = 0;
			std::vector<PlacedEntity> actors;
			world::WorldCamera		  camera;
			WorldDepthGather		  gather;
		};

	} // namespace

	// At 270 degrees the chimney stands at the north end and its top reaches 2.59 m north of the pivot: an item 2.5 m
	// below the view's bottom edge still draws into the view, and is gathered.
	TEST_F(PartsGatherTest, ATurnedItemJustOutsideTheViewIsGathered) {
		camera.setPosition({0.0F, 0.0F});
		const VisibleBounds view = computeVisibleBounds(camera, 800, 600, 16.0F, 0.0F);
		const float			reach = AssetRegistry::Get().maxReachMeters();
		ASSERT_GT(reach, 2.5F);
		addItem({0.0F, view.maxY + 2.5F}, 270.0);
		float north = 0.0F;
		for (const ItemStrip& strip : actors.front().itemLayout->layout.strips) {
			north = std::min(north, strip.minY);
		}
		ASSERT_LT(view.maxY + 2.5F + north, view.maxY) << "its chimney's top is on screen";
		EXPECT_EQ(gathered(), actors.size());
		actors.clear();
		addItem({0.0F, view.maxY + reach + 0.1F}, 270.0);
		EXPECT_EQ(gathered(), 0U) << "past the reach nothing of it can show";
	}

	// The planner is sized from the strips in the stream: kept layouts the view doesn't hold add nothing.
	TEST_F(PartsGatherTest, TheStripCountIsTheStreamsWhateverTheRegistryKeeps) {
		camera.setPosition({0.0F, 0.0F});
		addItem({0.0F, 0.0F}, 45.0);
		const uint32_t withOneLayout = gathered();
		EXPECT_EQ(withOneLayout, actors.size());
		for (uint32_t turn = 0; turn < 10000; ++turn) {
			ASSERT_NE(AssetRegistry::Get().stripLayout(forge, turn), nullptr);
		}
		EXPECT_EQ(gathered(), withOneLayout) << "10,000 kept layouts";
		actors.clear();
		EXPECT_EQ(gathered(), 0U) << "no item in view: the bound is the template slots', as before parts items";
	}

} // namespace assets

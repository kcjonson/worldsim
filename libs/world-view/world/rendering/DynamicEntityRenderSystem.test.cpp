#include "DynamicEntityRenderSystem.h"

#include <assets/ItemView.h>
#include <assets/PartsAsset.test.h>
#include <ecs/World.h>
#include <ecs/components/AgentRadius.h>
#include <ecs/components/Appearance.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Transform.h>
#include <ecs/systems/PlayerVisibilitySystem.h>
#include <ecs/systems/TimeSystem.h>
#include <ecs/systems/VisionSystem.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <numbers>

namespace {

	constexpr const char* kMoverDefName = "Test_RenderedMover";

	class MoverRenderTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			world.registerSystem<ecs::TimeSystem>();
			colonist = addEntity({0.0F, 0.0F}, "Test_RenderedColonist");
			world.addComponent<ecs::Colonist>(colonist, ecs::Colonist{"Tester"});
			ecs::Memory memory;
			memory.sightRadius = 15.0F;
			world.addComponent<ecs::Memory>(colonist, std::move(memory));
			world.addComponent<ecs::AgentRadius>(colonist, ecs::AgentRadius{});
			for (int tick = 0; tick < 12; ++tick) {
				world.update(0.016F);
			}
		}

		ecs::EntityID addEntity(glm::vec2 at, const char* defName) {
			const ecs::EntityID entity = world.createEntity();
			world.addComponent<ecs::Position>(entity, ecs::Position{at});
			world.addComponent<ecs::Rotation>(entity, ecs::Rotation{0.0F});
			world.addComponent<ecs::Appearance>(entity, ecs::Appearance{defName, 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
			return entity;
		}

		// A thing that moves by itself and is not a colonist.
		ecs::EntityID addMover(glm::vec2 at) {
			const ecs::EntityID mover = addEntity(at, kMoverDefName);
			world.addComponent<ecs::AgentRadius>(mover, ecs::AgentRadius{});
			return mover;
		}

		[[nodiscard]] std::size_t drawn(const char* defName) {
			world.update(0.016F);
			const auto& data = render.getRenderData();
			return static_cast<std::size_t>(std::count_if(data.begin(), data.end(), [defName](const assets::PlacedEntity& placed) {
				return placed.defName == defName;
			}));
		}

		ecs::World								world;
		ecs::VisionSystem&						vision = world.registerSystem<ecs::VisionSystem>();
		ecs::PlayerVisibilitySystem&			visibility = world.registerSystem<ecs::PlayerVisibilitySystem>();
		worldview::DynamicEntityRenderSystem&	render = world.registerSystem<worldview::DynamicEntityRenderSystem>();
		ecs::EntityID							colonist = ecs::kInvalidEntity;
	};

} // namespace

TEST_F(MoverRenderTest, MoverOutOfEveryColonistsSightIsAbsentFromTheRenderData) {
	addMover({100.0F, 0.0F});
	ASSERT_FALSE(visibility.isVisible({100.0F, 0.0F}));
	EXPECT_EQ(drawn(kMoverDefName), 0U);
}

TEST_F(MoverRenderTest, MoverInsideAColonistsSightIsPresent) {
	addMover({5.0F, 0.0F});
	ASSERT_TRUE(visibility.isVisible({5.0F, 0.0F}));
	EXPECT_EQ(drawn(kMoverDefName), 1U);
}

TEST_F(MoverRenderTest, ColonistsAndThingsThatDoNotMoveAlwaysDraw) {
	addEntity({100.0F, 0.0F}, "Test_RenderedStill");
	ASSERT_FALSE(visibility.isVisible({100.0F, 0.0F}));
	EXPECT_EQ(drawn("Test_RenderedStill"), 1U);
	EXPECT_EQ(drawn("Test_RenderedColonist"), 1U);
}

TEST_F(MoverRenderTest, RevealDrawsEveryMover) {
	addMover({100.0F, 0.0F});
	visibility.setRevealed(true);
	EXPECT_EQ(drawn(kMoverDefName), 1U);
}

// ─── Parts items in the sorted stream (rotatable-placed-items 8) ─────────────────────────────────────────

namespace {

	using namespace assets::parts_fixtures;

	class ItemStripRecordTest : public PartsTest {
	  protected:
		// A plain 3 m x 1 m bench, 0.8 m tall.
		void SetUp() override {
			PartsTest::SetUp();
			writeDef("Bench", "<parts>" + block("bench", -1.5F, -0.5F, 1.5F, 0.5F, 0.0F, 0.8F) + "</parts><placements>" + place("bench") + "</placements>");
			ASSERT_NE(load("Bench"), nullptr);
		}

		ecs::EntityID add(const char* defName, glm::vec2 at, float radians) {
			const ecs::EntityID entity = world.createEntity();
			world.addComponent<ecs::Position>(entity, ecs::Position{at});
			world.addComponent<ecs::Rotation>(entity, ecs::Rotation{radians});
			world.addComponent<ecs::Appearance>(entity, ecs::Appearance{defName, 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
			return entity;
		}

		const std::vector<assets::PlacedEntity>& records() {
			world.update(0.016F);
			return render.getRenderData();
		}

		ecs::World							  world;
		worldview::DynamicEntityRenderSystem& render = world.registerSystem<worldview::DynamicEntityRenderSystem>();
	};

} // namespace

TEST_F(ItemStripRecordTest, APartsItemIsOneRecordAStripAnchoredByItsLayout) {
	const glm::vec2 at{10.0F, 20.0F};
	add("Bench", at, std::numbers::pi_v<float> / 4.0F);
	const std::vector<assets::PlacedEntity>& data = records();
	assets::AssetRegistry&					 registry = assets::AssetRegistry::Get();
	const assets::ItemLayout* layout = registry.stripLayout(registry.templateId("Bench", {}), assets::turnQuantaFromDegrees(45.0));
	ASSERT_NE(layout, nullptr);
	ASSERT_EQ(layout->layout.strips.size(), 31U);
	ASSERT_EQ(data.size(), layout->layout.strips.size());
	for (uint32_t i = 0; i < data.size(); ++i) {
		const assets::PlacedEntity& record = data[i];
		EXPECT_EQ(record.itemLayout, layout);
		EXPECT_EQ(record.strip, i) << "in the layout's stored order";
		EXPECT_EQ(record.position, at);
		EXPECT_EQ(record.rotation, 0.0F) << "the turn is in the view";
		EXPECT_EQ(record.scale, 1.0F);
		EXPECT_FLOAT_EQ(record.anchorY, at.y + layout->layout.strips[i].anchorLocalY);
		if (i > 0) {
			EXPECT_GE(record.anchorY, data[i - 1].anchorY) << "ascending, so the stable sort keeps them in order";
		}
	}
}

TEST_F(ItemStripRecordTest, TheTurnComesFromTheEntitysRotation) {
	add("Bench", {0.0F, 0.0F}, -std::numbers::pi_v<float> / 2.0F);
	const std::vector<assets::PlacedEntity>& data = records();
	ASSERT_FALSE(data.empty());
	EXPECT_EQ(data.front().itemLayout->turnQuanta, 27000U) << "a quarter turn back is 270 degrees";
}

#ifdef DEVELOPMENT_BUILD
// Telling a parts item apart takes no name lookup: an entity that isn't one costs the lookups it always did, its
// template id (templateId) and its anchor (getDefinition), and a parts item costs its template id alone, however many
// strips it has.
TEST_F(ItemStripRecordTest, TellingAPartsItemApartTakesNoNameLookup) {
	folder.write("Stone/stone.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 10\"><rect x=\"0\" y=\"0\" width=\"10\" height=\"10\" fill=\"#808080\"/></svg>");
	folder.write(
		"Stone/Stone.xml", "<?xml version=\"1.0\"?><AssetDefinitions><AssetDef><defName>Stone</defName><assetType>simple</assetType>"
						   "<svgPath>stone.svg</svgPath><worldHeight>0.5</worldHeight></AssetDef></AssetDefinitions>"
	);
	ASSERT_NE(load("Stone"), nullptr);
	assets::AssetRegistry& registry = assets::AssetRegistry::Get();
	struct Counting {
		assets::AssetRegistry& registry;
		explicit Counting(assets::AssetRegistry& counted) : registry(counted) { registry.countNameLookups(true); }
		~Counting() { registry.countNameLookups(false); }
		Counting(const Counting&) = delete;
		Counting& operator=(const Counting&) = delete;
	} counting{registry};
	const auto lookupsOfAFrame = [&]() {
		const uint64_t before = registry.nameLookups();
		(void)records();
		return registry.nameLookups() - before;
	};

	(void)registry.templateId("Stone", {});
	uint64_t before = registry.nameLookups();
	(void)registry.templateId("Stone", {});
	(void)registry.getDefinition("Stone");
	const uint64_t mainPerEntity = registry.nameLookups() - before;
	before = registry.nameLookups();
	(void)registry.templateId("Bench", {});
	const uint64_t templateIdAlone = registry.nameLookups() - before;

	add("Stone", {0.0F, 0.0F}, 0.0F);
	const uint64_t oneStone = lookupsOfAFrame();
	add("Stone", {5.0F, 0.0F}, 0.0F);
	EXPECT_EQ(lookupsOfAFrame() - oneStone, mainPerEntity) << "a stone looks names up as it did before parts items";

	const uint64_t stones = lookupsOfAFrame();
	add("Bench", {20.0F, 0.0F}, std::numbers::pi_v<float> / 4.0F);
	EXPECT_EQ(lookupsOfAFrame() - stones, templateIdAlone) << "31 strips, one template id";
	for (const assets::PlacedEntity& record : render.getRenderData()) {
		if (record.itemLayout != nullptr) {
			EXPECT_TRUE(record.defName.empty()) << "a strip carries its template id and layout, not a name";
		}
	}
}
#endif

// Everything that isn't a parts def is one record as before: rotation forced to 0, no layout.
TEST_F(ItemStripRecordTest, AnEntityThatIsNotAPartsItemIsUntouched) {
	add("Test_RenderedStill", {3.0F, 4.0F}, 1.0F);
	const std::vector<assets::PlacedEntity>& data = records();
	ASSERT_EQ(data.size(), 1U);
	EXPECT_EQ(data.front().itemLayout, nullptr);
	EXPECT_EQ(data.front().strip, 0U);
	EXPECT_EQ(data.front().rotation, 0.0F);
	EXPECT_EQ(data.front().position, glm::vec2(3.0F, 4.0F));
	EXPECT_EQ(data.front().anchorY, 4.0F) << "no template: the anchor is the position";
}

// The draw path for parts items (rotatable-placed-items 9.1, 9.2): depth strips placed through the RunPlanner with
// their layout's orders reserved, merged into ranged draws at each close. The stream here is what emitSorted walks:
// strips of real layouts, and colonists, which close every open run where they sort (flushRuns).

#include "world/rendering/RunPlanner.h"
#include "world/rendering/StripRuns.h"

#include <assets/ItemView.h>
#include <assets/PartsAsset.test.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <vector>

namespace {

	using namespace assets::parts_fixtures;

	constexpr float kPixelsPerMeter = 32.0F;
	constexpr int	kViewportWidth = 4000;
	constexpr int	kViewportHeight = 3000;

	struct Run {
		uint32_t							view = worldview::kNotAView;
		uint32_t							strip = 0;
		std::vector<Renderer::InstanceData> instances;
	};

	struct Item {
		const assets::ItemLayout* layout = nullptr;
		glm::vec2				  position{0.0F, 0.0F};
	};

	// An upright of the stream: an item's strip, or a colonist (no item).
	struct Entry {
		float		anchorY = 0.0F;
		const Item* item = nullptr;
		uint32_t	strip = 0;
	};

	// One draw: `strips` consecutive strips of a view from `firstStrip`, over `instances` items.
	struct ItemDraw {
		uint32_t view = 0;
		uint32_t firstStrip = 0;
		uint32_t strips = 0;
		size_t	 instances = 0;
	};

	std::vector<Entry> stripsOf(const Item& item) {
		std::vector<Entry> entries;
		for (uint32_t strip = 0; strip < item.layout->layout.strips.size(); ++strip) {
			entries.push_back({.anchorY = item.position.y + item.layout->layout.strips[strip].anchorLocalY, .item = &item, .strip = strip});
		}
		return entries;
	}

	// Walks a stream as emitSorted does and returns the item draws, in the order drawn.
	std::vector<ItemDraw> drawStream(std::vector<Entry> entries) {
		std::stable_sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) { return left.anchorY < right.anchorY; });
		worldview::RunPlanner planner;
		planner.beginFrame(kViewportWidth, kViewportHeight, assets::AssetRegistry::Get().stripLayoutStripCount());
		std::vector<Run>			   runs;
		uint32_t					   runCount = 0;
		std::vector<worldview::RunSpan> spans;
		std::vector<ItemDraw>		   draws;
		std::set<uint32_t>			   settled;

		const auto close = [&]() {
			const std::span<const uint32_t> closing = planner.close();
			worldview::spanRuns(planner, closing, runs, spans);
			for (const worldview::RunSpan& span : spans) {
				const Run& run = runs[closing[span.first]];
				draws.push_back({.view = run.view, .firstStrip = run.strip, .strips = span.count, .instances = run.instances.size()});
				for (uint32_t i = 0; i < span.count; ++i) {
					runs[closing[span.first + i]].instances.clear();
				}
			}
		};

		for (const Entry& entry : entries) {
			if (entry.item == nullptr) {
				close();
				continue;
			}
			const assets::ItemLayout& layout = *entry.item->layout;
			if (settled.insert(layout.id).second) {
				planner.reserveOrders(layout.firstStrip, static_cast<uint32_t>(layout.layout.strips.size()));
			}
			const assets::ItemStrip& box = layout.layout.strips[entry.strip];
			const Foundation::Vec2	 position{entry.item->position.x, entry.item->position.y};
			const Foundation::Vec2	 pixelMin = (position + Foundation::Vec2{box.minX, box.minY}) * kPixelsPerMeter;
			const Foundation::Vec2	 pixelMax = (position + Foundation::Vec2{box.maxX, box.maxY}) * kPixelsPerMeter;
			const uint32_t			 r = planner.place(layout.firstStrip + entry.strip, planner.cellsOf(pixelMin, pixelMax), false);
			if (r == runCount) {
				if (runCount == runs.size()) {
					runs.emplace_back();
				}
				runs[runCount].view = layout.id;
				runs[runCount].strip = entry.strip;
				++runCount;
			}
			runs[r].instances.emplace_back(position, 0.0F, 1.0F, Foundation::Vec4{1.0F, 1.0F, 1.0F, 1.0F});
		}
		close();
		return draws;
	}

	class ItemStripDrawTest : public PartsTest {
	  protected:
		void SetUp() override {
			PartsTest::SetUp();
			writeDef("Bench", "<parts>" + block("bench", -1.5F, -0.5F, 1.5F, 0.5F, 0.0F, 0.8F) + "</parts><placements>" + place("bench") + "</placements>");
			ASSERT_NE(load("Bench"), nullptr);
			bench = assets::AssetRegistry::Get().templateId("Bench", {});
			ASSERT_NE(bench, 0U);
		}

		const assets::ItemLayout* layoutAt(double degrees) const {
			return assets::AssetRegistry::Get().stripLayout(bench, assets::turnQuantaFromDegrees(degrees));
		}

		uint32_t bench = 0;
	};

} // namespace

TEST_F(ItemStripDrawTest, OneItemOverlappingNothingIsOneDrawAtEveryTurn) {
	for (int degrees = 0; degrees < 360; degrees += 5) {
		const Item					item{.layout = layoutAt(degrees), .position = {40.0F, 40.0F}};
		const std::vector<ItemDraw> draws = drawStream(stripsOf(item));
		ASSERT_EQ(draws.size(), 1U) << degrees << " degrees, " << item.layout->layout.strips.size() << " strips";
		EXPECT_EQ(draws.front().firstStrip, 0U);
		EXPECT_EQ(draws.front().strips, item.layout->layout.strips.size()) << "every strip in one ranged draw";
	}
	const Item westward{.layout = layoutAt(315.0), .position = {40.0F, 40.0F}};
	EXPECT_EQ(westward.layout->layout.strips.size(), 31U);
	EXPECT_EQ(drawStream(stripsOf(westward)).size(), 1U) << "anchors falling to the right of the southern corner";
}

TEST_F(ItemStripDrawTest, TenItemsOfOneDefAtOneTurnAreOneDraw) {
	std::vector<Item> items;
	for (int i = 0; i < 10; ++i) {
		items.push_back({.layout = layoutAt(45.0), .position = {10.0F + (5.0F * static_cast<float>(i)), 40.0F + (0.3F * static_cast<float>(i % 3))}});
	}
	std::vector<Entry> entries;
	for (const Item& item : items) {
		const std::vector<Entry> strips = stripsOf(item);
		entries.insert(entries.end(), strips.begin(), strips.end());
	}
	const std::vector<ItemDraw> draws = drawStream(entries);
	ASSERT_EQ(draws.size(), 1U);
	EXPECT_EQ(draws.front().instances, 10U);
	EXPECT_EQ(draws.front().strips, 31U);
}

// A colonist closes the open runs where he sorts: the item's strips that sort before him draw in one ranged draw, the
// rest in another, wherever he stands, and nowhere else.
TEST_F(ItemStripDrawTest, AColonistAmongTheStripsSplitsTheItemWhereHeSortsAndNowhereElse) {
	const Item				 item{.layout = layoutAt(45.0), .position = {40.0F, 40.0F}};
	const std::vector<Entry> strips = stripsOf(item);
	ASSERT_EQ(strips.size(), 31U);
	for (const uint32_t before : {1U, 10U, 15U, 25U}) {
		ASSERT_LT(strips[before - 1].anchorY, strips[before].anchorY) << "room between them for him";
		const float		   between = (strips[before - 1].anchorY + strips[before].anchorY) / 2.0F;
		std::vector<Entry> entries = strips;
		entries.push_back({.anchorY = between});
		const std::vector<ItemDraw> draws = drawStream(entries);
		ASSERT_EQ(draws.size(), 2U) << "colonist after strip " << before - 1;
		EXPECT_EQ(draws[0].firstStrip, 0U);
		EXPECT_EQ(draws[0].strips, before) << "everything behind him, before him";
		EXPECT_EQ(draws[1].firstStrip, before);
		EXPECT_EQ(draws[1].strips, 31U - before);
	}
	std::vector<Entry> behind = strips;
	behind.push_back({.anchorY = strips.front().anchorY - 1.0F});
	EXPECT_EQ(drawStream(behind).size(), 1U) << "a colonist north of the whole item";
	std::vector<Entry> inFront = strips;
	inFront.push_back({.anchorY = strips.back().anchorY + 1.0F});
	EXPECT_EQ(drawStream(inFront).size(), 1U) << "a colonist south of the whole item";
}

// Two items whose drawings overlap draw in depth order: a stool-sized box in front of the bench's high end opens its own
// draw after the bench's strips behind it.
TEST_F(ItemStripDrawTest, AnItemInFrontOfAnotherDrawsAfterTheStripsBehindIt) {
	writeDef("Box", "<parts>" + block("box", -0.25F, -0.25F, 0.25F, 0.25F, 0.0F, 0.5F) + "</parts><placements>" + place("box") + "</placements>");
	ASSERT_NE(load("Box"), nullptr);
	assets::AssetRegistry& registry = assets::AssetRegistry::Get();
	bench = registry.templateId("Bench", {});
	const Item benchItem{.layout = layoutAt(45.0), .position = {40.0F, 40.0F}};
	// At 45 degrees the bench's high end is its left end, to the north-west; its front edge runs y = x + 0.707 from
	// the pivot, so at x = -1.06 it is at y = -0.35, and the box stands 0.7 m south of that.
	const Item boxItem{.layout = registry.stripLayout(registry.templateId("Box", {}), 0), .position = {38.94F, 40.35F}};
	std::vector<Entry> entries = stripsOf(benchItem);
	const std::vector<Entry> box = stripsOf(boxItem);
	entries.insert(entries.end(), box.begin(), box.end());
	const std::vector<ItemDraw> draws = drawStream(entries);
	uint32_t benchStripsBeforeBox = 0;
	bool	 sawBox = false;
	for (const ItemDraw& draw : draws) {
		if (draw.view == boxItem.layout->id) {
			sawBox = true;
		} else if (!sawBox) {
			benchStripsBeforeBox += draw.strips;
		}
	}
	ASSERT_TRUE(sawBox);
	uint32_t behindBox = 0;
	for (const Entry& strip : stripsOf(benchItem)) {
		behindBox += strip.anchorY < box.front().anchorY ? 1U : 0U;
	}
	EXPECT_GE(benchStripsBeforeBox, behindBox) << "every bench strip behind the box draws before it";
	EXPECT_LE(draws.size(), 3U) << "the bench splits at most once round the box";
}

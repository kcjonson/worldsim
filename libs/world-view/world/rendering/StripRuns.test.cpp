// The draw path for parts items (rotatable-placed-items 9.1, 9.2, 10, 15): depth strips placed through the RunPlanner
// with their layout's orders reserved, merged into ranged draws at each close, their views released when nothing wants
// them, and a selected item's outline drawn in pieces at its strips. The stream here is what emitSorted walks: strips
// of real layouts, and colonists, which close every open run where they sort (flushRuns).

#include "world/rendering/ItemViewIndex.h"
#include "world/rendering/RunPlanner.h"
#include "world/rendering/SelectionOutline.h"
#include "world/rendering/StripRuns.h"

#include <assets/ItemView.h>
#include <assets/PartsAsset.test.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <set>
#include <vector>

namespace {

	using namespace assets::parts_fixtures;

	constexpr float kPixelsPerMeter = 32.0F;
	constexpr int	kViewportWidth = 4000;
	constexpr int	kViewportHeight = 3000;

	// Template meshes take planner ids below this, as template slots do in emitSorted.
	constexpr uint32_t kTemplateSlots = 16;

	struct Run {
		uint32_t							view = worldview::kNotAView;
		uint32_t							layout = 0;
		uint32_t							strip = 0;
		bool								itemsApart = false;
		std::vector<Renderer::InstanceData> instances;
	};

	struct Item {
		const assets::ItemLayout* layout = nullptr;
		glm::vec2				  position{0.0F, 0.0F};
	};

	// An upright of the stream: an item's strip, a template entity (a tree: its mesh id and a 1 m box at
	// `position`), or a colonist walking (neither: an animated entity, which closes the open runs).
	struct Entry {
		float		anchorY = 0.0F;
		const Item* item = nullptr;
		uint32_t	strip = 0;
		int			templateMesh = -1;
		glm::vec2	position{0.0F, 0.0F};
	};

	// One draw: `strips` consecutive strips of a view from `firstStrip`, over `instances` items, or a template run
	// (view kNotAView, firstStrip its mesh).
	struct ItemDraw {
		uint32_t view = 0;
		uint32_t firstStrip = 0;
		uint32_t strips = 0;
		size_t	 instances = 0;
		uint32_t planner = 0; // the planner's draw index of its first run
	};

	std::vector<Entry> stripsOf(const Item& item) {
		std::vector<Entry> entries;
		for (uint32_t strip = 0; strip < item.layout->layout.strips.size(); ++strip) {
			entries.push_back({.anchorY = item.position.y + item.layout->layout.strips[strip].anchorLocalY, .item = &item, .strip = strip});
		}
		return entries;
	}

	uint32_t stripsIn(const std::vector<Entry>& entries) {
		return static_cast<uint32_t>(std::count_if(entries.begin(), entries.end(), [](const Entry& entry) { return entry.item != nullptr; }));
	}

	// Walks a stream as emitSorted does and returns the draws, in the order drawn. The planner is sized from the
	// strips in the stream, and each layout numbers its strips' mesh ids from kTemplateSlots on at its first strip.
	std::vector<ItemDraw> drawStream(std::vector<Entry> entries) {
		std::stable_sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) { return left.anchorY < right.anchorY; });
		worldview::RunPlanner planner;
		planner.beginFrame(kViewportWidth, kViewportHeight, kTemplateSlots + stripsIn(entries));
		worldview::ItemViewIndex		views;
		(void)views.follow(assets::AssetRegistry::Get().generation());
		uint32_t						nextMesh = kTemplateSlots;
		std::vector<Run>				runs;
		uint32_t						runCount = 0;
		std::vector<worldview::RunSpan> spans;
		std::vector<ItemDraw>			draws;

		const auto close = [&]() {
			const std::span<const uint32_t> closing = planner.close();
			for (const uint32_t index : closing) {
				if (runs[index].view != worldview::kNotAView) {
					runs[index].itemsApart = !views.frameOf(runs[index].layout).itemsMeet;
				}
			}
			worldview::spanRuns(planner, closing, runs, spans);
			for (const worldview::RunSpan& span : spans) {
				const Run& run = runs[closing[span.first]];
				draws.push_back({.view = run.view, .firstStrip = run.strip, .strips = span.count, .instances = run.instances.size(), .planner = closing[span.first]});
				for (uint32_t i = 0; i < span.count; ++i) {
					runs[closing[span.first + i]].instances.clear();
				}
			}
		};

		const auto open = [&](uint32_t runIndex, uint32_t view, uint32_t layout, uint32_t strip) {
			if (runIndex == runCount) {
				if (runCount == runs.size()) {
					runs.emplace_back();
				}
				runs[runCount] = Run{.view = view, .layout = layout, .strip = strip};
				++runCount;
			}
		};

		for (const Entry& entry : entries) {
			if (entry.templateMesh >= 0) {
				const Foundation::Vec2 position{entry.position.x, entry.position.y};
				const uint32_t		   runIndex = planner.place(
					  static_cast<uint32_t>(entry.templateMesh),
					  planner.cellsOf((position - Foundation::Vec2{0.5F, 1.0F}) * kPixelsPerMeter, (position + Foundation::Vec2{0.5F, 0.0F}) * kPixelsPerMeter),
					  false
				  );
				open(runIndex, worldview::kNotAView, 0, static_cast<uint32_t>(entry.templateMesh));
				runs[runIndex].instances.emplace_back(position, 0.0F, 1.0F, Foundation::Vec4{1.0F, 1.0F, 1.0F, 1.0F});
				continue;
			}
			if (entry.item == nullptr) {
				close();
				continue;
			}
			const assets::ItemLayout& layout = *entry.item->layout;
			worldview::LayoutFrame*	  settled = nullptr;
			if (views.settle(layout, 1, nextMesh, settled)) {
				settled->view = layout.id;
				planner.reserveOrders(settled->firstMesh, static_cast<uint32_t>(layout.layout.strips.size()));
			}
			if (entry.strip == 0) {
				worldview::ItemViewIndex::noteItem(*settled, entry.item->position);
			}
			const assets::ItemStrip& box = layout.layout.strips[entry.strip];
			const Foundation::Vec2	 position{entry.item->position.x, entry.item->position.y};
			const Foundation::Vec2	 pixelMin = (position + Foundation::Vec2{box.minX, box.minY}) * kPixelsPerMeter;
			const Foundation::Vec2	 pixelMax = (position + Foundation::Vec2{box.maxX, box.maxY}) * kPixelsPerMeter;
			const uint32_t			 runIndex = planner.place(settled->firstMesh + entry.strip, planner.cellsOf(pixelMin, pixelMax), false);
			open(runIndex, layout.id, layout.id, entry.strip);
			runs[runIndex].instances.emplace_back(position, 0.0F, 1.0F, Foundation::Vec4{1.0F, 1.0F, 1.0F, 1.0F});
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

// Two items of one view whose boxes meet on the screen are never drawn by a ranged draw over both: merged, each item's
// strips would draw together, the north item's whole drawing before any of the south one's.
TEST_F(ItemStripDrawTest, ItemsOfOneViewThatMeetOnScreenAreNotMergedAcrossEachOther) {
	// Two benches at 45 degrees, 1.1 m apart square to their long sides (their colliders 0.1 m apart) and staggered
	// 1 m along them.
	const glm::vec2 along{0.7071F, 0.7071F};
	const glm::vec2 across{-0.7071F, 0.7071F};
	const Item		north{.layout = layoutAt(45.0), .position = {40.0F, 40.0F}};
	const Item		south{.layout = layoutAt(45.0), .position = glm::vec2{40.0F, 40.0F} + (1.1F * across) + along};
	std::vector<Entry> entries = stripsOf(north);
	const std::vector<Entry> other = stripsOf(south);
	entries.insert(entries.end(), other.begin(), other.end());
	const std::vector<ItemDraw> draws = drawStream(entries);
	uint32_t					stripsDrawn = 0;
	for (const ItemDraw& draw : draws) {
		if (draw.instances > 1) {
			EXPECT_EQ(draw.instances, 2U);
			EXPECT_EQ(draw.strips, 1U) << "a run of both items is drawn alone";
		}
		stripsDrawn += draw.strips * static_cast<uint32_t>(draw.instances);
	}
	EXPECT_EQ(stripsDrawn, 62U) << "every strip of both drawn once";
	EXPECT_GT(draws.size(), 1U);
}

// With no parts item in the stream the path is the planner's alone: template runs and a colonist give one draw a
// run, in the order the planner closes them, with the same draw indices.
TEST_F(ItemStripDrawTest, AStreamWithoutItemsDrawsTheRunsThePlannerAloneGives) {
	std::vector<Entry> entries;
	for (int i = 0; i < 12; ++i) {
		entries.push_back({.anchorY = static_cast<float>(i) * 0.4F, .templateMesh = i % 3, .position = {5.0F + (static_cast<float>(i % 4) * 0.6F), static_cast<float>(i) * 0.4F}});
	}
	entries.push_back({.anchorY = 2.1F}); // a colonist walking
	const std::vector<ItemDraw> draws = drawStream(entries);

	std::vector<Entry> sorted = entries;
	std::stable_sort(sorted.begin(), sorted.end(), [](const Entry& left, const Entry& right) { return left.anchorY < right.anchorY; });
	worldview::RunPlanner planner;
	planner.beginFrame(kViewportWidth, kViewportHeight, kTemplateSlots);
	std::vector<uint32_t> expected;
	for (const Entry& entry : sorted) {
		if (entry.templateMesh < 0) {
			const std::span<const uint32_t> closing = planner.close();
			expected.insert(expected.end(), closing.begin(), closing.end());
			continue;
		}
		const Foundation::Vec2 position{entry.position.x, entry.position.y};
		(void)planner.place(
			static_cast<uint32_t>(entry.templateMesh),
			planner.cellsOf((position - Foundation::Vec2{0.5F, 1.0F}) * kPixelsPerMeter, (position + Foundation::Vec2{0.5F, 0.0F}) * kPixelsPerMeter), false
		);
	}
	const std::span<const uint32_t> closing = planner.close();
	expected.insert(expected.end(), closing.begin(), closing.end());

	ASSERT_EQ(draws.size(), expected.size());
	for (size_t i = 0; i < draws.size(); ++i) {
		EXPECT_EQ(draws[i].view, worldview::kNotAView);
		EXPECT_EQ(draws[i].strips, 1U) << "template runs never merge";
		EXPECT_EQ(draws[i].planner, expected[i]) << "draw " << i;
	}
	EXPECT_GT(planner.splits(), 0U) << "the trees overlap, so the stream has layers to keep in order";
}

// The selection outline of a 45-degree bench (rotatable-placed-items 15): with nothing sorting among its strips it is
// one draw; each upright that sorts among them and whose box meets the outline adds one, an upright whose box is
// elsewhere on the screen adds none, and one whose box isn't known (an animated colonist) is taken to meet it.
TEST_F(ItemStripDrawTest, ATurnedItemsOutlineIsOneDrawUnlessSomethingMeetingItSortsAmongItsStrips) {
	const assets::ItemLayout*	  layout = layoutAt(45.0);
	const assets::ItemSilhouette* silhouette = assets::AssetRegistry::Get().itemSilhouette(bench, layout->turnQuanta);
	ASSERT_NE(silhouette, nullptr);
	const glm::vec2 at{40.0F, 40.0F};
	const auto		pixels = [](glm::vec2 point) { return Foundation::Vec2{point.x * kPixelsPerMeter, point.y * kPixelsPerMeter}; };

	std::vector<float>				  anchors;
	std::vector<worldview::ScreenBox> boxes;
	for (size_t strip = 0; strip < layout->layout.strips.size(); ++strip) {
		worldview::ScreenBox box{.min = {1.0e9F, 1.0e9F}, .max = {-1.0e9F, -1.0e9F}};
		for (const std::vector<glm::vec2>& line : silhouette->stripLines[strip]) {
			for (const glm::vec2& point : line) {
				const Foundation::Vec2 pixel = pixels(at + point);
				box.min = {std::min(box.min.x, pixel.x), std::min(box.min.y, pixel.y)};
				box.max = {std::max(box.max.x, pixel.x), std::max(box.max.y, pixel.y)};
			}
		}
		anchors.push_back(at.y + layout->layout.strips[strip].anchorLocalY);
		boxes.push_back(box);
	}
	const auto stripBox = [&](size_t strip, glm::vec2 offset) {
		const assets::ItemStrip& box = layout->layout.strips[strip];
		return worldview::ScreenBox{.min = pixels(at + offset + glm::vec2{box.minX, box.minY}), .max = pixels(at + offset + glm::vec2{box.maxX, box.maxY})};
	};
	size_t between = 0; // a gap in the anchors halfway along
	for (size_t strip = anchors.size() / 2; strip + 1 < anchors.size(); ++strip) {
		if (anchors[strip] < anchors[strip + 1]) {
			between = strip;
			break;
		}
	}
	ASSERT_GT(between, 0U);
	const float betweenAnchor = (anchors[between] + anchors[between + 1]) * 0.5F;

	struct Upright {
		float							   anchorY = 0.0F;
		std::optional<worldview::ScreenBox> box;
		bool							   own = false;
	};
	const auto drawsWith = [&](const std::vector<Upright>& others) {
		std::vector<Upright> stream;
		for (size_t strip = 0; strip < anchors.size(); ++strip) {
			stream.push_back({.anchorY = anchors[strip], .box = stripBox(strip, {0.0F, 0.0F}), .own = true});
		}
		stream.insert(stream.end(), others.begin(), others.end());
		std::stable_sort(stream.begin(), stream.end(), [](const Upright& left, const Upright& right) { return left.anchorY < right.anchorY; });
		worldview::OutlineCursor cursor;
		cursor.begin(anchors, boxes);
		size_t draws = 0;
		size_t pieces = 0;
		for (const Upright& upright : stream) {
			const size_t count = cursor.before(upright.anchorY, upright.box, upright.own);
			draws += count > 0 ? 1 : 0;
			pieces += count;
		}
		const size_t left = cursor.remaining();
		draws += left > 0 ? 1 : 0;
		EXPECT_EQ(pieces + left, anchors.size()) << "every piece drawn once";
		return draws;
	};

	EXPECT_EQ(drawsWith({}), 1U);
	const Upright meeting{.anchorY = betweenAnchor, .box = stripBox(between, {0.0F, 0.0F})};
	EXPECT_EQ(drawsWith({meeting}), 2U);
	EXPECT_EQ(drawsWith({{.anchorY = betweenAnchor, .box = stripBox(between, {100.0F, 0.0F})}}), 1U) << "far to the east";
	EXPECT_EQ(drawsWith({{.anchorY = betweenAnchor, .box = std::nullopt}}), 2U) << "a colonist";
	EXPECT_EQ(drawsWith({meeting, {.anchorY = anchors.back() + 1.0F, .box = stripBox(0, {0.0F, 0.0F})}}), 2U) << "one in front of every strip adds nothing";
	EXPECT_EQ(drawsWith({{.anchorY = anchors.front() - 1.0F, .box = stripBox(0, {0.0F, 0.0F})}}), 1U) << "nor one behind them all";
	const Upright earlier{.anchorY = (anchors[2] + anchors[between]) * 0.5F, .box = stripBox(2, {0.0F, 0.0F})};
	if (anchors[2] < earlier.anchorY && earlier.anchorY < anchors[between]) {
		EXPECT_EQ(drawsWith({earlier, meeting}), 3U);
	}
}

// A registry clear starts template ids and layouts over, so what was kept by them is dropped.
TEST_F(ItemStripDrawTest, ViewsAndLayoutFramesAreDroppedWhenTheRegistryClears) {
	worldview::ItemViewIndex views;
	assets::AssetRegistry&	 registry = assets::AssetRegistry::Get();
	EXPECT_FALSE(views.follow(registry.generation())) << "nothing to drop the first time";
	EXPECT_TRUE(views.viewSlot(bench, 4500, 0, true, 1).second);
	EXPECT_FALSE(views.viewSlot(bench, 4500, 0, true, 1).second) << "kept";
	uint32_t				nextMesh = 0;
	worldview::LayoutFrame* frame = nullptr;
	EXPECT_TRUE(views.settle(*layoutAt(45.0), 1, nextMesh, frame));
	EXPECT_FALSE(views.follow(registry.generation())) << "the same generation keeps them";
	EXPECT_EQ(views.viewCount(), 1U);

	ASSERT_NE(load("Bench"), nullptr);
	EXPECT_TRUE(views.follow(registry.generation())) << "a clear drops them";
	EXPECT_EQ(views.viewCount(), 0U);
	EXPECT_TRUE(views.viewSlot(registry.templateId("Bench", {}), 4500, 0, true, 1).second) << "a view is made again for the new ids";
}

// The release rule (rotatable-placed-items 10): a finer view not drawn for the eviction span goes; a reference view
// goes only when it is also stale and no installed item stands at its template and turn. A view never ready goes
// once nothing has wanted it for the span. Slots are handed out again, so placing, turning, and removing items
// doesn't grow the slots without bound.
TEST_F(ItemStripDrawTest, ViewsNothingWantsAreReleasedAndTheirSlotsHandedOutAgain) {
	using worldview::ViewState;
	constexpr uint64_t		 kEvict = 100;
	worldview::ItemViewIndex views;
	(void)views.follow(assets::AssetRegistry::Get().generation());
	bool		standing = true; // an item stands at (bench, 45 degrees)
	const auto	kept = [&](uint32_t templateId, uint32_t turnQuanta) { return standing && templateId == bench && turnQuanta == 4500; };
	const auto	reference = views.viewSlot(bench, 4500, 1, true, 1).first;
	const auto	finer = views.viewSlot(bench, 4500, 2, false, 1).first;
	const auto	requested = views.viewSlot(bench, 9000, 2, false, 1).first;
	views.setState(reference, ViewState::Ready);
	views.setState(finer, ViewState::Ready);
	views.setState(requested, ViewState::Requested);
	EXPECT_EQ(views.readyMask(bench, 4500), 0b110);

	views.view(finer).drawnFrame = 50;
	views.view(requested).wantedFrame = 90;
	EXPECT_TRUE(views.releaseStale(1 + kEvict, kEvict, kept).empty()) << "nothing is stale yet";
	EXPECT_EQ(views.releaseStale(151, kEvict, kept), std::vector<uint32_t>{finer}) << "the finer view, not drawn for the span";
	EXPECT_EQ(views.readyMask(bench, 4500), 0b010);
	EXPECT_FALSE(views.findView(bench, 4500, 2).has_value());

	const std::vector<uint32_t> late = views.releaseStale(1000, kEvict, kept);
	EXPECT_EQ(late, std::vector<uint32_t>{requested}) << "never ready and not wanted for the span; the reference view stays while the item stands";
	EXPECT_TRUE(views.findView(bench, 4500, 1).has_value());
	standing = false;
	EXPECT_EQ(views.releaseStale(1000, kEvict, kept), std::vector<uint32_t>{reference}) << "the item is gone";
	EXPECT_EQ(views.viewCount(), 0U);
	EXPECT_EQ(views.readyMask(bench, 4500), 0);

	// A turn dragged through a thousand angles, each view released as the next comes: the slots stay few.
	for (uint32_t turn = 0; turn < 1000; ++turn) {
		const uint64_t frame = 2000 + (static_cast<uint64_t>(turn) * (kEvict + 1));
		views.setState(views.viewSlot(bench, turn, 1, true, frame).first, ViewState::Ready);
		(void)views.releaseStale(frame, kEvict, kept);
	}
	EXPECT_LE(views.slotBound(), 3U);
	EXPECT_LE(views.viewCount(), 2U);
}

// TemplateLevelBuilder: a wanted level is built off the calling thread and handed back once; a
// request no frame wants any more is dropped before it starts.

#include "world/rendering/TemplateLevelBuilder.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

using namespace worldview;

namespace {

	// Polls until the builder has nothing pending, collecting what it finished; false on timeout.
	bool drain(TemplateLevelBuilder& builder, std::vector<TemplateLevelBuilder::Finished>& out) {
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (std::chrono::steady_clock::now() < deadline) {
			for (auto& done : builder.takeFinished()) {
				out.push_back(std::move(done));
			}
			if (builder.pending() == 0) {
				for (auto& done : builder.takeFinished()) {
					out.push_back(std::move(done));
				}
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return false;
	}

	renderer::TessellatedMesh meshOf(uint32_t templateId, uint8_t level) {
		renderer::TessellatedMesh mesh;
		mesh.vertices.emplace_back(static_cast<float>(templateId), static_cast<float>(level));
		return mesh;
	}

} // namespace

TEST(TemplateLevelBuilderTest, BuildsEachWantedLevelOnce) {
	std::mutex								mutex;
	std::multiset<std::pair<uint32_t, int>> builds;
	TemplateLevelBuilder					builder([&](uint32_t templateId, uint8_t level) -> std::optional<renderer::TessellatedMesh> {
		   {
			   std::lock_guard<std::mutex> lock(mutex);
			   builds.insert({templateId, level});
		   }
		   if (level == 7) {
			   return std::nullopt; // a failed build comes back empty
		   }
		   return meshOf(templateId, level);
	   });

	builder.want(1, 0);
	builder.want(1, 0);
	builder.want(2, 4);
	builder.want(3, 7);
	builder.endFrame();
	// Still wanted the next frame: queued or building, not asked twice.
	builder.want(1, 0);
	builder.want(2, 4);
	builder.want(3, 7);
	builder.endFrame();

	std::vector<TemplateLevelBuilder::Finished> finished;
	ASSERT_TRUE(drain(builder, finished));
	ASSERT_EQ(finished.size(), 3U);
	EXPECT_EQ(builds.count({1, 0}), 1U);
	for (const auto& done : finished) {
		if (done.level == 7) {
			EXPECT_FALSE(done.mesh.has_value());
		} else {
			ASSERT_TRUE(done.mesh.has_value());
			EXPECT_EQ(done.mesh->vertices.front().x, static_cast<float>(done.templateId));
		}
	}
}

TEST(TemplateLevelBuilderTest, DropsARequestNoFrameWantsBeforeItStarts) {
	std::promise<void>		 firstStarted;
	std::promise<void>		 releaseFirst;
	std::shared_future<void> release = releaseFirst.get_future().share();
	std::atomic<int>		 builtSecond{0};
	TemplateLevelBuilder	 builder([&](uint32_t templateId, uint8_t level) -> std::optional<renderer::TessellatedMesh> {
		if (templateId == 1) {
			firstStarted.set_value();
			release.wait();
		} else {
			++builtSecond;
		}
		return meshOf(templateId, level);
	});

	builder.want(1, 0);
	builder.endFrame();
	firstStarted.get_future().wait(); // the worker is busy with template 1

	builder.want(2, 0);
	builder.endFrame(); // kept: wanted this frame
	EXPECT_EQ(builder.pending(), 2U);
	builder.endFrame(); // not wanted this frame: dropped, though it never started
	EXPECT_EQ(builder.pending(), 1U);

	releaseFirst.set_value();
	std::vector<TemplateLevelBuilder::Finished> finished;
	ASSERT_TRUE(drain(builder, finished));
	ASSERT_EQ(finished.size(), 1U);
	EXPECT_EQ(finished.front().templateId, 1U);
	EXPECT_EQ(builtSecond.load(), 0);
}

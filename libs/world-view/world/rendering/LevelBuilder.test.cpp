// LevelBuilder: a wanted key is built off the calling thread and handed back once; a request no frame wants any
// more is dropped before it starts.

#include "world/rendering/LevelBuilder.h"

#include <vector/Types.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

using namespace worldview;

namespace {

	struct Key {
		uint32_t templateId = 0;
		uint8_t	 level = 0;
		bool	 operator==(const Key&) const = default;
		struct Hash {
			size_t operator()(const Key& key) const { return std::hash<uint64_t>{}((static_cast<uint64_t>(key.templateId) << 8U) | key.level); }
		};
	};
	using Builder = LevelBuilder<Key, renderer::TessellatedMesh>;

	// Polls until the builder has nothing pending, collecting what it finished; false on timeout.
	bool drain(Builder& builder, std::vector<Builder::Finished>& out) {
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

	renderer::TessellatedMesh meshOf(const Key& key) {
		renderer::TessellatedMesh mesh;
		mesh.vertices.emplace_back(static_cast<float>(key.templateId), static_cast<float>(key.level));
		return mesh;
	}

} // namespace

TEST(LevelBuilderTest, BuildsEachWantedKeyOnce) {
	std::mutex								mutex;
	std::multiset<std::pair<uint32_t, int>> builds;
	Builder									builder([&](const Key& key) -> std::optional<renderer::TessellatedMesh> {
		{
			std::lock_guard<std::mutex> lock(mutex);
			builds.insert({key.templateId, key.level});
		}
		if (key.level == 7) {
			return std::nullopt; // a failed build comes back empty
		}
		return meshOf(key);
	});

	builder.want({1, 0});
	builder.want({1, 0});
	builder.want({2, 4});
	builder.want({3, 7});
	builder.endFrame();
	// Still wanted the next frame: queued or building, not asked twice.
	builder.want({1, 0});
	builder.want({2, 4});
	builder.want({3, 7});
	builder.endFrame();

	std::vector<Builder::Finished> finished;
	ASSERT_TRUE(drain(builder, finished));
	ASSERT_EQ(finished.size(), 3U);
	EXPECT_EQ(builds.count({1, 0}), 1U);
	for (const auto& done : finished) {
		if (done.key.level == 7) {
			EXPECT_FALSE(done.result.has_value());
		} else {
			ASSERT_TRUE(done.result.has_value());
			EXPECT_EQ(done.result->vertices.front().x, static_cast<float>(done.key.templateId));
		}
	}
}

TEST(LevelBuilderTest, AResultIsHandedBackWithoutWaitingForASlowerOne) {
	if (std::thread::hardware_concurrency() < 3) {
		GTEST_SKIP() << "needs a pool of two threads, so both keys build in one batch";
	}
	std::promise<void>		 releaseSlow;
	std::shared_future<void> release = releaseSlow.get_future().share();
	Builder					 builder([&](const Key& key) -> std::optional<renderer::TessellatedMesh> {
		 if (key.templateId == 1) {
			 release.wait();
		 }
		 return meshOf(key);
	 });

	builder.want({1, 0});
	builder.want({2, 0});
	builder.endFrame();

	std::vector<Builder::Finished> finished;
	const auto					   deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (finished.empty() && std::chrono::steady_clock::now() < deadline) {
		for (auto& done : builder.takeFinished()) {
			finished.push_back(std::move(done));
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	releaseSlow.set_value();
	ASSERT_EQ(finished.size(), 1U);
	EXPECT_EQ(finished.front().key.templateId, 2U);
	ASSERT_TRUE(drain(builder, finished));
	EXPECT_EQ(finished.size(), 2U);
}

TEST(LevelBuilderTest, DropsARequestNoFrameWantsBeforeItStarts) {
	std::promise<void>		 firstStarted;
	std::promise<void>		 releaseFirst;
	std::shared_future<void> release = releaseFirst.get_future().share();
	std::atomic<int>		 builtSecond{0};
	Builder					 builder([&](const Key& key) -> std::optional<renderer::TessellatedMesh> {
		 if (key.templateId == 1) {
			 firstStarted.set_value();
			 release.wait();
		 } else {
			 ++builtSecond;
		 }
		 return meshOf(key);
	 });

	builder.want({1, 0});
	builder.endFrame();
	firstStarted.get_future().wait(); // the worker is busy with key 1

	builder.want({2, 0});
	builder.endFrame(); // kept: wanted this frame
	EXPECT_EQ(builder.pending(), 2U);
	builder.endFrame(); // not wanted this frame: dropped, though it never started
	EXPECT_EQ(builder.pending(), 1U);

	releaseFirst.set_value();
	std::vector<Builder::Finished> finished;
	ASSERT_TRUE(drain(builder, finished));
	ASSERT_EQ(finished.size(), 1U);
	EXPECT_EQ(finished.front().key.templateId, 1U);
	EXPECT_EQ(builtSecond.load(), 0);
}

#pragma once

// Builds meshes at the detail levels the camera wants (assets/DetailLevels.h) off the GL thread: template levels, and
// parts items' views (rotatable-placed-items 10). The renderer says each frame which keys it is missing (want), then
// ends the frame (endFrame): a request not wanted again in that frame is dropped if it hasn't started, so a template
// that left the view or a level the zoom moved past costs nothing. A worker thread takes the queue in batches, oldest
// first, and builds each batch on its own task pool; the renderer collects the results (takeFinished) and uploads
// them on the GL thread.

#include <threading/TaskPool.h>
#include <utils/Log.h>

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace worldview {

	/// `Key` is equality-comparable and hashed by `Key::Hash`; `Result` is what one build makes.
	template <typename Key, typename Result>
	class LevelBuilder {
	  public:
		/// Builds one key's result; nullopt when it fails (the builder never retries). Runs on the pool's threads,
		/// several at once.
		using BuildFn = std::function<std::optional<Result>(const Key& key)>;

		struct Finished {
			Key					  key;
			std::optional<Result> result;
		};

		explicit LevelBuilder(BuildFn build)
			: build(std::move(build)),
			  worker([this](const std::stop_token& stop) { run(stop); }) {}
		~LevelBuilder() {
			worker.request_stop();
			wake.notify_all();
		}
		LevelBuilder(const LevelBuilder&) = delete;
		LevelBuilder& operator=(const LevelBuilder&) = delete;
		LevelBuilder(LevelBuilder&&) = delete;
		LevelBuilder& operator=(LevelBuilder&&) = delete;

		/// This frame needs the result: queued unless it is queued, building, or finished and not yet taken.
		void want(const Key& key) {
			std::lock_guard<std::mutex> lock(mutex);
			if (building.contains(key) || finishedKeys.contains(key)) {
				return;
			}
			auto [entry, inserted] = queued.try_emplace(key);
			entry->second.wantedFrame = frame;
			if (inserted) {
				queue.push_back(key);
			}
		}

		/// Drops every queued request not wanted since the last endFrame, then lets the worker run.
		void endFrame() {
			{
				std::lock_guard<std::mutex> lock(mutex);
				std::erase_if(queue, [&](const Key& key) {
					const auto found = queued.find(key);
					if (found->second.wantedFrame == frame) {
						found->second.kept = true;
						return false;
					}
					queued.erase(found);
					return true;
				});
				++frame;
			}
			wake.notify_one();
		}

		/// The results built since the last call, failures included.
		[[nodiscard]] std::vector<Finished> takeFinished() {
			std::lock_guard<std::mutex> lock(mutex);
			finishedKeys.clear();
			return std::exchange(finished, {});
		}

		/// Requests queued or building.
		[[nodiscard]] size_t pending() const {
			std::lock_guard<std::mutex> lock(mutex);
			return queue.size() + building.size();
		}

	  private:
		using Hash = typename Key::Hash;

		void run(const std::stop_token& stop) {
			foundation::TaskPool  pool;
			std::vector<Finished> batch;
			while (true) {
				batch.clear();
				{
					std::unique_lock<std::mutex> lock(mutex);
					// Only entries an endFrame has kept (the oldest, so at the front): one dropped this frame never starts.
					wake.wait(lock, stop, [&] { return !queue.empty() && queued.at(queue.front()).kept; });
					if (stop.stop_requested()) {
						return;
					}
					while (!queue.empty() && batch.size() < std::max(pool.threadCount(), 1U) && queued.at(queue.front()).kept) {
						const Key key = queue.front();
						queue.pop_front();
						queued.erase(key);
						building.insert(key);
						batch.push_back({.key = key, .result = std::nullopt});
					}
				}

				// Each result is handed over as it finishes, so a slow build holds back only itself.
				pool.parallelFor(0, batch.size(), 1, [&](size_t first, size_t last) {
					for (size_t i = first; i < last; ++i) {
						Finished& done = batch[i];
						try {
							done.result = build(done.key);
						} catch (const std::exception& failure) {
							LOG_ERROR(Engine, "A level build threw: %s", failure.what());
						}
						std::lock_guard<std::mutex> lock(mutex);
						building.erase(done.key);
						finishedKeys.insert(done.key);
						finished.push_back(std::move(done));
					}
				});
			}
		}

		BuildFn build;

		mutable std::mutex			mutex;
		std::condition_variable_any wake;
		std::deque<Key>				queue; // oldest first
		struct Queued {
			uint64_t wantedFrame = 0; // the frame that last wanted it
			bool	 kept = false;	  // an endFrame has kept it, so the worker may start it
		};
		std::unordered_map<Key, Queued, Hash> queued;
		std::unordered_set<Key, Hash>		  building;		// taken by the worker
		std::unordered_set<Key, Hash>		  finishedKeys; // in `finished`, not yet taken
		std::vector<Finished>				  finished;
		uint64_t							  frame = 0;

		std::jthread worker; // last: started once everything it reads is constructed, joined first
	};

} // namespace worldview

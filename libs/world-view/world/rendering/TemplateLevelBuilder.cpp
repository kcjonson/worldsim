#include "TemplateLevelBuilder.h"

#include <assets/AssetRegistry.h>
#include <threading/TaskPool.h>
#include <utils/Log.h>

#include <algorithm>
#include <exception>
#include <utility>

namespace worldview {

	TemplateLevelBuilder::TemplateLevelBuilder()
		: TemplateLevelBuilder([](uint32_t templateId, uint8_t level) {
			  assets::AssetRegistry::BuiltTemplate built = assets::AssetRegistry::Get().buildTemplateLevel(templateId, level);
			  return std::move(built.mesh);
		  }) {}

	TemplateLevelBuilder::TemplateLevelBuilder(BuildFn build)
		: build(std::move(build)),
		  worker([this](const std::stop_token& stop) { run(stop); }) {}

	TemplateLevelBuilder::~TemplateLevelBuilder() {
		worker.request_stop();
		wake.notify_all();
	}

	void TemplateLevelBuilder::want(uint32_t templateId, uint8_t level) {
		const uint64_t				k = key(templateId, level);
		std::lock_guard<std::mutex> lock(mutex);
		if (building.contains(k) || finishedKeys.contains(k)) {
			return;
		}
		auto [it, inserted] = queued.try_emplace(k);
		it->second.wantedFrame = frame;
		if (inserted) {
			queue.push_back(k);
		}
	}

	void TemplateLevelBuilder::endFrame() {
		{
			std::lock_guard<std::mutex> lock(mutex);
			std::erase_if(queue, [&](uint64_t k) {
				const auto found = queued.find(k);
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

	std::vector<TemplateLevelBuilder::Finished> TemplateLevelBuilder::takeFinished() {
		std::lock_guard<std::mutex> lock(mutex);
		finishedKeys.clear();
		return std::exchange(finished, {});
	}

	size_t TemplateLevelBuilder::pending() const {
		std::lock_guard<std::mutex> lock(mutex);
		return queue.size() + building.size();
	}

	void TemplateLevelBuilder::run(const std::stop_token& stop) {
		foundation::TaskPool pool;
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
					const uint64_t k = queue.front();
					queue.pop_front();
					queued.erase(k);
					building.insert(k);
					batch.push_back({.templateId = static_cast<uint32_t>(k >> 8U), .level = static_cast<uint8_t>(k & 0xFFU)});
				}
			}

			// Each level is handed over as it finishes, so a slow build holds back only itself.
			pool.parallelFor(0, batch.size(), 1, [&](size_t first, size_t last) {
				for (size_t i = first; i < last; ++i) {
					Finished& done = batch[i];
					try {
						done.mesh = build(done.templateId, done.level);
					} catch (const std::exception& e) {
						LOG_ERROR(Engine, "Building template %u at detail level %u threw: %s", done.templateId, static_cast<unsigned>(done.level), e.what());
					}
					const uint64_t				k = key(done.templateId, done.level);
					std::lock_guard<std::mutex> lock(mutex);
					building.erase(k);
					finishedKeys.insert(k);
					finished.push_back(std::move(done));
				}
			});
		}
	}

} // namespace worldview

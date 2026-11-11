#pragma once

// Builds template meshes at the detail levels the camera wants (assets/DetailLevels.h), off the GL
// thread. The renderer says each frame which (template, level) meshes it is missing (want), then ends
// the frame (endFrame): a request not wanted again in that frame is dropped if it hasn't started, so a
// template that left the view or a level the zoom moved past costs nothing. A worker thread takes the
// queue in batches, oldest first, and builds each batch on its own task pool; the renderer collects
// the results (takeFinished) and uploads them on the GL thread.

#include <vector/Types.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace worldview {

	class TemplateLevelBuilder {
	  public:
		/// Builds one template's mesh at one level; nullopt when it fails (the builder never retries).
		/// Runs on the pool's threads, several at once.
		using BuildFn = std::function<std::optional<renderer::TessellatedMesh>(uint32_t templateId, uint8_t level)>;

		struct Finished {
			uint32_t								 templateId = 0;
			uint8_t									 level = 0;
			std::optional<renderer::TessellatedMesh> mesh;
		};

		/// The registry's buildTemplateLevel.
		TemplateLevelBuilder();
		explicit TemplateLevelBuilder(BuildFn build);
		~TemplateLevelBuilder();
		TemplateLevelBuilder(const TemplateLevelBuilder&) = delete;
		TemplateLevelBuilder& operator=(const TemplateLevelBuilder&) = delete;
		TemplateLevelBuilder(TemplateLevelBuilder&&) = delete;
		TemplateLevelBuilder& operator=(TemplateLevelBuilder&&) = delete;

		/// This frame needs the mesh: queued unless it is queued, building, or finished and not yet taken.
		void want(uint32_t templateId, uint8_t level);

		/// Drops every queued request not wanted since the last endFrame, then lets the worker run.
		void endFrame();

		/// The meshes built since the last call, failures included.
		[[nodiscard]] std::vector<Finished> takeFinished();

		/// Requests queued or building.
		[[nodiscard]] size_t pending() const;

	  private:
		[[nodiscard]] static uint64_t key(uint32_t templateId, uint8_t level) { return (static_cast<uint64_t>(templateId) << 8U) | level; }
		void						  run(const std::stop_token& stop);

		BuildFn build;

		mutable std::mutex					   mutex;
		std::condition_variable_any			   wake;
		std::deque<uint64_t>				   queue;			// oldest first
		struct Queued {
			uint64_t wantedFrame = 0;	 // the frame that last wanted it
			bool	 kept = false;		 // an endFrame has kept it, so the worker may start it
		};
		std::unordered_map<uint64_t, Queued>   queued;
		std::unordered_set<uint64_t>		   building;		// taken by the worker
		std::unordered_set<uint64_t>		   finishedKeys;	// in `finished`, not yet taken
		std::vector<Finished>				   finished;
		uint64_t							   frame = 0;

		std::jthread worker; // last: started once everything it reads is constructed, joined first
	};

} // namespace worldview

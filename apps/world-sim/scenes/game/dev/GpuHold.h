#pragma once

// GpuHold - GPU work that keeps the card at its top performance state through a perf capture
// (docs/technical/ground-textures.md, "Clock state"). On a light view the driver drops an RTX 3090
// from P0 to P2 or P3, and the scene's GPU timers then measure the clock rather than the code.
// Nobody changes driver clock settings, so instead, each frame, this draws a throwaway shader into
// its own 512x512 target, which nothing reads, and sizes that work so the scene's GPU time plus its
// own sits near a fraction of the frame time.
//
// GameScene calls render() once a frame after its scene GPU timer has closed, so no scene timer
// counts this work. Off until the dev server's gpuhold control turns it on, so a build without the
// server never runs it; GL objects are made on first use.

#include <metrics/GPUTimer.h>

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace world_sim {

	class GpuHold {
	  public:
		static constexpr float kDefaultTarget = 0.8F;

		GpuHold();
		~GpuHold();

		GpuHold(const GpuHold&) = delete;
		GpuHold& operator=(const GpuHold&) = delete;
		GpuHold(GpuHold&&) = delete;
		GpuHold& operator=(GpuHold&&) = delete;

		/// /api/control?action=gpuhold (GpuHoldCommand.h): value=0|1 turns it off or on, target=0.05..0.95
		/// sets the fraction of the frame time to hold; either may be left out. False, with the 400's message
		/// in error, for any other value.
		bool configure(const std::map<std::string, std::string>& params, std::string& error);

		/// Once a frame, with GL current and no GPU timer span open. sceneGpuMs: the scene's GPU time.
		void render(float sceneGpuMs);

		/// {"enabled","target","passes","holdGpuMs","sceneGpuMs","frameMs"}: the GPU times are the latest
		/// results, a few frames old.
		[[nodiscard]] std::string statusJson() const;

	  private:
		struct Gl;

		bool createGl();
		void reset();

		bool  enabled = false;
		float target = kDefaultTarget;

		// The controller: the frame interval, smoothed, and the pass count it moves.
		std::optional<std::chrono::steady_clock::time_point> lastRender;
		float												 frameMs = 0.0F;
		float												 passCount = 1.0F;
		int													 passes = 0;
		float												 lastSceneGpuMs = 0.0F;

		std::unique_ptr<Gl> gl;
		Renderer::GPUTimer	timer;
	};

} // namespace world_sim

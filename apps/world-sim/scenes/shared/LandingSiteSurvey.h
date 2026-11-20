#pragma once

// One worker per planet that finds its suggested landing sites and resolves the player's picks, so
// neither a scan nor a 50 to 160 ms spawn search runs on the main thread. The newest pick waiting is
// resolved next, ahead of the rest of the scan's verification; a pick replaced before its result is
// handed back is dropped. Results cross to the main thread only through take*; the worker reads the
// planet and its own copy of the provisions table, nothing else. docs/technical/landing-sites.md 4.3.

#include <worldgen/sampling/LandingSite.h>

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

namespace world_sim {

	class LandingSiteSurvey {
	  public:
		LandingSiteSurvey() = default;
		LandingSiteSurvey(const LandingSiteSurvey&) = delete;
		LandingSiteSurvey& operator=(const LandingSiteSurvey&) = delete;
		~LandingSiteSurvey();

		// Cancels any survey already running, then starts this planet's.
		void start(
			std::shared_ptr<const worldgen::GeneratedWorld> world,
			worldgen::BiomeProvisionTable					 provisions,
			std::optional<worldgen::Biome>					 preferredBiome,
			std::size_t										 maximumSites
		);
		// True from start until the sites are ready (taken or not), and false once cancelled.
		[[nodiscard]] bool surveying() const;
		// The suggested sites, once, after the scan finishes.
		[[nodiscard]] std::optional<std::vector<worldgen::LandingSite>> takeSites();

		// Replaces any pick not yet resolved. Ignored when no survey is running.
		void resolve(worldgen::LatLon pick);
		// True from resolve until that pick's landing is ready.
		[[nodiscard]] bool resolving() const;
		struct ResolvedPick {
			worldgen::LatLon	  pick;
			worldgen::LandingSite landing;
		};
		// The newest pick's landing, once.
		[[nodiscard]] std::optional<ResolvedPick> takeResolved();

		// Requests stop and joins; the survey's results are dropped.
		void cancel();

	  private:
		void run(
			const std::stop_token&							 stop,
			const std::shared_ptr<const worldgen::GeneratedWorld>& world,
			const worldgen::BiomeProvisionTable&			 provisions,
			std::optional<worldgen::Biome>					 preferredBiome,
			std::size_t										 maximumSites
		);
		void resolveWaitingPick(const std::shared_ptr<const worldgen::GeneratedWorld>& world, const worldgen::BiomeProvisionTable& provisions);

		mutable std::mutex											mutex;
		std::condition_variable_any									pickWaiting;
		bool														scanning{false};
		std::optional<std::vector<worldgen::LandingSite>>			sites;
		std::optional<worldgen::LatLon>								waitingPick;
		bool														pickInProgress{false};
		std::optional<ResolvedPick>									resolved;
		std::jthread												worker;
	};

} // namespace world_sim

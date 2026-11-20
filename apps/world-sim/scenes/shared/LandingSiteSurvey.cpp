#include "scenes/shared/LandingSiteSurvey.h"

#include <utility>

namespace world_sim {

	LandingSiteSurvey::~LandingSiteSurvey() {
		cancel();
	}

	void LandingSiteSurvey::start(
		std::shared_ptr<const worldgen::GeneratedWorld> world,
		worldgen::BiomeProvisionTable					 provisions,
		worldgen::SuggestedSiteQuery					 query
	) {
		cancel();
		{
			const std::scoped_lock lock(mutex);
			scanning = true;
		}
		worker = std::jthread(
			[this, world = std::move(world), provisions = std::move(provisions), query = std::move(query)](
				const std::stop_token& stop
			) mutable { run(stop, world, provisions, std::move(query)); }
		);
	}

	bool LandingSiteSurvey::surveying() const {
		const std::scoped_lock lock(mutex);
		return scanning;
	}

	std::optional<std::vector<worldgen::LandingSite>> LandingSiteSurvey::takeSites() {
		const std::scoped_lock lock(mutex);
		return std::exchange(sites, std::nullopt);
	}

	void LandingSiteSurvey::resolve(worldgen::LatLon pick) {
		if (!worker.joinable()) {
			return;
		}
		{
			const std::scoped_lock lock(mutex);
			waitingPick = pick;
			resolved.reset();
		}
		pickWaiting.notify_one();
	}

	bool LandingSiteSurvey::resolving() const {
		const std::scoped_lock lock(mutex);
		return waitingPick.has_value() || pickInProgress;
	}

	std::optional<LandingSiteSurvey::ResolvedPick> LandingSiteSurvey::takeResolved() {
		const std::scoped_lock lock(mutex);
		return std::exchange(resolved, std::nullopt);
	}

	void LandingSiteSurvey::cancel() {
		if (worker.joinable()) {
			worker.request_stop();
			worker.join();
		}
		const std::scoped_lock lock(mutex);
		scanning = false;
		sites.reset();
		waitingPick.reset();
		pickInProgress = false;
		resolved.reset();
	}

	void LandingSiteSurvey::run(
		const std::stop_token&								   stop,
		const std::shared_ptr<const worldgen::GeneratedWorld>& world,
		const worldgen::BiomeProvisionTable&				   provisions,
		worldgen::SuggestedSiteQuery						   query
	) {
		query.stopToken = stop;
		query.checkpoint = [&] { resolveWaitingPick(world, provisions); };
		std::vector<worldgen::LandingSite> found = worldgen::findSuggestedLandingSites(world, provisions, query);
		{
			const std::scoped_lock lock(mutex);
			if (!stop.stop_requested()) {
				sites = std::move(found);
			}
			scanning = false;
		}

		while (true) {
			{
				std::unique_lock lock(mutex);
				if (!pickWaiting.wait(lock, stop, [this] { return waitingPick.has_value(); })) {
					return;
				}
			}
			resolveWaitingPick(world, provisions);
		}
	}

	void LandingSiteSurvey::resolveWaitingPick(
		const std::shared_ptr<const worldgen::GeneratedWorld>& world, const worldgen::BiomeProvisionTable& provisions
	) {
		worldgen::LatLon pick;
		{
			const std::scoped_lock lock(mutex);
			if (!waitingPick) {
				return;
			}
			pick = *waitingPick;
			waitingPick.reset();
			pickInProgress = true;
		}
		worldgen::LandingSite landing = worldgen::resolveLandingSite(world, provisions, pick);
		const std::scoped_lock lock(mutex);
		pickInProgress = false;
		if (!waitingPick) {
			resolved = ResolvedPick{pick, std::move(landing)};
		}
	}

} // namespace world_sim

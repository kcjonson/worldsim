#pragma once

// The benches' one way to the Quick Start landing: the shipped planet (WORLDSIM_PLANET names the
// .wsplanet, else the project's planets/quickstart.wsplanet) with the world asset definitions loaded once.

#include <worldgen/data/GeneratedWorld.h>
#include <worldgen/sampling/LandingSite.h>

#include <benchmark/benchmark.h>
#include <glm/vec2.hpp>

#include <filesystem>
#include <memory>

namespace bench {

	std::filesystem::path projectRoot();

	struct QuickStartLanding {
		std::shared_ptr<const worldgen::GeneratedWorld> planet;
		worldgen::LandingSite							landing;
		glm::vec2										drop{0.0F, 0.0F}; // the landing's spawn, meters from its origin
	};

	// Quick Start's landing, found once. Null when there is none, the bench then skipped (no planet file) or
	// failed (a planet or asset definitions that won't load, or no site) with the reason.
	const QuickStartLanding* quickStartLanding(benchmark::State& state);

} // namespace bench

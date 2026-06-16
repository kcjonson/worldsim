#include "WorldCreatorModel.h"

#include <algorithm>
#include <random>

namespace world_sim {

WorldCreatorModel::WorldCreatorModel() {
	params = worldgen::PlanetParams::preset(worldgen::Preset::EarthLike);
}

void WorldCreatorModel::setPreset(worldgen::Preset preset) {
	// Presets define the planet, not the generator: keep seed and resolution
	// so switching presets doesn't silently diverge from what the UI shows
	uint64_t seed = params.seed;
	uint32_t subdivision = params.gridSubdivision;
	params = worldgen::PlanetParams::preset(preset);
	params.seed = seed;
	params.gridSubdivision = subdivision;
}

void WorldCreatorModel::setWaterAmount(double percent) {
	params.waterAmount = std::clamp(percent / 100.0, 0.0, 1.0);
}

void WorldCreatorModel::setTectonicPlates(int count) {
	params.tectonicPlateCount = std::clamp(count, 2, 30);
}

void WorldCreatorModel::setPlanetRadius(double earthRadii) {
	params.planetRadius = std::clamp(earthRadii, 0.1, 10.0);
}

void WorldCreatorModel::setRotationRate(double days) {
	params.rotationRate = std::clamp(days, 0.1, 100.0);
}

void WorldCreatorModel::setPlanetAge(double years) {
	params.planetAge = std::clamp(years, 1.0e7, 1.0e10);
}

void WorldCreatorModel::setAtmosphereStrength(double atm) {
	params.atmosphereStrength = std::clamp(atm, 0.1, 10.0);
}

void WorldCreatorModel::setStarTemperature(double kelvin) {
	params.starTemperature = std::clamp(kelvin, 2000.0, 50000.0);
}

void WorldCreatorModel::setSemiMajorAxis(double au) {
	params.semiMajorAxis = std::clamp(au, 0.1, 100.0);
}

void WorldCreatorModel::setEccentricity(double ecc) {
	params.eccentricity = std::clamp(ecc, 0.0, 0.95);
}

void WorldCreatorModel::setGridSubdivision(uint32_t n) {
	params.gridSubdivision = n;
}

void WorldCreatorModel::setSeed(uint64_t seed) {
	params.seed = seed;
}

void WorldCreatorModel::randomizeSeed() {
	std::random_device rd;
	params.seed = (static_cast<uint64_t>(rd()) << 32) | rd();
}

void WorldCreatorModel::startGeneration() {
	// Generate is the single regenerate path: valid from Configuring (first run)
	// and from Reviewing (regenerate after tweaking params). Only an in-flight
	// generation blocks a new start.
	if (state == WorldCreatorState::Generating) {
		return;
	}
	result.reset();
	state = WorldCreatorState::Generating;
	generator.start(params);
}

void WorldCreatorModel::cancelGeneration() {
	if (state != WorldCreatorState::Generating) {
		return;
	}
	generator.cancel();
	// State will transition once poll sees Cancelled
}

worldgen::GenerationProgress WorldCreatorModel::pollProgress() {
	if (state != WorldCreatorState::Generating) {
		return {};
	}

	auto prog = generator.progress();

	if (prog.state == worldgen::GenerationProgress::State::Complete) {
		result = generator.takeResult();
		// Complete with no result is a failure, not a reviewable world
		state = result ? WorldCreatorState::Reviewing : WorldCreatorState::Configuring;
	} else if (prog.state == worldgen::GenerationProgress::State::Cancelled ||
	           prog.state == worldgen::GenerationProgress::State::Failed) {
		state = WorldCreatorState::Configuring;
	}

	return prog;
}

} // namespace world_sim

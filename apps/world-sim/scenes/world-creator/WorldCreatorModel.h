#pragma once

#include <worldgen/data/GeneratedWorld.h>
#include <worldgen/data/PlanetParams.h>
#include <worldgen/pipeline/PlanetGenerator.h>

#include <cstdint>
#include <memory>
#include <string>

// WorldCreatorModel - owns worldgen state and the PlanetGenerator.
//
// Three states: Configuring (parameter editing), Generating (background thread),
// Reviewing (results visible). The scene polls this model each frame.

namespace world_sim {

enum class WorldCreatorState {
	Configuring,
	Generating,
	Reviewing,
};

class WorldCreatorModel {
  public:
	WorldCreatorModel();

	// Apply a named preset to params
	void setPreset(worldgen::Preset preset);

	// Individual param setters (UI units: water is 0-100%, plates int, etc.)
	void setWaterAmount(double percent);    // converts to 0..1 fraction internally
	void setTectonicPlates(int count);
	void setPlanetRadius(double earthRadii);
	void setRotationRate(double days);
	void setPlanetAge(double years);
	void setAtmosphereStrength(double atm);
	void setStarTemperature(double kelvin);
	void setSemiMajorAxis(double au);
	void setEccentricity(double ecc);
	void setGridSubdivision(uint32_t n);   // 256/512/1024/1449
	void setSeed(uint64_t seed);
	void randomizeSeed();

	// Transition Configuring -> Generating
	void startGeneration();

	// Transition Generating -> Configuring (cancels background thread)
	void cancelGeneration();

	// Enter Reviewing with an existing world (returning from landing site
	// selection; scenes are recreated on switch, so the world rides back via
	// GameStartConfig). Adopts the world's params so the UI reflects it.
	void restoreResult(std::shared_ptr<const worldgen::GeneratedWorld> world);

	// Call from scene's update() - polls progress, drives state machine
	worldgen::GenerationProgress pollProgress();

	// State queries
	WorldCreatorState getState() const { return state; }
	const worldgen::PlanetParams& getParams() const { return params; }

	// Latest completed world (valid when state == Reviewing)
	std::shared_ptr<const worldgen::GeneratedWorld> getResult() const { return result; }

	// Latest progressive snapshot from the generator (updates during Generating)
	std::shared_ptr<const worldgen::GeneratedWorld> snapshot() const { return generator.snapshot(); }

  private:
	WorldCreatorState         state{WorldCreatorState::Configuring};
	worldgen::PlanetParams    params;
	worldgen::PlanetGenerator generator;

	std::shared_ptr<const worldgen::GeneratedWorld> result;
};

} // namespace world_sim

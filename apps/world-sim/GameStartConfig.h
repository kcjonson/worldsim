#pragma once

// GameStartConfig - scene-to-scene handoff for starting a game on a planet.
// MainMenu (Quick Start) or WorldCreator (New Game: Land button) set a pending
// config; GameLoadingScene takes it and builds the gameplay world from it.
// Mirrors the GameWorldState SetPending()/Take() pattern.

#include <worldgen/data/GeneratedWorld.h>

#include <memory>

namespace world_sim {

// The prebuilt Quick Start planet, resolved next to the executable via
// Foundation::findResource. Generated at build time by the `quickstart-planet`
// CMake target (n=2048, seed 424242) and shipped in the package — players never
// generate it. The main menu disables Quick Start when this file is absent;
// GameLoadingScene loads it (no in-game generation fallback).
inline constexpr const char* kQuickstartPlanetResource =
	"assets/planets/quickstart.wsplanet";

struct GameStartConfig {
	// QuickStart: GameLoadingScene loads the prebuilt shipped planet and picks
	// the default landing site itself.
	// NewGame: world and landing site are filled in by the creator flow.
	enum class Source { QuickStart, NewGame };

	Source source{Source::QuickStart};
	std::shared_ptr<const worldgen::GeneratedWorld> world;
	double landingLatDeg{0.0};
	double landingLonDeg{0.0};

	static void SetPending(std::unique_ptr<GameStartConfig> config);
	static std::unique_ptr<GameStartConfig> Take();
	static bool HasPending();

  private:
	static std::unique_ptr<GameStartConfig> s_pending;
};

} // namespace world_sim

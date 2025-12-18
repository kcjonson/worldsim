#pragma once

#include "../ISystem.h"

namespace ecs {

/// Decays all needs over time based on game-time scaling.
/// Priority: 50 (runs early, before movement decisions)
class NeedsDecaySystem : public ISystem {
  public:
    void update(float deltaTime) override;

    [[nodiscard]] int priority() const override { return 50; }
    [[nodiscard]] const char* name() const override { return "NeedsDecay"; }

    /// Set the game time scale (game-minutes per real-second)
    /// Default: 1.0 = 1 real second equals 1 game minute
    void setTimeScale(float gameMinutesPerSecond) { gameTimeScale = gameMinutesPerSecond; }

    [[nodiscard]] float timeScale() const { return gameTimeScale; }

  private:
    float gameTimeScale = 1.0f;  // game-minutes per real-second
};

}  // namespace ecs

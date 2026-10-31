#pragma once

namespace ecs {

/// Tag component marking a colonist as under direct player control. While present, the
/// AIDecisionSystem skips task selection (the colonist stands and waits) and the player
/// drives movement via right-click walk orders. Vision/discovery is unaffected.
struct PlayerControlled {};

}  // namespace ecs

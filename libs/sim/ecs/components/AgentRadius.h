#pragma once

namespace ecs {

// Physical footprint for a mobile agent. Used by CollisionSystem to keep
// agents from occupying the same space. invMass == 0 means immovable (pinned).
struct AgentRadius {
    float radiusMeters = 0.3f;
    float invMass      = 1.0f;
};

} // namespace ecs

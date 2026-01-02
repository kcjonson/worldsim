#pragma once

// Task Chain Definition
// Defines multi-step task chains loaded from XML configuration.
// Chains link related steps with a shared identifier so colonists get
// priority bonuses for completing sequences (e.g., Pickup → Deposit).

#include <cstdint>
#include <string>
#include <vector>

namespace engine::assets {

/// A single step in a task chain
struct ChainStep {
    /// Step order (0-based)
    uint8_t order = 0;

    /// Action defName to execute (references ActionTypeRegistry)
    std::string actionDefName;

    /// Target identifier (e.g., "source", "destination", "station")
    std::string target;

    /// If true, this step can be skipped
    bool optional = false;

    /// If true, this step cannot start unless previous step completed
    /// Used to prevent depositing before picking up
    bool requiresPreviousStep = true;
};

/// Definition of a task chain (multi-step task sequence)
/// Loaded from assets/config/work/task-chains.xml
struct TaskChainDef {
    /// Unique identifier (e.g., "Chain_PickupDeposit")
    std::string defName;

    /// Human-readable name
    std::string label;

    /// Description of what this chain does
    std::string description;

    /// Ordered steps in the chain
    std::vector<ChainStep> steps;

    /// Get step by order, or nullptr if not found
    [[nodiscard]] const ChainStep* getStep(uint8_t order) const {
        for (const auto& step : steps) {
            if (step.order == order) {
                return &step;
            }
        }
        return nullptr;
    }

    /// Get the next step after the given order, or nullptr if none
    [[nodiscard]] const ChainStep* getNextStep(uint8_t currentOrder) const {
        return getStep(currentOrder + 1);
    }

    /// Get total number of steps
    [[nodiscard]] size_t stepCount() const { return steps.size(); }
};

} // namespace engine::assets

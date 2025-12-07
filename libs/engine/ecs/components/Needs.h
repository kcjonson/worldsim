#pragma once

#include <array>
#include <cstdint>

namespace ecs {

/// MVP needs: Hunger, Thirst, Energy, Bladder
enum class NeedType : uint8_t {
    Hunger = 0,
    Thirst,
    Energy,
    Bladder,
    Count  // Sentinel for array sizing
};

/// Individual need state
struct Need {
    float value = 100.0f;         // Current value 0-100%
    float decayRate = 1.0f;       // Percent per game-minute
    float seekThreshold = 50.0f;  // AI seeks fulfillment below this
    float criticalThreshold = 10.0f;  // Emergency behavior below this

    /// Check if need is below seek threshold
    [[nodiscard]] bool needsAttention() const { return value < seekThreshold; }

    /// Check if need is critical
    [[nodiscard]] bool isCritical() const { return value < criticalThreshold; }

    /// Apply decay over time (clamped to 0)
    void decay(float gameMinutes) {
        value -= decayRate * gameMinutes;
        if (value < 0.0f) {
            value = 0.0f;
        }
    }

    /// Restore need (clamped to 100)
    void restore(float amount) {
        value += amount;
        if (value > 100.0f) {
            value = 100.0f;
        }
    }
};

/// Component containing all needs for an entity
struct NeedsComponent {
    std::array<Need, static_cast<size_t>(NeedType::Count)> needs;

    /// Access need by type
    [[nodiscard]] Need& get(NeedType type) {
        return needs[static_cast<size_t>(type)];
    }

    [[nodiscard]] const Need& get(NeedType type) const {
        return needs[static_cast<size_t>(type)];
    }

    /// Convenience accessors
    [[nodiscard]] Need& hunger() { return get(NeedType::Hunger); }
    [[nodiscard]] Need& thirst() { return get(NeedType::Thirst); }
    [[nodiscard]] Need& energy() { return get(NeedType::Energy); }
    [[nodiscard]] Need& bladder() { return get(NeedType::Bladder); }

    [[nodiscard]] const Need& hunger() const { return get(NeedType::Hunger); }
    [[nodiscard]] const Need& thirst() const { return get(NeedType::Thirst); }
    [[nodiscard]] const Need& energy() const { return get(NeedType::Energy); }
    [[nodiscard]] const Need& bladder() const { return get(NeedType::Bladder); }

    /// Create with default MVP configuration
    static NeedsComponent createDefault() {
        NeedsComponent comp;

        // Hunger: ~50% seek, ~10% critical, moderate decay
        comp.hunger() = Need{100.0f, 0.8f, 50.0f, 10.0f};

        // Thirst: ~50% seek, ~10% critical, faster decay than hunger
        comp.thirst() = Need{100.0f, 1.2f, 50.0f, 10.0f};

        // Energy: ~30% seek, ~10% critical (need sleep earlier)
        comp.energy() = Need{100.0f, 0.5f, 30.0f, 10.0f};

        // Bladder: ~30% seek, ~10% critical (accelerated by drinking)
        comp.bladder() = Need{100.0f, 0.3f, 30.0f, 10.0f};

        return comp;
    }

    /// Find the most urgent need (lowest value below seek threshold)
    /// Returns NeedType::Count if no needs require attention
    [[nodiscard]] NeedType mostUrgentNeed() const {
        NeedType urgent = NeedType::Count;
        float lowestValue = 100.0f;

        for (size_t i = 0; i < static_cast<size_t>(NeedType::Count); ++i) {
            const auto& need = needs[i];
            if (need.needsAttention() && need.value < lowestValue) {
                lowestValue = need.value;
                urgent = static_cast<NeedType>(i);
            }
        }

        return urgent;
    }
};

}  // namespace ecs

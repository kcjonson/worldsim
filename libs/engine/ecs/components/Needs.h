#pragma once

#include <array>
#include <cstdint>

namespace ecs {

/// Needs: Hunger, Thirst, Energy, Bladder, Digestion, Hygiene, Recreation, Temperature
enum class NeedType : uint8_t {
    Hunger = 0,
    Thirst,
    Energy,
    Bladder,    // Filled by drinking, relieved by peeing
    Digestion,  // Filled by eating, relieved by pooping
    Hygiene,    // Cleanliness / washing
    Recreation, // Fun / leisure
    Temperature, // Thermal comfort placeholder (environmental)
    Count  // Sentinel for array sizing
};

/// Human-readable labels for each need type (for UI display)
constexpr std::array<const char*, static_cast<size_t>(NeedType::Count)> kNeedLabels = {
    "Hunger", "Thirst", "Energy", "Bladder", "Digestion", "Hygiene", "Recreation", "Temperature"
};

/// Get the human-readable label for a need type
[[nodiscard]] inline const char* needLabel(NeedType type) {
    auto index = static_cast<size_t>(type);
    return (index < kNeedLabels.size()) ? kNeedLabels[index] : "Unknown";
}

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

    /// Actionable needs the AI can currently fulfill (others are tracked but not acted on yet)
    static constexpr std::array<NeedType, 5> kActionableNeeds = {
        NeedType::Hunger,
        NeedType::Thirst,
        NeedType::Energy,
        NeedType::Bladder,
        NeedType::Digestion,
    };

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
    [[nodiscard]] Need& digestion() { return get(NeedType::Digestion); }
    [[nodiscard]] Need& hygiene() { return get(NeedType::Hygiene); }
    [[nodiscard]] Need& recreation() { return get(NeedType::Recreation); }
    [[nodiscard]] Need& temperature() { return get(NeedType::Temperature); }

    [[nodiscard]] const Need& hunger() const { return get(NeedType::Hunger); }
    [[nodiscard]] const Need& thirst() const { return get(NeedType::Thirst); }
    [[nodiscard]] const Need& energy() const { return get(NeedType::Energy); }
    [[nodiscard]] const Need& bladder() const { return get(NeedType::Bladder); }
    [[nodiscard]] const Need& digestion() const { return get(NeedType::Digestion); }
    [[nodiscard]] const Need& hygiene() const { return get(NeedType::Hygiene); }
    [[nodiscard]] const Need& recreation() const { return get(NeedType::Recreation); }
    [[nodiscard]] const Need& temperature() const { return get(NeedType::Temperature); }

    /// Create with default MVP configuration
    /// Decay rates are percent per game-minute (reduced by 10x for playable pacing)
    static NeedsComponent createDefault() {
        NeedsComponent comp;

        // Hunger: ~50% seek, ~10% critical, moderate decay
        comp.hunger() = Need{100.0f, 0.08f, 50.0f, 10.0f};

        // Thirst: ~50% seek, ~10% critical, faster decay than hunger
        comp.thirst() = Need{100.0f, 0.12f, 50.0f, 10.0f};

        // Energy: ~30% seek, ~10% critical (need sleep earlier)
        comp.energy() = Need{100.0f, 0.05f, 30.0f, 10.0f};

        // Bladder: ~30% seek, ~10% critical (filled by drinking, relieved by peeing)
        comp.bladder() = Need{100.0f, 0.03f, 30.0f, 10.0f};

        // Digestion: ~30% seek, ~10% critical (filled by eating, relieved by pooping)
        // Decay rate is lower than bladder's, meaning digestion depletes more slowly
        // (food takes longer to process than liquids)
        comp.digestion() = Need{100.0f, 0.02f, 30.0f, 10.0f};

        // Hygiene: ~40% seek, ~15% critical (washing deferred, keep decay modest for now)
        comp.hygiene() = Need{100.0f, 0.015f, 40.0f, 15.0f};

        // Recreation: ~30% seek, ~10% critical (leisure deferred, modest decay)
        comp.recreation() = Need{100.0f, 0.01f, 30.0f, 10.0f};

        // Temperature: placeholder tracked value (no decay until environment model plugs in)
        comp.temperature() = Need{100.0f, 0.0f, 40.0f, 15.0f};

        return comp;
    }

    /// Find the most urgent need (lowest value below seek threshold)
    /// Returns NeedType::Count if no needs require attention
    [[nodiscard]] NeedType mostUrgentNeed() const {
        NeedType urgent = NeedType::Count;
        float lowestValue = 100.0f;

        for (auto needType : kActionableNeeds) {
            const auto& need = get(needType);
            if (need.needsAttention() && need.value < lowestValue) {
                lowestValue = need.value;
                urgent = needType;
            }
        }

        return urgent;
    }
};

}  // namespace ecs

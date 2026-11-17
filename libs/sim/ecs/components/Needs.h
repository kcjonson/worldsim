#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ecs {

	/// Needs: Hunger, Thirst, Energy, Bladder, Digestion, Hygiene, Recreation, Temperature
	enum class NeedType : uint8_t {
		Hunger = 0,
		Thirst,
		Energy,
		Bladder,	 // Filled by drinking, relieved by peeing
		Digestion,	 // Filled by eating, relieved by pooping
		Hygiene,	 // Cleanliness / washing
		Recreation,	 // Fun / leisure
		Temperature, // Thermal comfort placeholder (environmental)
		Count		 // Sentinel for array sizing
	};

	/// Human-readable labels for each need type (for UI display)
	constexpr std::array<const char*, static_cast<size_t>(NeedType::Count)> kNeedLabels =
		{"Hunger", "Thirst", "Energy", "Bladder", "Digestion", "Hygiene", "Recreation", "Temperature"};

	/// Get the human-readable label for a need type
	[[nodiscard]] inline const char* needLabel(NeedType type) {
		auto index = static_cast<size_t>(type);
		return (index < kNeedLabels.size()) ? kNeedLabels[index] : "Unknown";
	}

	class NeedLevels;

	/// One need: its value and decay, and its thresholds from data (NeedLevels). See
	/// /docs/technical/interruptable-crafts.md (Needs in three levels).
	struct Need {
		float value;			 // Current value 0-100%
		float decayRate;		 // Percent per game-minute
		float seekThreshold;	 // AI seeks fulfillment below this ("slightly hungry")
		float urgentThreshold;	 // Interrupts work below this ("very hungry")
		float criticalThreshold; // Emergency behavior below this

		/// Check if need is below seek threshold
		[[nodiscard]] bool needsAttention() const { return value < seekThreshold; }

		/// Check if need is critical
		[[nodiscard]] bool isCritical() const { return value < criticalThreshold; }

		/// 0 at or above the seek threshold, and one more below each of seek, urgent, and critical.
		[[nodiscard]] std::size_t level() const {
			return static_cast<std::size_t>(value < seekThreshold) + static_cast<std::size_t>(value < urgentThreshold) +
				   static_cast<std::size_t>(value < criticalThreshold);
		}

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
		/// Every need full, at its decay rate, with its thresholds from `levels`.
		explicit NeedsComponent(const NeedLevels& levels);

		std::array<Need, static_cast<size_t>(NeedType::Count)> needs{};

		/// Actionable needs the AI can currently fulfill (others are tracked but not acted on yet)
		static constexpr std::array<NeedType, 5> kActionableNeeds = {
			NeedType::Hunger,
			NeedType::Thirst,
			NeedType::Energy,
			NeedType::Bladder,
			NeedType::Digestion,
		};

		/// Access need by type
		[[nodiscard]] Need& get(NeedType type) { return needs[static_cast<size_t>(type)]; }

		[[nodiscard]] const Need& get(NeedType type) const { return needs[static_cast<size_t>(type)]; }

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

		/// Find the most urgent need (lowest value below seek threshold)
		/// Returns NeedType::Count if no needs require attention
		[[nodiscard]] NeedType mostUrgentNeed() const {
			NeedType urgent = NeedType::Count;
			float	 lowestValue = 100.0f;

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

} // namespace ecs

#pragma once

// A placed item's turn as a whole number of quanta (docs/technical/rendering/rotatable-placed-items.md, section 3).

#include <cstdint>

namespace assets {

	/// A placed item's turn is a whole number of these. Part of the stored format, not tuning.
	inline constexpr double kTurnQuantumDegrees = 0.01;
	/// Quanta in a whole turn: a turn is k in [0, kTurnQuantaPerTurn).
	inline constexpr uint32_t kTurnQuantaPerTurn = 36000;

	/// A turn in degrees clockwise rounded to the nearest quantum and then wrapped into [0, 36000), in that
	/// order, so 359.996 degrees is quantum 0.
	[[nodiscard]] uint32_t turnQuantaFromDegrees(double degrees);

	/// A turn's radians, k times the quantum, computed in double and stored as float.
	[[nodiscard]] float turnRadiansFromQuanta(uint32_t turnQuanta);

	/// The turn an entity's ecs::Rotation stands for: its radians, positive from +X toward +Y (clockwise on the
	/// screen), in degrees, rounded to the quantum and wrapped as turnQuantaFromDegrees does.
	[[nodiscard]] uint32_t turnQuantaFromRadians(float radians);

} // namespace assets

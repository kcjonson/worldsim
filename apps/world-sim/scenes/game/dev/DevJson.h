#pragma once

// The JSON pieces the dev views share: string escaping, and a measure to three decimals.

#include <string>

namespace world_sim {

	/// `text` with the characters a JSON string may not hold escaped.
	std::string jsonEscape(const std::string& text);

	/// A measure (meters, square meters) to three decimals; `null` for NaN and infinity, which JSON cannot hold.
	std::string jsonNumber(double value);

} // namespace world_sim

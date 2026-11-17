#pragma once

// The JSON pieces the dev views share: string escaping, and a measure to the millimeter.

#include <string>

namespace world_sim {

	/// `text` with the characters a JSON string may not hold escaped.
	std::string jsonEscape(const std::string& text);

	/// A measure (meters, square meters) to three decimals, the millimeter.
	std::string jsonNumber(double value);

} // namespace world_sim

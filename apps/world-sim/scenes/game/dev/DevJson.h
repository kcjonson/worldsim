#pragma once

// The JSON pieces the dev views share: string escaping, a list of strings, and a measure to three decimals.

#include <string>
#include <vector>

namespace world_sim {

	/// `text` with the characters a JSON string may not hold escaped.
	std::string jsonEscape(const std::string& text);

	/// `texts` as a JSON array of strings.
	std::string jsonStrings(const std::vector<std::string>& texts);

	/// A measure (meters, square meters) to three decimals; `null` for NaN and infinity, which JSON cannot hold.
	std::string jsonNumber(double value);

} // namespace world_sim

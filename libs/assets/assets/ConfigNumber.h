#pragma once

#include <charconv>
#include <optional>
#include <string_view>

namespace assets {

	/// A config attribute's whole text as a number: nothing for empty text or text that is not entirely a
	/// number, so "30abc" is refused rather than read as 30.
	[[nodiscard]] inline std::optional<float> parseConfigNumber(std::string_view text) {
		float number = 0.0F;
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
		if (text.empty() || error != std::errc() || end != text.data() + text.size()) {
			return std::nullopt;
		}
		return number;
	}

} // namespace assets

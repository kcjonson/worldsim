#pragma once

#include <charconv>
#include <cmath>
#include <optional>
#include <string_view>

namespace assets {

	/// A config attribute's whole text as a finite number, a leading '+' allowed: nothing for empty text,
	/// text that is not entirely a number ("30abc" is refused rather than read as 30), or nan and inf.
	[[nodiscard]] inline std::optional<float> parseConfigNumber(std::string_view text) {
		if (text.starts_with('+')) {
			text.remove_prefix(1);
			if (text.starts_with('-') || text.starts_with('+')) {
				return std::nullopt;
			}
		}
		float number = 0.0F;
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
		if (text.empty() || error != std::errc() || end != text.data() + text.size() || !std::isfinite(number)) {
			return std::nullopt;
		}
		return number;
	}

} // namespace assets

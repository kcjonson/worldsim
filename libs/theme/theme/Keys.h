#pragma once

// One key of an InputSnapshot: theme::key(frame.input, foundation::Key::Escape).pressed().

#include <input/InputTypes.h>

#include <cstddef>

namespace theme {

	inline const foundation::ButtonState& key(const foundation::InputSnapshot& input, foundation::Key which) {
		return input.keys.at(static_cast<std::size_t>(which));
	}

} // namespace theme

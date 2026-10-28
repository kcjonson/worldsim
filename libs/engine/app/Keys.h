#pragma once

// One key of an InputSnapshot: engine::key(frame.input, foundation::Key::Escape).pressed().

#include <input/InputTypes.h>

#include <cstddef>

namespace engine {

	inline const foundation::ButtonState& key(const foundation::InputSnapshot& input, foundation::Key which) {
		return input.keys.at(static_cast<std::size_t>(which));
	}

} // namespace engine

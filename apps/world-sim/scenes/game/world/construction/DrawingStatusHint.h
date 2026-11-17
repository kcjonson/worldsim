#pragma once

// The one place that decides the hint line on the drawing tools' status strip: what to do next, or
// why the cursor's placement is refused. The strip draws it and `what=drawing` reports it.

#include "scenes/game/world/construction/DrawingSystem.h"

#include <string>

namespace world_sim {

	enum class StatusHintTone { Ok, Warn, Crit };

	struct StatusHint {
		std::string	   text;
		StatusHintTone tone = StatusHintTone::Ok;
	};

	[[nodiscard]] StatusHint statusHint(const DrawingStatus& status);

} // namespace world_sim

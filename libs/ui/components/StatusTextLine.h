#pragma once

// StatusTextLine - Task/action line with status-based styling.
//
// A text component that shows a status indicator and text:
// - Status indicator prefix ("> ", "  ", "x ")
// - Color coding based on status
// - Optional indent for hierarchy
//
// Implements IComponent for use in LayoutContainer.

#include "shapes/Shapes.h"
#include "theme/Theme.h"

#include <string>

namespace UI {

/// Status indicator for task/action display
enum class LineStatus {
	Active,   ///< Currently executing (green, "> " prefix)
	Pending,  ///< Waiting to execute (yellow)
	Idle,     ///< Not active (gray)
	Blocked,  ///< Cannot execute (red, "x " prefix)
	Available ///< Could execute (default, "  " prefix)
};

/// A status-colored text line for task/action display
class StatusTextLine : public Text {
  public:
	struct Args {
		std::string text;
		LineStatus status = LineStatus::Available;
		float fontSize = 11.0F;
		float indent = 8.0F;
		float margin = 0.0F;
		std::string id = "status_line";
	};

	explicit StatusTextLine(const Args& args)
		: Text(Text::Args{
			  .position = {args.indent, 0.0F},
			  .text = statusPrefix(args.status) + args.text,
			  .style = {
				  .color = statusColor(args.status),
				  .fontSize = args.fontSize,
				  .hAlign = Foundation::HorizontalAlign::Left,
				  .vAlign = Foundation::VerticalAlign::Top,
			  },
			  .id = args.id.c_str()
		  }),
		  currentStatus(args.status),
		  indentOffset(args.indent) {
		margin = args.margin;
	}

	/// Update the status and text
	void setStatus(LineStatus newStatus, const std::string& newText) {
		currentStatus = newStatus;
		text = statusPrefix(newStatus) + newText;
		style.color = statusColor(newStatus);
	}

	/// Get the current status
	[[nodiscard]] LineStatus getStatus() const { return currentStatus; }

  private:
	static std::string statusPrefix(LineStatus status) {
		switch (status) {
			case LineStatus::Active:
				return "> ";
			case LineStatus::Blocked:
				return "x ";
			case LineStatus::Pending:
			case LineStatus::Idle:
			case LineStatus::Available:
			default:
				return "  ";
		}
	}

	static Foundation::Color statusColor(LineStatus status) {
		switch (status) {
			case LineStatus::Active:
				return Theme::Colors::statusActive;
			case LineStatus::Pending:
				return Theme::Colors::statusPending;
			case LineStatus::Idle:
				return Theme::Colors::statusIdle;
			case LineStatus::Blocked:
				return Theme::Colors::statusBlocked;
			case LineStatus::Available:
			default:
				return Theme::Colors::textBody;
		}
	}

	LineStatus currentStatus;
	float indentOffset;
};

} // namespace UI

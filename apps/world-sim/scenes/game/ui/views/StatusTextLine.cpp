#include "StatusTextLine.h"

#include <ui/Context.h>

namespace world_sim {

	namespace {

		std::string prefixOf(LineStatus status) {
			switch (status) {
				case LineStatus::Active:
					return "> ";
				case LineStatus::Blocked:
					return "x ";
				case LineStatus::Pending:
				case LineStatus::Idle:
				case LineStatus::Available:
					break;
			}
			return "  ";
		}

		ui::ColorToken colorOf(LineStatus status) {
			switch (status) {
				case LineStatus::Active:
					return tokens::color::statusOk;
				case LineStatus::Pending:
					return tokens::color::statusWarn;
				case LineStatus::Idle:
					return tokens::color::textDim;
				case LineStatus::Blocked:
					return tokens::color::statusCrit;
				case LineStatus::Available:
					break;
			}
			return tokens::color::text;
		}

	} // namespace

	StatusTextLine::StatusTextLine(const Args& args)
		: ui::Text({.text = prefixOf(args.status) + args.text, .wrap = args.wrap}),
		  status(args.status),
		  fontSize(args.fontSize) {}

	void StatusTextLine::onMount() {
		given = getDeclaredStyle();
		tint();
	}

	void StatusTextLine::setStatus(LineStatus newStatus, const std::string& newText) {
		setText(prefixOf(newStatus) + newText);
		if (newStatus != status) {
			status = newStatus;
			tint();
		}
	}

	void StatusTextLine::tint() {
		if (getContext() != nullptr) {
			setStyle(world_sim::text(getContext()->getTheme(), {.size = fontSize, .color = colorOf(status)}).extend(given));
		}
	}

} // namespace world_sim

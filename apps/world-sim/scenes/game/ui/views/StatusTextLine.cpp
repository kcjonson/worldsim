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
					return look::t::color::statusOk;
				case LineStatus::Pending:
					return look::t::color::statusWarn;
				case LineStatus::Idle:
					return look::t::color::textDim;
				case LineStatus::Blocked:
					return look::t::color::statusCrit;
				case LineStatus::Available:
					break;
			}
			return look::t::color::text;
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
			setStyle(look::text(getContext()->getTheme(), colorOf(status), fontSize).extend(given));
		}
	}

} // namespace world_sim

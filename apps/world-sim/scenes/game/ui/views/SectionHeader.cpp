#include "SectionHeader.h"

#include <ui/Context.h>

namespace world_sim {

	SectionHeader::SectionHeader(const Args& args)
		: ui::Text({.text = args.text}),
		  fontSize(args.fontSize),
		  color(args.color) {}

	void SectionHeader::onMount() {
		setStyle(look::text(getContext()->getTheme(), color, fontSize).extend(getDeclaredStyle()));
	}

} // namespace world_sim

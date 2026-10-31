#include "theme/Theme.h"

#include "theme/Fonts.h"

#include <theme/Icons.h>
#include <theme/Tokens.h>

#include <ui/theme/Theme.h>
#include <utils/Log.h>

#include <array>
#include <utility>

namespace theme {

	namespace {
		constexpr std::array<ui::FontFamilyName, 3> kFontFamilyNames{{
			{.name = "display", .family = fontDisplay},
			{.name = "body", .family = fontUi},
			{.name = "mono", .family = fontMono},
		}};
	} // namespace

	void apply(ui::Context& context) {
		ui::ThemeLoad load = ui::Theme::load({.tokens = &tokenTable(), .fontFamilies = kFontFamilyNames});
		for (const ui::dtcg::Diagnostic& diagnostic : load.diagnostics) {
			LOG_WARNING(UI, "theme: %s: %s", diagnostic.path.c_str(), diagnostic.message.c_str());
		}
		context.setTheme(std::move(load.theme));
		context.icons().add(iconAtlas());
	}

} // namespace theme

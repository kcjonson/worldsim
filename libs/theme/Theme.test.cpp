#include <theme/Icons.h>
#include <theme/Theme.h>
#include <theme/Tokens.h>

#include <gtest/gtest.h>
#include <ui/theme/Theme.h>
#include <ui/theme/Tokens.h>

#include <algorithm>
#include <string_view>

namespace {

	// Worldsim's theme loads over ui's, under the game's font families, with nothing wrong or missing, and ui's
	// roles read as worldsim's values.
	TEST(Theme, LoadsOverUisTokens) {
		const ui::ThemeLoad load =
			ui::Theme::load({.tokens = &theme::tokenTable(), .fontFamilies = theme::fontFamilyNames(), .strict = true});
		for (const ui::dtcg::Diagnostic& diagnostic : load.diagnostics) {
			ADD_FAILURE() << diagnostic.path << ": " << diagnostic.message;
		}
		ASSERT_TRUE(load.ok());

		const Foundation::Color raised = load.theme.get(ui::tokens::color::surface::raised);
		const Foundation::Color panel = load.theme.get(theme::tokens::theme::color::bgPanel);
		EXPECT_FLOAT_EQ(raised.r, panel.r);
		EXPECT_FLOAT_EQ(raised.g, panel.g);
		EXPECT_FLOAT_EQ(raised.b, panel.b);
		EXPECT_FLOAT_EQ(raised.a, panel.a);
		EXPECT_FLOAT_EQ(load.theme.get(ui::tokens::motion::duration::fast), 120.0F);
		EXPECT_FLOAT_EQ(load.theme.get(ui::tokens::radius::sm), 2.0F);
	}

	// The line set, plusBold and the ten asset icons, three of them renamed past the line set's names.
	TEST(ThemeIcons, NameEveryIcon) {
		const ui::IconAtlasData& atlas = theme::iconAtlas();
		EXPECT_EQ(atlas.names.size(), 62U);
		for (const std::string_view name : {"gear", "chevronDown", "plusBold", "pause", "pause_solid", "zoom_reset"}) {
			EXPECT_TRUE(std::ranges::any_of(atlas.names, [&](const ui::IconName& icon) { return icon.name == name; })) << name;
		}
		EXPECT_FALSE(atlas.png.empty());
	}

} // namespace

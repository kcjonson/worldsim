// Worldsim's components on salal's ui, headless under worldsim's theme: their structure and their tint.

#include "theme/Avatar.h"
#include "theme/Panel.h"

#include <theme/DesignTokens.h>

#include <gtest/gtest.h>
#include <ui/Context.h>
#include <ui/theme/Theme.h>

#include <cstddef>

namespace theme {

	namespace {

		namespace t = tokens::theme;

		void loadWorldsimTheme(ui::Context& context) {
			context.setTheme(ui::Theme::load({.tokens = &tokenTable(), .strict = true}).theme);
		}

		ui::Component& child(const ui::Context& context, const ui::Component& parent, std::size_t index) {
			return *context.find(parent.getChildren().at(index));
		}

		void expectColor(const Foundation::Color& actual, const Foundation::Color& expected) {
			EXPECT_FLOAT_EQ(actual.r, expected.r);
			EXPECT_FLOAT_EQ(actual.g, expected.g);
			EXPECT_FLOAT_EQ(actual.b, expected.b);
			EXPECT_FLOAT_EQ(actual.a, expected.a);
		}

	} // namespace

	// A header with the kicker over the title, a hairline, the body under them grown to the foot, and
	// eight ticks in the accent; raised takes the raised surface.
	TEST(ThemePanel, LaysOutItsHeaderBodyAndTicks) {
		ui::Context context({800.0F, 600.0F});
		loadWorldsimTheme(context);
		const ui::Theme& theme = context.getTheme();
		ui::Ref<Panel>	 panel = context.root().addChild<Panel>(
			  {.size = {300.0F, 200.0F}, .title = "Crew", .kicker = "Sector 7", .variant = PanelVariant::Raised, .accent = PanelAccent::Data}
		  );
		context.layout();

		const Panel& shown = *panel.get();
		ASSERT_EQ(shown.getChildren().size(), 11U); // header, hairline, body, eight ticks
		EXPECT_EQ(child(context, shown, 0).getChildren().size(), 2U);
		const ui::Component& body = *shown.body().get();
		EXPECT_GT(body.getBounds().y, 0.0F);
		EXPECT_FLOAT_EQ(body.getBounds().y + body.getSize().y, 200.0F);
		expectColor(*child(context, shown, 3).getStyle().backgroundColor, theme.get(t::color::data));
		expectColor(*shown.getStyle().backgroundColor, theme.get(t::color::bgPanelRaised));
		EXPECT_TRUE(shown.getStyle().boxShadow.has_value());

		// The bottom-right tick's legs end at the panel's corner.
		const ui::Component& legAcross = child(context, shown, 9);
		EXPECT_FLOAT_EQ(legAcross.getBounds().x + legAcross.getSize().x, 300.0F);
		EXPECT_FLOAT_EQ(legAcross.getBounds().y + legAcross.getSize().y, 200.0F);
	}

	// The seed fixes the hue and the initials; the ring follows the mood, and selected adds the outline.
	TEST(ThemeAvatar, TakesItsLookFromTheSeedAndMood) {
		EXPECT_EQ(Avatar::initials("kai okafor reyes"), "KO");
		EXPECT_EQ(Avatar::initials("Mara"), "M");
		EXPECT_EQ(Avatar::hash("Kai Okafor"), Avatar::hash("Kai Okafor"));
		EXPECT_NE(Avatar::hash("Kai Okafor"), Avatar::hash("Mara Voss"));

		ui::Context context({800.0F, 600.0F});
		loadWorldsimTheme(context);
		const ui::Theme& theme = context.getTheme();
		ui::Ref<Avatar>	 low = context.root().addChild<Avatar>({.seed = "Kai Okafor", .mood = 0.2F});
		ui::Ref<Avatar>	 neutral = context.root().addChild<Avatar>({.size = {60.0F, 60.0F}, .seed = "Mara Voss", .selected = true});
		context.layout();

		EXPECT_FLOAT_EQ(low.get()->getSize().x, 44.0F);
		expectColor(*low.get()->getStyle().borderColor, theme.get(t::color::statusCrit));
		EXPECT_TRUE(low.get()->getStyle().boxShadow.has_value());
		expectColor(*neutral.get()->getStyle().borderColor, theme.get(t::color::lineEdge));
		EXPECT_FALSE(neutral.get()->getStyle().boxShadow.has_value());
		// The disc, two initials, two tick legs, and for the selected one the outline.
		EXPECT_EQ(low.get()->getChildren().size(), 5U);
		EXPECT_EQ(neutral.get()->getChildren().size(), 6U);
		EXPECT_FLOAT_EQ(child(context, *neutral.get(), 0).getSize().x, 60.0F * 0.68F);
	}

} // namespace theme

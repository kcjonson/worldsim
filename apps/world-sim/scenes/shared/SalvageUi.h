#pragma once

// What world-sim's scenes on salal's ui share. enterSalvageUi, from a scene's onEnter, takes the input
// back from the bridge for App's own dispatch and gives the scene's ui worldsim's theme and icons.
// Tokens are worldsim's own design tokens, read through the scene's theme; text() turns a type role
// into a style. Diamond is the mark the title blocks carry, which salal's shapes haven't got.

#include <application/App.h>
#include <theme/DesignTokens.h>
#include <ui/Context.h>
#include <ui/shapes/Text.h>
#include <ui/theme/Tokens.h>

#include <optional>

namespace world_sim {

	namespace tokens = ::theme::tokens::theme;

	void enterSalvageUi(app::App& app, ui::Context& context);

	// A type role: a family, a size and a colour from the tokens, tracked by `tracking` em of the size.
	struct Type {
		ui::FontFamilyToken			   family = ui::tokens::font::family::body;
		ui::DimensionToken			   size = tokens::fontSize::fsMd;
		ui::ColorToken				   color = tokens::color::text;
		std::optional<ui::NumberToken> tracking;
		Foundation::TextTransform	   transform = Foundation::TextTransform::None;
		Foundation::HorizontalAlign	   align = Foundation::HorizontalAlign::Left;
	};

	[[nodiscard]] ui::Style text(const ui::Theme& theme, const Type& type);

	// A square turned a quarter in its box's middle, `radius` px from centre to point, in the accent.
	class Diamond : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			float radius = 11.0F;
		};

		explicit Diamond(const Args& args)
			: radius(args.radius) {}

		[[nodiscard]] const char* typeName() const override { return "Diamond"; }

	  protected:
		void										  render() override;
		[[nodiscard]] std::optional<Foundation::Vec2> intrinsicSize(std::optional<float> width, float availableWidth) const override;

	  private:
		float radius;
	};

} // namespace world_sim

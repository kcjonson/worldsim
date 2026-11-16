#pragma once

// Worldsim's Panel (docs/design/ui/design-system/components.md, Panel) on salal's ui: a surface in one of
// three variants, a header with a mono kicker over an upper-case title and a hairline under it, a body
// slot, and L-bracket ticks at the corners in the accent. salal's Panel has no room for a kicker over
// its title, so this one lays out its own header. It reads worldsim's tokens, so its scene's theme loads
// theme::tokenTable().

#include <graphics/Color.h>
#include <ui/shapes/Rectangle.h>

#include <cstdint>
#include <optional>
#include <string>

namespace theme {

	enum class PanelVariant : std::uint8_t { Panel, Raised, Inset };

	// The ticks' and the kicker's colour.
	enum class PanelAccent : std::uint8_t { Accent, Data, None };

	class Panel : public ui::Rectangle {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string						 title;	 // empty, with no kicker: no header
			std::string						 kicker; // the eyebrow over the title
			PanelVariant					 variant = PanelVariant::Panel;
			PanelAccent						 accent = PanelAccent::Accent;
			bool							 corners = true;
			bool							 compact = false; // denser padding, for the HUD
			bool							 flush = false;	  // a body with no padding
			std::optional<Foundation::Color> glow;			  // a halo in this colour, instead of the raised drop shadow
		};

		explicit Panel(const Args& args);

		// Where the content goes: a column inside the body's padding, grown to the panel's foot.
		[[nodiscard]] ui::Ref<ui::Component> body() const { return bodySlot; }

		// Tints again, so a theme change reaches the variant.
		void setStyle(const ui::Style& value) override;

		[[nodiscard]] const char* typeName() const override { return "theme::Panel"; }
		[[nodiscard]] ui::StyleId themeStyle() const override;

	  protected:
		void onMount() override;

	  private:
		void tint();

		std::string						 title;
		std::string						 kicker;
		PanelVariant					 variant;
		PanelAccent						 accent;
		bool							 corners;
		bool							 compact;
		bool							 flush;
		std::optional<Foundation::Color> glow;
		ui::Ref<ui::Component>			 bodySlot;
	};

} // namespace theme

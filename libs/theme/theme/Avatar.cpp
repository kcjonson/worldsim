#include "theme/Avatar.h"

#include <theme/Tokens.h>

#include <ui/Context.h>
#include <ui/shapes/Circle.h>
#include <ui/shapes/Text.h>
#include <ui/theme/Tokens.h>

#include <cmath>
#include <sstream>

namespace theme {

	namespace {

		namespace t = tokens::theme;

		constexpr float kDefaultSide = 44.0F;
		constexpr float kDiscShare = 0.34F; // of the side: the disc's radius and the initials' size
		constexpr float kTickLeg = 5.0F;
		constexpr float kTickInset = 3.0F;

		// h in degrees, s and l 0 to 1.
		Foundation::Color hsl(float h, float s, float l) {
			const float c = (1.0F - std::abs((2.0F * l) - 1.0F)) * s;
			const float hp = h / 60.0F;
			const float x = c * (1.0F - std::abs(std::fmod(hp, 2.0F) - 1.0F));
			float		r = 0.0F;
			float		g = 0.0F;
			float		b = 0.0F;
			if (hp < 1.0F) {
				r = c;
				g = x;
			} else if (hp < 2.0F) {
				r = x;
				g = c;
			} else if (hp < 3.0F) {
				g = c;
				b = x;
			} else if (hp < 4.0F) {
				g = x;
				b = c;
			} else if (hp < 5.0F) {
				r = x;
				b = c;
			} else {
				r = c;
				b = x;
			}
			const float m = l - (c * 0.5F);
			return {r + m, g + m, b + m, 1.0F};
		}

		Foundation::Color ringColor(const ui::Theme& theme, std::optional<float> mood) {
			if (!mood.has_value()) {
				return theme.get(t::color::lineEdge);
			}
			if (*mood < 0.3F) {
				return theme.get(t::color::statusCrit);
			}
			if (*mood < 0.55F) {
				return theme.get(t::color::statusWarn);
			}
			return theme.get(t::color::statusOk);
		}

	} // namespace

	Avatar::Avatar(const Args& args)
		: seed(args.seed),
		  mood(args.mood),
		  selected(args.selected) {}

	std::uint32_t Avatar::hash(const std::string& seed) {
		std::uint32_t h = 2166136261U;
		for (const char c : seed) {
			h ^= static_cast<std::uint8_t>(c);
			h *= 16777619U;
		}
		return h;
	}

	std::string Avatar::initials(const std::string& seed) {
		std::string		   out;
		std::istringstream words(seed);
		std::string		   word;
		while (out.size() < 2 && words >> word) {
			char first = word.front();
			if (first >= 'a' && first <= 'z') {
				first = static_cast<char>(first - ('a' - 'A'));
			}
			out.push_back(first);
		}
		return out;
	}

	void Avatar::onMount() {
		const ui::Theme& theme = getContext()->getTheme();
		ui::Size		 box = getSizeSpec();
		const float		 side = box.width.unit == ui::Length::Unit::Px ? box.width.value : kDefaultSide;
		setSize({side, side});

		const float hue = static_cast<float>(hash(seed) % 360U);
		const float radius = side * kDiscShare;
		addChild<ui::Circle>(
			{.position = {(side * 0.5F) - radius, (side * 0.5F) - radius},
			 .size = {radius * 2.0F, radius * 2.0F},
			 .style = {.backgroundColor = hsl(hue, 0.45F, 0.24F)}}
		);

		const std::string letters = initials(seed);
		if (!letters.empty()) {
			const ui::Style text{
				.fontFamily = theme.get(ui::tokens::font::family::display),
				.fontSize = side * kDiscShare,
				.textAlign = Foundation::HorizontalAlign::Center,
				.verticalAlign = Foundation::VerticalAlign::Middle,
			};
			addChild<ui::Text>(
				{.position = {1.0F, 1.0F},
				 .size = {side, side},
				 .style = text.extend({.color = theme.get(t::color::bgVoid)}),
				 .text = letters}
			);
			addChild<ui::Text>({.size = {side, side}, .style = text.extend({.color = hsl(hue, 0.55F, 0.82F)}), .text = letters});
		}

		// The dossier tick, top right.
		const float		thin = theme.get(t::border::bw);
		const ui::Style faint{.backgroundColor = theme.get(t::color::textFaint)};
		addChild<ui::Rectangle>({.position = {side - kTickInset - kTickLeg, kTickInset}, .size = {kTickLeg, thin}, .style = faint});
		addChild<ui::Rectangle>({.position = {side - kTickInset - thin, kTickInset}, .size = {thin, kTickLeg}, .style = faint});

		// Selected: an accent outline a pixel outside the frame.
		outline = addChild<ui::Rectangle>(
			{.visible = selected,
			 .position = {-2.0F * thin, -2.0F * thin},
			 .size = {side + (4.0F * thin), side + (4.0F * thin)},
			 .style = {
				 .backgroundColor = Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F),
				 .borderColor = theme.get(t::color::accent),
				 .borderWidth = thin,
				 .borderRadius = theme.get(t::radius::rSm)
			 }}
		);
		tint();
	}

	void Avatar::setMood(std::optional<float> value) {
		if (mood != value) {
			mood = value;
			tint();
		}
	}

	void Avatar::setSelected(bool value) {
		selected = value;
		if (ui::Rectangle* shown = outline.get()) {
			shown->setVisible(value);
		}
	}

	void Avatar::setStyle(const ui::Style& value) {
		Rectangle::setStyle(value);
		tint();
	}

	// The frame: the inset surface, the mood's ring and its glow, under whatever the avatar's own style sets.
	void Avatar::tint() {
		if (getContext() == nullptr) {
			return;
		}
		const ui::Theme&		theme = getContext()->getTheme();
		const ui::Style&		declared = getDeclaredStyle();
		const Foundation::Color ring = ringColor(theme, mood);
		if (!declared.backgroundColor) {
			style.backgroundColor = theme.get(t::color::bgInset);
		}
		if (!declared.borderColor) {
			style.borderColor = ring;
		}
		if (!declared.borderWidth) {
			style.borderWidth = theme.get(t::border::bwThick);
		}
		if (!declared.borderRadius) {
			style.borderRadius = theme.get(t::radius::rSm);
		}
		if (!declared.boxShadow) {
			if (mood.has_value()) {
				style.boxShadow = Foundation::BoxShadow{.color = {ring.r, ring.g, ring.b, 0.4F}, .blur = 8.0F};
			} else {
				style.boxShadow.reset();
			}
		}
	}

} // namespace theme

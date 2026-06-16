#include "components/avatar/Avatar.h"

#include "theme/Tokens.h"
#include "theme/Variants.h"
#include "font/FontRenderer.h"
#include "graphics/Color.h"
#include "graphics/PrimitiveStyles.h"
#include "graphics/Rect.h"
#include "primitives/Primitives.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <sstream>
#include <utility>

namespace UI {

	namespace {

		// drawText scale is relative to a 16px base.
		constexpr float kTextBasePx = 16.0F;

		float textScale(float sizePx) { return sizePx / kTextBasePx; }

		// FNV-1a (32-bit), matching Avatar.tsx hash(): XOR the byte, multiply by the
		// prime with 32-bit wrap (JS Math.imul), keep the result unsigned.
		std::uint32_t fnv1a(const std::string& str) {
			std::uint32_t h = 2166136261U;
			for (const char c : str) {
				h ^= static_cast<std::uint8_t>(c);
				h *= 16777619U; // wraps at 2^32, same as Math.imul + >>> 0
			}
			return h;
		}

		// HSL (h in degrees 0..360, s/l in 0..1) -> RGB Color with the given alpha.
		Foundation::Color hslToRgb(float h, float s, float l, float a = 1.0F) {
			const float c = (1.0F - std::abs((2.0F * l) - 1.0F)) * s;
			const float hp = h / 60.0F;
			const float x = c * (1.0F - std::abs(std::fmod(hp, 2.0F) - 1.0F));
			float		r1 = 0.0F;
			float		g1 = 0.0F;
			float		b1 = 0.0F;
			if (hp < 1.0F) {
				r1 = c;
				g1 = x;
			} else if (hp < 2.0F) {
				r1 = x;
				g1 = c;
			} else if (hp < 3.0F) {
				g1 = c;
				b1 = x;
			} else if (hp < 4.0F) {
				g1 = x;
				b1 = c;
			} else if (hp < 5.0F) {
				r1 = x;
				b1 = c;
			} else {
				r1 = c;
				b1 = x;
			}
			const float m = l - (c * 0.5F);
			return {r1 + m, g1 + m, b1 + m, a};
		}

		// Mood ring color, mirroring moodColor() in Avatar.tsx. hasMood == false is
		// the prototype's `mood === undefined`: a neutral edge ring with no glow.
		Foundation::Color moodColor(bool hasMood, float mood) {
			if (!hasMood) {
				return line_edge;
			}
			if (mood < 0.3F) {
				return status_crit;
			}
			if (mood < 0.55F) {
				return status_warn;
			}
			return status_ok;
		}

		// Split seed on whitespace, take the first char of each word, keep the first
		// two, uppercase. "Kai Okafor" -> "KO"; single word -> one letter.
		std::string initialsOf(const std::string& seed) {
			std::string		  out;
			std::istringstream stream(seed);
			std::string		  word;
			while (stream >> word && out.size() < 2) {
				char first = word.front();
				if (first >= 'a' && first <= 'z') {
					first = static_cast<char>(first - ('a' - 'A'));
				}
				out.push_back(first);
			}
			return out;
		}

	} // namespace

	Avatar::Avatar(Args avatarArgs)
		: args(std::move(avatarArgs)) {}

	void Avatar::render() const {
		using Renderer::Primitives::drawCircle;
		using Renderer::Primitives::drawRect;
		using Renderer::Primitives::drawText;

		const std::uint32_t hash = fnv1a(args.seed);
		const float			hue = static_cast<float>(hash % 360U);

		const Foundation::Rect frame{args.position, {args.size, args.size}};
		const Foundation::Color ring = moodColor(args.hasMood, args.mood);

		// Mood glow: a soft halo behind the frame in the ring color, as an inline
		// box-shadow. Only when a mood is set (the prototype's softened box-shadow).
		std::optional<Foundation::BoxShadow> moodGlow;
		if (args.hasMood) {
			moodGlow = Foundation::BoxShadow{.color = withAlpha(ring, 0.4F), .blur = 8.0F, .spread = 0.0F, .offset = {0.0F, 0.0F}};
		}

		// Framed box: bg_inset surface, 2px ring border, 2px radius. With a mood set
		// the border takes the ring color; otherwise the neutral line_edge.
		drawRect({.bounds = frame,
				  .style = {.fill = bg_inset,
							.border = Foundation::BorderStyle{
								.color = ring, .width = bw_thick, .cornerRadius = r_sm, .position = Foundation::BorderPosition::Inside},
							.boxShadow = moodGlow},
				  .id = "ds_avatar_frame"});

		// Hashed silhouette: a filled disc in the portrait's first gradient hue,
		// standing in for the SVG head/shoulders art. hsl(hue 45% 24%).
		const Foundation::Vec2 center = frame.center();
		const float			   discRadius = args.size * 0.34F;
		drawCircle({.center = center, .radius = discRadius, .style = {.fill = hslToRgb(hue, 0.45F, 0.24F)}, .id = "ds_avatar_disc"});

		// Initials centered over the disc, in the portrait's light hue. fontDisplay
		// because we have no silhouette art to pin them bottom-left against.
		const std::string initials = initialsOf(args.seed);
		if (!initials.empty()) {
			const float				glyphPx = args.size * 0.34F;
			const float				scale = textScale(glyphPx);
			const Foundation::Color textColor = hslToRgb(hue, 0.55F, 0.82F);
			// Center the initials in the frame box (== disc center) using the text
			// primitive's own metrics, with a dark shadow copy for legibility.
			const auto drawInitials = [&](Foundation::Vec2 pos, Foundation::Color col) {
				drawText({.text = initials,
						  .position = pos,
						  .scale = scale,
						  .color = col,
						  .font = fontDisplay,
						  .hAlign = Foundation::HorizontalAlign::Center,
						  .vAlign = Foundation::VerticalAlign::Middle,
						  .boxWidth = args.size,
						  .boxHeight = args.size,
						  .id = "ds_avatar_initials"});
			};
			drawInitials({frame.x + 1.0F, frame.y + 1.0F}, bg_void);
			drawInitials({frame.x, frame.y}, textColor);
		}

		// Corner tick, top-right, for the dossier feel: a 5px L-bracket in text_faint.
		{
			const float leg = 5.0F;
			const float th = bw;
			const float inset = 3.0F;
			const float rx = frame.right() - inset;
			const float ty = frame.y + inset;
			drawRect({.bounds = {rx - leg, ty, leg, th}, .style = {.fill = text_faint}, .id = "ds_avatar_tick"});
			drawRect({.bounds = {rx - th, ty, th, leg}, .style = {.fill = text_faint}, .id = "ds_avatar_tick"});
		}

		// Selected: a 1px accent outline offset 1px outside the frame, over the ring.
		if (args.selected) {
			const float off = 1.0F;
			drawRect({.bounds = {frame.x - off, frame.y - off, frame.width + (off * 2.0F), frame.height + (off * 2.0F)},
					  .style = {.fill = Foundation::Color::transparent(),
								.border = Foundation::BorderStyle{
									.color = accent, .width = bw, .cornerRadius = r_sm, .position = Foundation::BorderPosition::Outside}},
					  .id = "ds_avatar_selected"});
		}
	}

} // namespace UI

#pragma once

// GENERATED from docs/ui-prototype/src/design-system/primitives/Icon/Icon.tsx
// (via icons.json) by docs/ui-prototype/scripts/gen-icon-geometry.mjs.
// Do not edit by hand.
//
// Flattened glyph geometry in the 24x24 icon space. Stroked glyphs are drawn as
// line segments with round joins; filled glyphs are tessellated. See Icon.cpp.

#include "math/Types.h"

#include <string_view>

namespace UI::Icons {

	struct SubPath {
		const Foundation::Vec2* pts;
		int						count;
		bool					closed;
	};

	struct GlyphDef {
		const SubPath* subs;
		int			   subCount;
		bool		   filled;
	};

	inline const Foundation::Vec2 play_p0[] = {{7.0F, 5.0F}, {19.0F, 12.0F}, {7.0F, 19.0F}};
	inline const SubPath play_subs[] = {{play_p0, 3, true}};
	inline const GlyphDef play_def{play_subs, 1, true};

	inline const Foundation::Vec2 pause_p0[] = {{7.0F, 5.0F}, {10.5F, 5.0F}, {10.5F, 19.0F}, {7.0F, 19.0F}};
	inline const Foundation::Vec2 pause_p1[] = {{13.5F, 5.0F}, {17.0F, 5.0F}, {17.0F, 19.0F}, {13.5F, 19.0F}};
	inline const SubPath pause_subs[] = {{pause_p0, 4, true}, {pause_p1, 4, true}};
	inline const GlyphDef pause_def{pause_subs, 2, false};

	inline const Foundation::Vec2 fast_p0[] = {{5.0F, 5.0F}, {12.0F, 12.0F}, {5.0F, 19.0F}};
	inline const Foundation::Vec2 fast_p1[] = {{12.0F, 5.0F}, {19.0F, 12.0F}, {12.0F, 19.0F}};
	inline const SubPath fast_subs[] = {{fast_p0, 3, true}, {fast_p1, 3, true}};
	inline const GlyphDef fast_def{fast_subs, 2, true};

	inline const Foundation::Vec2 veryFast_p0[] = {{3.0F, 5.0F}, {9.0F, 12.0F}, {3.0F, 19.0F}};
	inline const Foundation::Vec2 veryFast_p1[] = {{9.0F, 5.0F}, {15.0F, 12.0F}, {9.0F, 19.0F}};
	inline const Foundation::Vec2 veryFast_p2[] = {{15.0F, 5.0F}, {21.0F, 12.0F}, {15.0F, 19.0F}};
	inline const SubPath veryFast_subs[] = {{veryFast_p0, 3, true}, {veryFast_p1, 3, true}, {veryFast_p2, 3, true}};
	inline const GlyphDef veryFast_def{veryFast_subs, 3, true};

	inline const Foundation::Vec2 plus_p0[] = {{12.0F, 5.0F}, {12.0F, 19.0F}};
	inline const Foundation::Vec2 plus_p1[] = {{5.0F, 12.0F}, {19.0F, 12.0F}};
	inline const SubPath plus_subs[] = {{plus_p0, 2, false}, {plus_p1, 2, false}};
	inline const GlyphDef plus_def{plus_subs, 2, false};

	inline const Foundation::Vec2 minus_p0[] = {{5.0F, 12.0F}, {19.0F, 12.0F}};
	inline const SubPath minus_subs[] = {{minus_p0, 2, false}};
	inline const GlyphDef minus_def{minus_subs, 1, false};

	inline const Foundation::Vec2 close_p0[] = {{6.0F, 6.0F}, {18.0F, 18.0F}};
	inline const Foundation::Vec2 close_p1[] = {{18.0F, 6.0F}, {6.0F, 18.0F}};
	inline const SubPath close_subs[] = {{close_p0, 2, false}, {close_p1, 2, false}};
	inline const GlyphDef close_def{close_subs, 2, false};

	inline const Foundation::Vec2 gear_p0[] = {{12.0F, 2.5F}, {12.0F, 5.0F}};
	inline const Foundation::Vec2 gear_p1[] = {{12.0F, 19.0F}, {12.0F, 21.5F}};
	inline const Foundation::Vec2 gear_p2[] = {{21.5F, 12.0F}, {19.0F, 12.0F}};
	inline const Foundation::Vec2 gear_p3[] = {{5.0F, 12.0F}, {2.5F, 12.0F}};
	inline const Foundation::Vec2 gear_p4[] = {{18.7F, 5.3F}, {17.0F, 7.0F}};
	inline const Foundation::Vec2 gear_p5[] = {{7.0F, 17.0F}, {5.3F, 18.7F}};
	inline const Foundation::Vec2 gear_p6[] = {{18.7F, 18.7F}, {17.0F, 17.0F}};
	inline const Foundation::Vec2 gear_p7[] = {{7.0F, 7.0F}, {5.3F, 5.3F}};
	inline const Foundation::Vec2 gear_p8[] = {{15.2F, 12.0F}, {15.12F, 12.712F}, {14.883F, 13.388F}, {14.502F, 13.995F}, {13.995F, 14.502F}, {13.388F, 14.883F}, {12.712F, 15.12F}, {12.0F, 15.2F}, {11.288F, 15.12F}, {10.612F, 14.883F}, {10.005F, 14.502F}, {9.498F, 13.995F}, {9.117F, 13.388F}, {8.88F, 12.712F}, {8.8F, 12.0F}, {8.88F, 11.288F}, {9.117F, 10.612F}, {9.498F, 10.005F}, {10.005F, 9.498F}, {10.612F, 9.117F}, {11.288F, 8.88F}, {12.0F, 8.8F}, {12.712F, 8.88F}, {13.388F, 9.117F}, {13.995F, 9.498F}, {14.502F, 10.005F}, {14.883F, 10.612F}, {15.12F, 11.288F}};
	inline const SubPath gear_subs[] = {{gear_p0, 2, false}, {gear_p1, 2, false}, {gear_p2, 2, false}, {gear_p3, 2, false}, {gear_p4, 2, false}, {gear_p5, 2, false}, {gear_p6, 2, false}, {gear_p7, 2, false}, {gear_p8, 28, true}};
	inline const GlyphDef gear_def{gear_subs, 9, false};

	inline const Foundation::Vec2 menu_p0[] = {{4.0F, 7.0F}, {20.0F, 7.0F}};
	inline const Foundation::Vec2 menu_p1[] = {{4.0F, 12.0F}, {20.0F, 12.0F}};
	inline const Foundation::Vec2 menu_p2[] = {{4.0F, 17.0F}, {20.0F, 17.0F}};
	inline const SubPath menu_subs[] = {{menu_p0, 2, false}, {menu_p1, 2, false}, {menu_p2, 2, false}};
	inline const GlyphDef menu_def{menu_subs, 3, false};

	inline const Foundation::Vec2 chevronLeft_p0[] = {{15.0F, 5.0F}, {8.0F, 12.0F}, {15.0F, 19.0F}};
	inline const SubPath chevronLeft_subs[] = {{chevronLeft_p0, 3, false}};
	inline const GlyphDef chevronLeft_def{chevronLeft_subs, 1, false};

	inline const Foundation::Vec2 chevronRight_p0[] = {{9.0F, 5.0F}, {16.0F, 12.0F}, {9.0F, 19.0F}};
	inline const SubPath chevronRight_subs[] = {{chevronRight_p0, 3, false}};
	inline const GlyphDef chevronRight_def{chevronRight_subs, 1, false};

	inline const Foundation::Vec2 chevronUp_p0[] = {{5.0F, 15.0F}, {12.0F, 8.0F}, {19.0F, 15.0F}};
	inline const SubPath chevronUp_subs[] = {{chevronUp_p0, 3, false}};
	inline const GlyphDef chevronUp_def{chevronUp_subs, 1, false};

	inline const Foundation::Vec2 chevronDown_p0[] = {{5.0F, 9.0F}, {12.0F, 16.0F}, {19.0F, 9.0F}};
	inline const SubPath chevronDown_subs[] = {{chevronDown_p0, 3, false}};
	inline const GlyphDef chevronDown_def{chevronDown_subs, 1, false};

	inline const Foundation::Vec2 arrowRight_p0[] = {{4.0F, 12.0F}, {20.0F, 12.0F}};
	inline const Foundation::Vec2 arrowRight_p1[] = {{14.0F, 6.0F}, {20.0F, 12.0F}, {14.0F, 18.0F}};
	inline const SubPath arrowRight_subs[] = {{arrowRight_p0, 2, false}, {arrowRight_p1, 3, false}};
	inline const GlyphDef arrowRight_def{arrowRight_subs, 2, false};

	inline const Foundation::Vec2 check_p0[] = {{5.0F, 12.5F}, {10.0F, 17.5F}, {19.0F, 6.5F}};
	inline const SubPath check_subs[] = {{check_p0, 3, false}};
	inline const GlyphDef check_def{check_subs, 1, false};

	inline const Foundation::Vec2 alert_p0[] = {{12.0F, 3.0F}, {22.0F, 20.0F}, {2.0F, 20.0F}};
	inline const Foundation::Vec2 alert_p1[] = {{12.0F, 9.0F}, {12.0F, 14.0F}};
	inline const Foundation::Vec2 alert_p2[] = {{12.0F, 17.0F}, {12.0F, 17.5F}};
	inline const SubPath alert_subs[] = {{alert_p0, 3, true}, {alert_p1, 2, false}, {alert_p2, 2, false}};
	inline const GlyphDef alert_def{alert_subs, 3, false};

	inline const Foundation::Vec2 info_p0[] = {{12.0F, 11.0F}, {12.0F, 16.0F}};
	inline const Foundation::Vec2 info_p1[] = {{12.0F, 8.0F}, {12.0F, 8.5F}};
	inline const Foundation::Vec2 info_p2[] = {{21.0F, 12.0F}, {20.774F, 14.003F}, {20.109F, 15.905F}, {19.036F, 17.611F}, {17.611F, 19.036F}, {15.905F, 20.109F}, {14.003F, 20.774F}, {12.0F, 21.0F}, {9.997F, 20.774F}, {8.095F, 20.109F}, {6.389F, 19.036F}, {4.964F, 17.611F}, {3.891F, 15.905F}, {3.226F, 14.003F}, {3.0F, 12.0F}, {3.226F, 9.997F}, {3.891F, 8.095F}, {4.964F, 6.389F}, {6.389F, 4.964F}, {8.095F, 3.891F}, {9.997F, 3.226F}, {12.0F, 3.0F}, {14.003F, 3.226F}, {15.905F, 3.891F}, {17.611F, 4.964F}, {19.036F, 6.389F}, {20.109F, 8.095F}, {20.774F, 9.997F}};
	inline const SubPath info_subs[] = {{info_p0, 2, false}, {info_p1, 2, false}, {info_p2, 28, true}};
	inline const GlyphDef info_def{info_subs, 3, false};

	inline const Foundation::Vec2 globe_p0[] = {{3.0F, 12.0F}, {21.0F, 12.0F}};
	inline const Foundation::Vec2 globe_p1[] = {{12.0F, 3.0F}, {12.439F, 3.664F}, {12.82F, 4.512F}, {13.143F, 5.518F}, {13.406F, 6.656F}, {13.611F, 7.9F}, {13.758F, 9.223F}, {13.846F, 10.598F}, {13.875F, 12.0F}, {13.846F, 13.402F}, {13.758F, 14.777F}, {13.611F, 16.1F}, {13.406F, 17.344F}, {13.143F, 18.482F}, {12.82F, 19.488F}, {12.439F, 20.336F}, {12.0F, 21.0F}, {11.561F, 20.336F}, {11.18F, 19.488F}, {10.857F, 18.482F}, {10.594F, 17.344F}, {10.389F, 16.1F}, {10.242F, 14.777F}, {10.154F, 13.402F}, {10.125F, 12.0F}, {10.154F, 10.598F}, {10.242F, 9.223F}, {10.389F, 7.9F}, {10.594F, 6.656F}, {10.857F, 5.518F}, {11.18F, 4.512F}, {11.561F, 3.664F}, {12.0F, 3.0F}};
	inline const Foundation::Vec2 globe_p2[] = {{21.0F, 12.0F}, {20.774F, 14.003F}, {20.109F, 15.905F}, {19.036F, 17.611F}, {17.611F, 19.036F}, {15.905F, 20.109F}, {14.003F, 20.774F}, {12.0F, 21.0F}, {9.997F, 20.774F}, {8.095F, 20.109F}, {6.389F, 19.036F}, {4.964F, 17.611F}, {3.891F, 15.905F}, {3.226F, 14.003F}, {3.0F, 12.0F}, {3.226F, 9.997F}, {3.891F, 8.095F}, {4.964F, 6.389F}, {6.389F, 4.964F}, {8.095F, 3.891F}, {9.997F, 3.226F}, {12.0F, 3.0F}, {14.003F, 3.226F}, {15.905F, 3.891F}, {17.611F, 4.964F}, {19.036F, 6.389F}, {20.109F, 8.095F}, {20.774F, 9.997F}};
	inline const SubPath globe_subs[] = {{globe_p0, 2, false}, {globe_p1, 33, true}, {globe_p2, 28, true}};
	inline const GlyphDef globe_def{globe_subs, 3, false};

	inline const Foundation::Vec2 crosshair_p0[] = {{12.0F, 2.0F}, {12.0F, 6.0F}};
	inline const Foundation::Vec2 crosshair_p1[] = {{12.0F, 18.0F}, {12.0F, 22.0F}};
	inline const Foundation::Vec2 crosshair_p2[] = {{2.0F, 12.0F}, {6.0F, 12.0F}};
	inline const Foundation::Vec2 crosshair_p3[] = {{18.0F, 12.0F}, {22.0F, 12.0F}};
	inline const Foundation::Vec2 crosshair_p4[] = {{19.0F, 12.0F}, {18.824F, 13.558F}, {18.307F, 15.037F}, {17.473F, 16.364F}, {16.364F, 17.473F}, {15.037F, 18.307F}, {13.558F, 18.824F}, {12.0F, 19.0F}, {10.442F, 18.824F}, {8.963F, 18.307F}, {7.636F, 17.473F}, {6.527F, 16.364F}, {5.693F, 15.037F}, {5.176F, 13.558F}, {5.0F, 12.0F}, {5.176F, 10.442F}, {5.693F, 8.963F}, {6.527F, 7.636F}, {7.636F, 6.527F}, {8.963F, 5.693F}, {10.442F, 5.176F}, {12.0F, 5.0F}, {13.558F, 5.176F}, {15.037F, 5.693F}, {16.364F, 6.527F}, {17.473F, 7.636F}, {18.307F, 8.963F}, {18.824F, 10.442F}};
	inline const Foundation::Vec2 crosshair_p5[] = {{13.4F, 12.0F}, {13.365F, 12.312F}, {13.261F, 12.607F}, {13.095F, 12.873F}, {12.873F, 13.095F}, {12.607F, 13.261F}, {12.312F, 13.365F}, {12.0F, 13.4F}, {11.688F, 13.365F}, {11.393F, 13.261F}, {11.127F, 13.095F}, {10.905F, 12.873F}, {10.739F, 12.607F}, {10.635F, 12.312F}, {10.6F, 12.0F}, {10.635F, 11.688F}, {10.739F, 11.393F}, {10.905F, 11.127F}, {11.127F, 10.905F}, {11.393F, 10.739F}, {11.688F, 10.635F}, {12.0F, 10.6F}, {12.312F, 10.635F}, {12.607F, 10.739F}, {12.873F, 10.905F}, {13.095F, 11.127F}, {13.261F, 11.393F}, {13.365F, 11.688F}};
	inline const SubPath crosshair_subs[] = {{crosshair_p0, 2, false}, {crosshair_p1, 2, false}, {crosshair_p2, 2, false}, {crosshair_p3, 2, false}, {crosshair_p4, 28, true}, {crosshair_p5, 28, true}};
	inline const GlyphDef crosshair_def{crosshair_subs, 6, false};

	inline const Foundation::Vec2 user_p0[] = {{5.0F, 20.0F}, {5.035F, 19.273F}, {5.137F, 18.594F}, {5.303F, 17.961F}, {5.531F, 17.375F}, {5.818F, 16.836F}, {6.16F, 16.344F}, {6.555F, 15.898F}, {7.0F, 15.5F}, {7.492F, 15.148F}, {8.027F, 14.844F}, {8.604F, 14.586F}, {9.219F, 14.375F}, {9.869F, 14.211F}, {10.551F, 14.094F}, {11.262F, 14.023F}, {12.0F, 14.0F}, {12.738F, 14.023F}, {13.449F, 14.094F}, {14.131F, 14.211F}, {14.781F, 14.375F}, {15.396F, 14.586F}, {15.973F, 14.844F}, {16.508F, 15.148F}, {17.0F, 15.5F}, {17.445F, 15.898F}, {17.84F, 16.344F}, {18.182F, 16.836F}, {18.469F, 17.375F}, {18.697F, 17.961F}, {18.863F, 18.594F}, {18.965F, 19.273F}, {19.0F, 20.0F}};
	inline const Foundation::Vec2 user_p1[] = {{15.6F, 8.0F}, {15.51F, 8.801F}, {15.243F, 9.562F}, {14.815F, 10.245F}, {14.245F, 10.815F}, {13.562F, 11.243F}, {12.801F, 11.51F}, {12.0F, 11.6F}, {11.199F, 11.51F}, {10.438F, 11.243F}, {9.755F, 10.815F}, {9.185F, 10.245F}, {8.757F, 9.562F}, {8.49F, 8.801F}, {8.4F, 8.0F}, {8.49F, 7.199F}, {8.757F, 6.438F}, {9.185F, 5.755F}, {9.755F, 5.185F}, {10.438F, 4.757F}, {11.199F, 4.49F}, {12.0F, 4.4F}, {12.801F, 4.49F}, {13.562F, 4.757F}, {14.245F, 5.185F}, {14.815F, 5.755F}, {15.243F, 6.438F}, {15.51F, 7.199F}};
	inline const SubPath user_subs[] = {{user_p0, 33, false}, {user_p1, 28, true}};
	inline const GlyphDef user_def{user_subs, 2, false};

	inline const Foundation::Vec2 users_p0[] = {{3.0F, 19.0F}, {3.03F, 18.363F}, {3.118F, 17.763F}, {3.262F, 17.202F}, {3.459F, 16.68F}, {3.707F, 16.197F}, {4.002F, 15.753F}, {4.342F, 15.35F}, {4.725F, 14.987F}, {5.148F, 14.666F}, {5.607F, 14.386F}, {6.102F, 14.147F}, {6.628F, 13.952F}, {7.184F, 13.798F}, {7.766F, 13.688F}, {8.372F, 13.622F}, {9.0F, 13.6F}, {9.628F, 13.622F}, {10.234F, 13.688F}, {10.816F, 13.798F}, {11.372F, 13.952F}, {11.898F, 14.147F}, {12.393F, 14.386F}, {12.852F, 14.666F}, {13.275F, 14.988F}, {13.658F, 15.35F}, {13.998F, 15.753F}, {14.293F, 16.197F}, {14.541F, 16.68F}, {14.738F, 17.202F}, {14.882F, 17.763F}, {14.97F, 18.363F}, {15.0F, 19.0F}};
	inline const Foundation::Vec2 users_p1[] = {{16.0F, 5.4F}, {16.366F, 5.488F}, {16.712F, 5.6F}, {17.038F, 5.735F}, {17.344F, 5.892F}, {17.628F, 6.07F}, {17.891F, 6.267F}, {18.132F, 6.481F}, {18.35F, 6.712F}, {18.544F, 6.959F}, {18.715F, 7.219F}, {18.861F, 7.492F}, {18.981F, 7.777F}, {19.076F, 8.071F}, {19.145F, 8.374F}, {19.186F, 8.684F}, {19.2F, 9.0F}, {19.188F, 9.279F}, {19.154F, 9.552F}, {19.098F, 9.818F}, {19.02F, 10.077F}, {18.922F, 10.326F}, {18.804F, 10.566F}, {18.668F, 10.795F}, {18.512F, 11.013F}, {18.34F, 11.217F}, {18.15F, 11.407F}, {17.945F, 11.583F}, {17.723F, 11.742F}, {17.488F, 11.885F}, {17.238F, 12.009F}, {16.975F, 12.115F}, {16.7F, 12.2F}};
	inline const Foundation::Vec2 users_p2[] = {{16.5F, 13.8F}, {17.028F, 13.891F}, {17.526F, 14.015F}, {17.992F, 14.171F}, {18.427F, 14.359F}, {18.829F, 14.579F}, {19.198F, 14.83F}, {19.535F, 15.112F}, {19.838F, 15.425F}, {20.106F, 15.768F}, {20.341F, 16.142F}, {20.54F, 16.545F}, {20.705F, 16.978F}, {20.833F, 17.44F}, {20.926F, 17.932F}, {20.981F, 18.452F}, {21.0F, 19.0F}};
	inline const Foundation::Vec2 users_p3[] = {{12.2F, 8.0F}, {12.12F, 8.712F}, {11.883F, 9.388F}, {11.502F, 9.995F}, {10.995F, 10.502F}, {10.388F, 10.883F}, {9.712F, 11.12F}, {9.0F, 11.2F}, {8.288F, 11.12F}, {7.612F, 10.883F}, {7.005F, 10.502F}, {6.498F, 9.995F}, {6.117F, 9.388F}, {5.88F, 8.712F}, {5.8F, 8.0F}, {5.88F, 7.288F}, {6.117F, 6.612F}, {6.498F, 6.005F}, {7.005F, 5.498F}, {7.612F, 5.117F}, {8.288F, 4.88F}, {9.0F, 4.8F}, {9.712F, 4.88F}, {10.388F, 5.117F}, {10.995F, 5.498F}, {11.502F, 6.005F}, {11.883F, 6.612F}, {12.12F, 7.288F}};
	inline const SubPath users_subs[] = {{users_p0, 33, false}, {users_p1, 33, false}, {users_p2, 17, false}, {users_p3, 28, true}};
	inline const GlyphDef users_def{users_subs, 4, false};

	inline const Foundation::Vec2 heart_p0[] = {{12.0F, 20.0F}, {11.905F, 19.937F}, {11.635F, 19.753F}, {11.216F, 19.455F}, {10.672F, 19.052F}, {10.029F, 18.55F}, {9.311F, 17.959F}, {8.543F, 17.286F}, {7.75F, 16.538F}, {6.957F, 15.723F}, {6.189F, 14.849F}, {5.471F, 13.923F}, {4.828F, 12.955F}, {4.284F, 11.95F}, {3.865F, 10.918F}, {3.595F, 9.865F}, {3.5F, 8.8F}, {3.525F, 8.285F}, {3.599F, 7.79F}, {3.72F, 7.317F}, {3.883F, 6.869F}, {4.087F, 6.446F}, {4.328F, 6.051F}, {4.604F, 5.685F}, {4.913F, 5.35F}, {5.25F, 5.048F}, {5.614F, 4.78F}, {6.002F, 4.549F}, {6.411F, 4.356F}, {6.838F, 4.203F}, {7.28F, 4.091F}, {7.735F, 4.023F}, {8.2F, 4.0F}, {8.533F, 4.012F}, {8.855F, 4.046F}, {9.168F, 4.102F}, {9.469F, 4.178F}, {9.758F, 4.275F}, {10.035F, 4.39F}, {10.299F, 4.524F}, {10.55F, 4.675F}, {10.787F, 4.842F}, {11.009F, 5.025F}, {11.215F, 5.223F}, {11.406F, 5.434F}, {11.581F, 5.659F}, {11.738F, 5.895F}, {11.878F, 6.142F}, {12.0F, 6.4F}, {12.122F, 6.142F}, {12.262F, 5.895F}, {12.419F, 5.659F}, {12.594F, 5.434F}, {12.785F, 5.223F}, {12.991F, 5.025F}, {13.213F, 4.842F}, {13.45F, 4.675F}, {13.701F, 4.524F}, {13.965F, 4.39F}, {14.242F, 4.275F}, {14.531F, 4.178F}, {14.832F, 4.102F}, {15.145F, 4.046F}, {15.467F, 4.012F}, {15.8F, 4.0F}, {16.265F, 4.023F}, {16.72F, 4.091F}, {17.162F, 4.203F}, {17.589F, 4.356F}, {17.998F, 4.549F}, {18.386F, 4.78F}, {18.75F, 5.048F}, {19.088F, 5.35F}, {19.396F, 5.685F}, {19.672F, 6.051F}, {19.913F, 6.446F}, {20.117F, 6.869F}, {20.28F, 7.317F}, {20.401F, 7.79F}, {20.475F, 8.285F}, {20.5F, 8.8F}, {20.405F, 9.865F}, {20.135F, 10.918F}, {19.716F, 11.95F}, {19.172F, 12.955F}, {18.529F, 13.923F}, {17.811F, 14.849F}, {17.043F, 15.723F}, {16.25F, 16.538F}, {15.457F, 17.286F}, {14.689F, 17.959F}, {13.971F, 18.55F}, {13.328F, 19.052F}, {12.784F, 19.455F}, {12.365F, 19.753F}, {12.095F, 19.937F}, {12.0F, 20.0F}};
	inline const SubPath heart_subs[] = {{heart_p0, 97, true}};
	inline const GlyphDef heart_def{heart_subs, 1, false};

	inline const Foundation::Vec2 food_p0[] = {{7.0F, 3.0F}, {7.0F, 10.0F}, {6.989F, 10.182F}, {6.957F, 10.354F}, {6.908F, 10.52F}, {6.844F, 10.68F}, {6.768F, 10.837F}, {6.684F, 10.993F}, {6.593F, 11.151F}, {6.5F, 11.313F}, {6.407F, 11.48F}, {6.316F, 11.655F}, {6.232F, 11.841F}, {6.156F, 12.039F}, {6.092F, 12.252F}, {6.043F, 12.481F}, {6.011F, 12.73F}, {6.0F, 13.0F}, {6.4F, 21.0F}, {7.6F, 21.0F}, {8.0F, 13.0F}, {7.989F, 12.73F}, {7.957F, 12.481F}, {7.908F, 12.252F}, {7.844F, 12.039F}, {7.768F, 11.841F}, {7.684F, 11.655F}, {7.593F, 11.48F}, {7.5F, 11.313F}, {7.407F, 11.151F}, {7.316F, 10.993F}, {7.232F, 10.837F}, {7.156F, 10.68F}, {7.092F, 10.52F}, {7.043F, 10.354F}, {7.011F, 10.182F}, {7.0F, 10.0F}, {7.0F, 3.0F}};
	inline const Foundation::Vec2 food_p1[] = {{16.0F, 3.0F}, {15.637F, 3.023F}, {15.297F, 3.092F}, {14.98F, 3.204F}, {14.688F, 3.359F}, {14.418F, 3.555F}, {14.172F, 3.791F}, {13.949F, 4.065F}, {13.75F, 4.375F}, {13.574F, 4.72F}, {13.422F, 5.1F}, {13.293F, 5.511F}, {13.188F, 5.953F}, {13.105F, 6.425F}, {13.047F, 6.924F}, {13.012F, 7.449F}, {13.0F, 8.0F}, {13.011F, 8.363F}, {13.045F, 8.703F}, {13.099F, 9.02F}, {13.172F, 9.313F}, {13.262F, 9.582F}, {13.369F, 9.828F}, {13.49F, 10.051F}, {13.625F, 10.25F}, {13.771F, 10.426F}, {13.928F, 10.578F}, {14.093F, 10.707F}, {14.266F, 10.813F}, {14.444F, 10.895F}, {14.627F, 10.953F}, {14.813F, 10.988F}, {15.0F, 11.0F}, {14.6F, 21.0F}, {15.8F, 21.0F}, {16.0F, 3.0F}};
	inline const SubPath food_subs[] = {{food_p0, 38, false}, {food_p1, 36, true}};
	inline const GlyphDef food_def{food_subs, 2, false};

	inline const Foundation::Vec2 water_p0[] = {{12.0F, 3.0F}, {11.933F, 3.08F}, {11.742F, 3.31F}, {11.446F, 3.676F}, {11.063F, 4.164F}, {10.608F, 4.761F}, {10.102F, 5.452F}, {9.56F, 6.224F}, {9.0F, 7.063F}, {8.44F, 7.954F}, {7.898F, 8.884F}, {7.392F, 9.839F}, {6.938F, 10.805F}, {6.554F, 11.768F}, {6.258F, 12.714F}, {6.067F, 13.629F}, {6.0F, 14.5F}, {6.031F, 15.144F}, {6.122F, 15.763F}, {6.271F, 16.353F}, {6.473F, 16.914F}, {6.727F, 17.443F}, {7.028F, 17.937F}, {7.375F, 18.394F}, {7.762F, 18.813F}, {8.189F, 19.19F}, {8.651F, 19.524F}, {9.146F, 19.813F}, {9.67F, 20.055F}, {10.221F, 20.246F}, {10.795F, 20.386F}, {11.389F, 20.471F}, {12.0F, 20.5F}, {12.611F, 20.471F}, {13.205F, 20.386F}, {13.779F, 20.246F}, {14.33F, 20.055F}, {14.854F, 19.813F}, {15.349F, 19.524F}, {15.811F, 19.19F}, {16.238F, 18.813F}, {16.625F, 18.394F}, {16.972F, 17.937F}, {17.273F, 17.443F}, {17.527F, 16.914F}, {17.729F, 16.353F}, {17.878F, 15.763F}, {17.969F, 15.144F}, {18.0F, 14.5F}, {17.933F, 13.629F}, {17.742F, 12.714F}, {17.446F, 11.768F}, {17.063F, 10.805F}, {16.608F, 9.839F}, {16.102F, 8.884F}, {15.56F, 7.954F}, {15.0F, 7.063F}, {14.44F, 6.224F}, {13.898F, 5.452F}, {13.392F, 4.761F}, {12.938F, 4.164F}, {12.554F, 3.676F}, {12.258F, 3.31F}, {12.067F, 3.08F}, {12.0F, 3.0F}};
	inline const SubPath water_subs[] = {{water_p0, 65, true}};
	inline const GlyphDef water_def{water_subs, 1, true};

	inline const Foundation::Vec2 energy_p0[] = {{13.0F, 3.0F}, {5.0F, 13.0F}, {11.0F, 13.0F}, {10.0F, 21.0F}, {19.0F, 10.0F}, {13.0F, 10.0F}};
	inline const SubPath energy_subs[] = {{energy_p0, 6, true}};
	inline const GlyphDef energy_def{energy_subs, 1, true};

	inline const Foundation::Vec2 rest_p0[] = {{20.0F, 14.5F}, {19.754F, 14.628F}, {19.503F, 14.748F}, {19.248F, 14.861F}, {18.989F, 14.967F}, {18.726F, 15.065F}, {18.458F, 15.156F}, {18.187F, 15.238F}, {17.912F, 15.312F}, {17.634F, 15.379F}, {17.353F, 15.437F}, {17.068F, 15.486F}, {16.78F, 15.527F}, {16.489F, 15.558F}, {16.195F, 15.581F}, {15.899F, 15.595F}, {15.6F, 15.6F}, {14.674F, 15.553F}, {13.773F, 15.414F}, {12.904F, 15.189F}, {12.069F, 14.881F}, {11.274F, 14.496F}, {10.523F, 14.039F}, {9.822F, 13.514F}, {9.175F, 12.925F}, {8.586F, 12.278F}, {8.061F, 11.577F}, {7.604F, 10.826F}, {7.219F, 10.031F}, {6.911F, 9.196F}, {6.686F, 8.327F}, {6.547F, 7.426F}, {6.5F, 6.5F}, {6.505F, 6.201F}, {6.519F, 5.905F}, {6.542F, 5.611F}, {6.573F, 5.319F}, {6.614F, 5.029F}, {6.663F, 4.742F}, {6.721F, 4.457F}, {6.788F, 4.175F}, {6.862F, 3.895F}, {6.944F, 3.617F}, {7.035F, 3.342F}, {7.133F, 3.069F}, {7.239F, 2.798F}, {7.352F, 2.53F}, {7.472F, 2.264F}, {7.6F, 2.0F}, {6.993F, 2.267F}, {6.412F, 2.579F}, {5.858F, 2.933F}, {5.333F, 3.328F}, {4.839F, 3.762F}, {4.378F, 4.232F}, {3.952F, 4.737F}, {3.563F, 5.275F}, {3.212F, 5.843F}, {2.902F, 6.439F}, {2.634F, 7.062F}, {2.411F, 7.709F}, {2.234F, 8.379F}, {2.105F, 9.068F}, {2.027F, 9.776F}, {2.0F, 10.5F}, {2.05F, 11.464F}, {2.195F, 12.401F}, {2.431F, 13.308F}, {2.753F, 14.178F}, {3.156F, 15.008F}, {3.635F, 15.791F}, {4.184F, 16.523F}, {4.8F, 17.2F}, {5.477F, 17.816F}, {6.209F, 18.365F}, {6.992F, 18.844F}, {7.822F, 19.247F}, {8.692F, 19.569F}, {9.599F, 19.805F}, {10.536F, 19.95F}, {11.5F, 20.0F}, {12.224F, 19.973F}, {12.932F, 19.895F}, {13.621F, 19.767F}, {14.291F, 19.591F}, {14.938F, 19.369F}, {15.561F, 19.104F}, {16.157F, 18.797F}, {16.725F, 18.45F}, {17.263F, 18.066F}, {17.768F, 17.646F}, {18.238F, 17.194F}, {18.672F, 16.709F}, {19.067F, 16.196F}, {19.421F, 15.655F}, {19.733F, 15.089F}, {20.0F, 14.5F}};
	inline const SubPath rest_subs[] = {{rest_p0, 97, true}};
	inline const GlyphDef rest_def{rest_subs, 1, false};

	inline const Foundation::Vec2 hammer_p0[] = {{14.0F, 6.0F}, {18.0F, 10.0F}};
	inline const Foundation::Vec2 hammer_p1[] = {{16.5F, 4.5F}, {19.5F, 7.5F}, {17.0F, 10.0F}, {14.0F, 7.0F}};
	inline const Foundation::Vec2 hammer_p2[] = {{15.5F, 8.5F}, {7.0F, 17.0F}, {9.0F, 19.0F}, {17.5F, 10.5F}};
	inline const SubPath hammer_subs[] = {{hammer_p0, 2, false}, {hammer_p1, 4, true}, {hammer_p2, 4, false}};
	inline const GlyphDef hammer_def{hammer_subs, 3, false};

	inline const Foundation::Vec2 box_p0[] = {{4.0F, 8.0F}, {12.0F, 4.0F}, {20.0F, 8.0F}, {12.0F, 12.0F}};
	inline const Foundation::Vec2 box_p1[] = {{4.0F, 8.0F}, {4.0F, 16.0F}, {12.0F, 20.0F}, {12.0F, 12.0F}};
	inline const Foundation::Vec2 box_p2[] = {{20.0F, 8.0F}, {20.0F, 16.0F}, {12.0F, 20.0F}};
	inline const SubPath box_subs[] = {{box_p0, 4, true}, {box_p1, 4, false}, {box_p2, 3, false}};
	inline const GlyphDef box_def{box_subs, 3, false};

	inline const Foundation::Vec2 leaf_p0[] = {{5.0F, 19.0F}, {5.069F, 17.524F}, {5.273F, 16.102F}, {5.606F, 14.737F}, {6.063F, 13.438F}, {6.636F, 12.208F}, {7.32F, 11.055F}, {8.11F, 9.983F}, {9.0F, 9.0F}, {9.983F, 8.11F}, {11.055F, 7.32F}, {12.208F, 6.636F}, {13.438F, 6.063F}, {14.737F, 5.606F}, {16.102F, 5.273F}, {17.524F, 5.069F}, {19.0F, 5.0F}, {18.931F, 6.476F}, {18.727F, 7.898F}, {18.394F, 9.263F}, {17.938F, 10.563F}, {17.364F, 11.792F}, {16.68F, 12.945F}, {15.89F, 14.017F}, {15.0F, 15.0F}, {14.017F, 15.89F}, {12.945F, 16.68F}, {11.792F, 17.364F}, {10.563F, 17.938F}, {9.263F, 18.394F}, {7.898F, 18.727F}, {6.476F, 18.931F}, {5.0F, 19.0F}};
	inline const Foundation::Vec2 leaf_p1[] = {{9.0F, 15.0F}, {16.0F, 8.0F}};
	inline const SubPath leaf_subs[] = {{leaf_p0, 33, true}, {leaf_p1, 2, false}};
	inline const GlyphDef leaf_def{leaf_subs, 2, false};

	inline const Foundation::Vec2 search_p0[] = {{15.5F, 15.5F}, {20.0F, 20.0F}};
	inline const Foundation::Vec2 search_p1[] = {{17.0F, 11.0F}, {16.85F, 12.335F}, {16.406F, 13.603F}, {15.691F, 14.741F}, {14.741F, 15.691F}, {13.603F, 16.406F}, {12.335F, 16.85F}, {11.0F, 17.0F}, {9.665F, 16.85F}, {8.397F, 16.406F}, {7.259F, 15.691F}, {6.309F, 14.741F}, {5.594F, 13.603F}, {5.15F, 12.335F}, {5.0F, 11.0F}, {5.15F, 9.665F}, {5.594F, 8.397F}, {6.309F, 7.259F}, {7.259F, 6.309F}, {8.397F, 5.594F}, {9.665F, 5.15F}, {11.0F, 5.0F}, {12.335F, 5.15F}, {13.603F, 5.594F}, {14.741F, 6.309F}, {15.691F, 7.259F}, {16.406F, 8.397F}, {16.85F, 9.665F}};
	inline const SubPath search_subs[] = {{search_p0, 2, false}, {search_p1, 28, true}};
	inline const GlyphDef search_def{search_subs, 2, false};

	inline const Foundation::Vec2 lock_p0[] = {{8.0F, 11.0F}, {8.0F, 8.0F}, {8.021F, 7.593F}, {8.082F, 7.196F}, {8.181F, 6.814F}, {8.316F, 6.447F}, {8.485F, 6.097F}, {8.686F, 5.768F}, {8.916F, 5.459F}, {9.175F, 5.175F}, {9.459F, 4.916F}, {9.768F, 4.686F}, {10.097F, 4.485F}, {10.447F, 4.316F}, {10.814F, 4.181F}, {11.196F, 4.082F}, {11.593F, 4.021F}, {12.0F, 4.0F}, {12.407F, 4.021F}, {12.804F, 4.082F}, {13.186F, 4.181F}, {13.553F, 4.316F}, {13.903F, 4.485F}, {14.232F, 4.686F}, {14.541F, 4.916F}, {14.825F, 5.175F}, {15.084F, 5.459F}, {15.314F, 5.768F}, {15.515F, 6.097F}, {15.684F, 6.447F}, {15.819F, 6.814F}, {15.918F, 7.196F}, {15.979F, 7.593F}, {16.0F, 8.0F}, {16.0F, 11.0F}};
	inline const Foundation::Vec2 lock_p1[] = {{5.0F, 11.0F}, {19.0F, 11.0F}, {19.0F, 20.0F}, {5.0F, 20.0F}};
	inline const SubPath lock_subs[] = {{lock_p0, 35, false}, {lock_p1, 4, true}};
	inline const GlyphDef lock_def{lock_subs, 2, false};

	inline const Foundation::Vec2 dice_p0[] = {{10.1F, 9.0F}, {10.072F, 9.245F}, {9.991F, 9.477F}, {9.86F, 9.686F}, {9.686F, 9.86F}, {9.477F, 9.991F}, {9.245F, 10.072F}, {9.0F, 10.1F}, {8.755F, 10.072F}, {8.523F, 9.991F}, {8.314F, 9.86F}, {8.14F, 9.686F}, {8.009F, 9.477F}, {7.928F, 9.245F}, {7.9F, 9.0F}, {7.928F, 8.755F}, {8.009F, 8.523F}, {8.14F, 8.314F}, {8.314F, 8.14F}, {8.523F, 8.009F}, {8.755F, 7.928F}, {9.0F, 7.9F}, {9.245F, 7.928F}, {9.477F, 8.009F}, {9.686F, 8.14F}, {9.86F, 8.314F}, {9.991F, 8.523F}, {10.072F, 8.755F}};
	inline const Foundation::Vec2 dice_p1[] = {{16.1F, 15.0F}, {16.072F, 15.245F}, {15.991F, 15.477F}, {15.86F, 15.686F}, {15.686F, 15.86F}, {15.477F, 15.991F}, {15.245F, 16.072F}, {15.0F, 16.1F}, {14.755F, 16.072F}, {14.523F, 15.991F}, {14.314F, 15.86F}, {14.14F, 15.686F}, {14.009F, 15.477F}, {13.928F, 15.245F}, {13.9F, 15.0F}, {13.928F, 14.755F}, {14.009F, 14.523F}, {14.14F, 14.314F}, {14.314F, 14.14F}, {14.523F, 14.009F}, {14.755F, 13.928F}, {15.0F, 13.9F}, {15.245F, 13.928F}, {15.477F, 14.009F}, {15.686F, 14.14F}, {15.86F, 14.314F}, {15.991F, 14.523F}, {16.072F, 14.755F}};
	inline const Foundation::Vec2 dice_p2[] = {{16.1F, 9.0F}, {16.072F, 9.245F}, {15.991F, 9.477F}, {15.86F, 9.686F}, {15.686F, 9.86F}, {15.477F, 9.991F}, {15.245F, 10.072F}, {15.0F, 10.1F}, {14.755F, 10.072F}, {14.523F, 9.991F}, {14.314F, 9.86F}, {14.14F, 9.686F}, {14.009F, 9.477F}, {13.928F, 9.245F}, {13.9F, 9.0F}, {13.928F, 8.755F}, {14.009F, 8.523F}, {14.14F, 8.314F}, {14.314F, 8.14F}, {14.523F, 8.009F}, {14.755F, 7.928F}, {15.0F, 7.9F}, {15.245F, 7.928F}, {15.477F, 8.009F}, {15.686F, 8.14F}, {15.86F, 8.314F}, {15.991F, 8.523F}, {16.072F, 8.755F}};
	inline const Foundation::Vec2 dice_p3[] = {{10.1F, 15.0F}, {10.072F, 15.245F}, {9.991F, 15.477F}, {9.86F, 15.686F}, {9.686F, 15.86F}, {9.477F, 15.991F}, {9.245F, 16.072F}, {9.0F, 16.1F}, {8.755F, 16.072F}, {8.523F, 15.991F}, {8.314F, 15.86F}, {8.14F, 15.686F}, {8.009F, 15.477F}, {7.928F, 15.245F}, {7.9F, 15.0F}, {7.928F, 14.755F}, {8.009F, 14.523F}, {8.14F, 14.314F}, {8.314F, 14.14F}, {8.523F, 14.009F}, {8.755F, 13.928F}, {9.0F, 13.9F}, {9.245F, 13.928F}, {9.477F, 14.009F}, {9.686F, 14.14F}, {9.86F, 14.314F}, {9.991F, 14.523F}, {10.072F, 14.755F}};
	inline const Foundation::Vec2 dice_p4[] = {{4.0F, 4.0F}, {20.0F, 4.0F}, {20.0F, 20.0F}, {4.0F, 20.0F}};
	inline const SubPath dice_subs[] = {{dice_p0, 28, true}, {dice_p1, 28, true}, {dice_p2, 28, true}, {dice_p3, 28, true}, {dice_p4, 4, true}};
	inline const GlyphDef dice_def{dice_subs, 5, false};

	inline const Foundation::Vec2 sprout_p0[] = {{12.0F, 20.0F}, {12.0F, 11.0F}};
	inline const Foundation::Vec2 sprout_p1[] = {{12.0F, 13.0F}, {11.96F, 12.273F}, {11.843F, 11.594F}, {11.654F, 10.961F}, {11.398F, 10.375F}, {11.081F, 9.836F}, {10.708F, 9.344F}, {10.283F, 8.898F}, {9.813F, 8.5F}, {9.301F, 8.148F}, {8.753F, 7.844F}, {8.174F, 7.586F}, {7.57F, 7.375F}, {6.946F, 7.211F}, {6.306F, 7.094F}, {5.655F, 7.023F}, {5.0F, 7.0F}, {5.04F, 7.727F}, {5.157F, 8.406F}, {5.346F, 9.039F}, {5.602F, 9.625F}, {5.919F, 10.164F}, {6.292F, 10.656F}, {6.717F, 11.102F}, {7.188F, 11.5F}, {7.699F, 11.852F}, {8.247F, 12.156F}, {8.826F, 12.414F}, {9.43F, 12.625F}, {10.054F, 12.789F}, {10.694F, 12.906F}, {11.345F, 12.977F}, {12.0F, 13.0F}};
	inline const Foundation::Vec2 sprout_p2[] = {{12.0F, 11.0F}, {12.035F, 10.455F}, {12.136F, 9.945F}, {12.3F, 9.471F}, {12.523F, 9.031F}, {12.803F, 8.627F}, {13.134F, 8.258F}, {13.513F, 7.924F}, {13.938F, 7.625F}, {14.403F, 7.361F}, {14.905F, 7.133F}, {15.442F, 6.939F}, {16.008F, 6.781F}, {16.6F, 6.658F}, {17.216F, 6.57F}, {17.85F, 6.518F}, {18.5F, 6.5F}, {18.465F, 7.127F}, {18.364F, 7.698F}, {18.2F, 8.215F}, {17.977F, 8.68F}, {17.697F, 9.095F}, {17.366F, 9.462F}, {16.987F, 9.784F}, {16.563F, 10.063F}, {16.097F, 10.3F}, {15.595F, 10.499F}, {15.058F, 10.661F}, {14.492F, 10.789F}, {13.9F, 10.885F}, {13.284F, 10.95F}, {12.65F, 10.988F}, {12.0F, 11.0F}};
	inline const SubPath sprout_subs[] = {{sprout_p0, 2, false}, {sprout_p1, 33, true}, {sprout_p2, 33, true}};
	inline const GlyphDef sprout_def{sprout_subs, 3, false};

	inline const Foundation::Vec2 mountain_p0[] = {{3.0F, 19.0F}, {9.5F, 7.0F}, {14.0F, 14.0F}, {16.5F, 10.0F}, {21.0F, 19.0F}};
	inline const SubPath mountain_subs[] = {{mountain_p0, 5, true}};
	inline const GlyphDef mountain_def{mountain_subs, 1, true};

	inline const Foundation::Vec2 temp_p0[] = {{10.0F, 5.0F}, {10.01F, 4.796F}, {10.041F, 4.598F}, {10.09F, 4.407F}, {10.158F, 4.223F}, {10.242F, 4.049F}, {10.343F, 3.884F}, {10.458F, 3.73F}, {10.588F, 3.588F}, {10.73F, 3.458F}, {10.884F, 3.343F}, {11.049F, 3.242F}, {11.223F, 3.158F}, {11.407F, 3.09F}, {11.598F, 3.041F}, {11.796F, 3.01F}, {12.0F, 3.0F}, {12.204F, 3.01F}, {12.402F, 3.041F}, {12.593F, 3.09F}, {12.777F, 3.158F}, {12.951F, 3.242F}, {13.116F, 3.343F}, {13.27F, 3.458F}, {13.413F, 3.588F}, {13.542F, 3.73F}, {13.657F, 3.884F}, {13.758F, 4.049F}, {13.842F, 4.223F}, {13.91F, 4.407F}, {13.959F, 4.598F}, {13.99F, 4.796F}, {14.0F, 5.0F}, {14.0F, 13.5F}, {14.22F, 13.638F}, {14.43F, 13.79F}, {14.63F, 13.954F}, {14.819F, 14.131F}, {14.996F, 14.32F}, {15.16F, 14.52F}, {15.312F, 14.73F}, {15.45F, 14.95F}, {15.574F, 15.18F}, {15.684F, 15.418F}, {15.778F, 15.665F}, {15.856F, 15.919F}, {15.918F, 16.18F}, {15.963F, 16.448F}, {15.991F, 16.721F}, {16.0F, 17.0F}, {15.979F, 17.407F}, {15.918F, 17.804F}, {15.819F, 18.186F}, {15.684F, 18.553F}, {15.515F, 18.903F}, {15.314F, 19.232F}, {15.084F, 19.541F}, {14.825F, 19.825F}, {14.541F, 20.084F}, {14.232F, 20.314F}, {13.903F, 20.515F}, {13.553F, 20.684F}, {13.186F, 20.819F}, {12.804F, 20.918F}, {12.407F, 20.979F}, {12.0F, 21.0F}, {11.593F, 20.979F}, {11.196F, 20.918F}, {10.814F, 20.819F}, {10.447F, 20.684F}, {10.097F, 20.515F}, {9.768F, 20.314F}, {9.459F, 20.084F}, {9.175F, 19.825F}, {8.916F, 19.541F}, {8.686F, 19.232F}, {8.485F, 18.903F}, {8.316F, 18.553F}, {8.181F, 18.186F}, {8.082F, 17.804F}, {8.021F, 17.407F}, {8.0F, 17.0F}, {8.009F, 16.721F}, {8.037F, 16.448F}, {8.082F, 16.18F}, {8.144F, 15.919F}, {8.222F, 15.665F}, {8.316F, 15.418F}, {8.426F, 15.18F}, {8.55F, 14.95F}, {8.688F, 14.73F}, {8.84F, 14.52F}, {9.004F, 14.32F}, {9.181F, 14.131F}, {9.37F, 13.954F}, {9.57F, 13.79F}, {9.78F, 13.638F}, {10.0F, 13.5F}};
	inline const Foundation::Vec2 temp_p1[] = {{13.5F, 17.0F}, {13.462F, 17.334F}, {13.351F, 17.651F}, {13.173F, 17.935F}, {12.935F, 18.173F}, {12.651F, 18.351F}, {12.334F, 18.462F}, {12.0F, 18.5F}, {11.666F, 18.462F}, {11.349F, 18.351F}, {11.065F, 18.173F}, {10.827F, 17.935F}, {10.649F, 17.651F}, {10.538F, 17.334F}, {10.5F, 17.0F}, {10.538F, 16.666F}, {10.649F, 16.349F}, {10.827F, 16.065F}, {11.065F, 15.827F}, {11.349F, 15.649F}, {11.666F, 15.538F}, {12.0F, 15.5F}, {12.334F, 15.538F}, {12.651F, 15.649F}, {12.935F, 15.827F}, {13.173F, 16.065F}, {13.351F, 16.349F}, {13.462F, 16.666F}};
	inline const SubPath temp_subs[] = {{temp_p0, 98, true}, {temp_p1, 28, true}};
	inline const GlyphDef temp_def{temp_subs, 2, false};

	inline const Foundation::Vec2 rain_p0[] = {{7.0F, 13.0F}, {6.593F, 12.979F}, {6.196F, 12.918F}, {5.814F, 12.819F}, {5.447F, 12.684F}, {5.097F, 12.515F}, {4.768F, 12.314F}, {4.459F, 12.084F}, {4.175F, 11.825F}, {3.916F, 11.541F}, {3.686F, 11.232F}, {3.485F, 10.903F}, {3.316F, 10.553F}, {3.181F, 10.186F}, {3.082F, 9.804F}, {3.021F, 9.407F}, {3.0F, 9.0F}, {3.021F, 8.593F}, {3.082F, 8.196F}, {3.181F, 7.814F}, {3.316F, 7.447F}, {3.485F, 7.097F}, {3.686F, 6.768F}, {3.916F, 6.459F}, {4.175F, 6.175F}, {4.459F, 5.916F}, {4.768F, 5.686F}, {5.097F, 5.485F}, {5.447F, 5.316F}, {5.814F, 5.181F}, {6.196F, 5.082F}, {6.593F, 5.021F}, {7.0F, 5.0F}, {7.089F, 4.686F}, {7.205F, 4.383F}, {7.346F, 4.092F}, {7.513F, 3.814F}, {7.702F, 3.551F}, {7.914F, 3.304F}, {8.147F, 3.074F}, {8.4F, 2.863F}, {8.672F, 2.671F}, {8.961F, 2.501F}, {9.267F, 2.353F}, {9.588F, 2.23F}, {9.922F, 2.131F}, {10.27F, 2.059F}, {10.63F, 2.015F}, {11.0F, 2.0F}, {11.407F, 2.021F}, {11.804F, 2.082F}, {12.186F, 2.181F}, {12.553F, 2.316F}, {12.903F, 2.485F}, {13.232F, 2.686F}, {13.541F, 2.916F}, {13.825F, 3.175F}, {14.084F, 3.459F}, {14.314F, 3.768F}, {14.515F, 4.097F}, {14.684F, 4.447F}, {14.819F, 4.814F}, {14.918F, 5.196F}, {14.979F, 5.593F}, {15.0F, 6.0F}, {16.0F, 6.0F}, {16.407F, 6.021F}, {16.804F, 6.082F}, {17.186F, 6.181F}, {17.553F, 6.316F}, {17.903F, 6.485F}, {18.232F, 6.686F}, {18.541F, 6.916F}, {18.825F, 7.175F}, {19.084F, 7.459F}, {19.314F, 7.768F}, {19.515F, 8.097F}, {19.684F, 8.447F}, {19.819F, 8.814F}, {19.918F, 9.196F}, {19.979F, 9.593F}, {20.0F, 10.0F}, {19.988F, 10.314F}, {19.954F, 10.618F}, {19.898F, 10.912F}, {19.82F, 11.195F}, {19.722F, 11.467F}, {19.604F, 11.728F}, {19.468F, 11.976F}, {19.313F, 12.212F}, {19.14F, 12.436F}, {18.95F, 12.646F}, {18.745F, 12.842F}, {18.523F, 13.023F}, {18.288F, 13.191F}, {18.038F, 13.343F}, {17.775F, 13.479F}, {17.5F, 13.6F}};
	inline const Foundation::Vec2 rain_p1[] = {{8.0F, 17.0F}, {7.0F, 19.0F}};
	inline const Foundation::Vec2 rain_p2[] = {{12.0F, 17.0F}, {11.0F, 19.0F}};
	inline const Foundation::Vec2 rain_p3[] = {{16.0F, 17.0F}, {15.0F, 19.0F}};
	inline const SubPath rain_subs[] = {{rain_p0, 98, false}, {rain_p1, 2, false}, {rain_p2, 2, false}, {rain_p3, 2, false}};
	inline const GlyphDef rain_def{rain_subs, 4, false};

	inline const Foundation::Vec2 map_p0[] = {{9.0F, 4.0F}, {3.0F, 6.0F}, {3.0F, 20.0F}, {9.0F, 18.0F}, {15.0F, 20.0F}, {21.0F, 18.0F}, {21.0F, 4.0F}, {15.0F, 6.0F}, {9.0F, 4.0F}};
	inline const Foundation::Vec2 map_p1[] = {{9.0F, 4.0F}, {9.0F, 18.0F}};
	inline const Foundation::Vec2 map_p2[] = {{15.0F, 6.0F}, {15.0F, 20.0F}};
	inline const SubPath map_subs[] = {{map_p0, 9, true}, {map_p1, 2, false}, {map_p2, 2, false}};
	inline const GlyphDef map_def{map_subs, 3, false};

	inline const Foundation::Vec2 home_p0[] = {{4.0F, 11.0F}, {12.0F, 4.0F}, {20.0F, 11.0F}};
	inline const Foundation::Vec2 home_p1[] = {{6.0F, 9.5F}, {6.0F, 20.0F}, {18.0F, 20.0F}, {18.0F, 9.5F}};
	inline const SubPath home_subs[] = {{home_p0, 3, false}, {home_p1, 4, false}};
	inline const GlyphDef home_def{home_subs, 2, false};

	inline const Foundation::Vec2 rocket_p0[] = {{12.0F, 2.0F}, {12.539F, 2.574F}, {13.033F, 3.17F}, {13.483F, 3.786F}, {13.891F, 4.422F}, {14.257F, 5.075F}, {14.584F, 5.744F}, {14.873F, 6.428F}, {15.125F, 7.125F}, {15.342F, 7.834F}, {15.525F, 8.553F}, {15.677F, 9.281F}, {15.797F, 10.016F}, {15.888F, 10.757F}, {15.951F, 11.502F}, {15.988F, 12.25F}, {16.0F, 13.0F}, {12.0F, 16.0F}, {8.0F, 13.0F}, {8.012F, 12.25F}, {8.049F, 11.502F}, {8.112F, 10.757F}, {8.203F, 10.016F}, {8.323F, 9.281F}, {8.475F, 8.553F}, {8.658F, 7.834F}, {8.875F, 7.125F}, {9.127F, 6.428F}, {9.416F, 5.744F}, {9.743F, 5.075F}, {10.109F, 4.422F}, {10.517F, 3.786F}, {10.967F, 3.17F}, {11.461F, 2.574F}, {12.0F, 2.0F}};
	inline const Foundation::Vec2 rocket_p1[] = {{8.0F, 13.0F}, {5.0F, 16.0F}, {5.0F, 20.0F}, {8.0F, 18.0F}};
	inline const Foundation::Vec2 rocket_p2[] = {{16.0F, 13.0F}, {19.0F, 16.0F}, {19.0F, 20.0F}, {16.0F, 18.0F}};
	inline const Foundation::Vec2 rocket_p3[] = {{13.6F, 9.0F}, {13.56F, 9.356F}, {13.442F, 9.694F}, {13.251F, 9.998F}, {12.998F, 10.251F}, {12.694F, 10.442F}, {12.356F, 10.56F}, {12.0F, 10.6F}, {11.644F, 10.56F}, {11.306F, 10.442F}, {11.002F, 10.251F}, {10.749F, 9.998F}, {10.558F, 9.694F}, {10.44F, 9.356F}, {10.4F, 9.0F}, {10.44F, 8.644F}, {10.558F, 8.306F}, {10.749F, 8.002F}, {11.002F, 7.749F}, {11.306F, 7.558F}, {11.644F, 7.44F}, {12.0F, 7.4F}, {12.356F, 7.44F}, {12.694F, 7.558F}, {12.998F, 7.749F}, {13.251F, 8.002F}, {13.442F, 8.306F}, {13.56F, 8.644F}};
	inline const SubPath rocket_subs[] = {{rocket_p0, 35, true}, {rocket_p1, 4, false}, {rocket_p2, 4, false}, {rocket_p3, 28, true}};
	inline const GlyphDef rocket_def{rocket_subs, 4, false};

	inline const Foundation::Vec2 star_p0[] = {{12.0F, 3.0F}, {14.6F, 9.2F}, {21.0F, 9.6F}, {16.0F, 13.8F}, {17.6F, 20.0F}, {12.0F, 16.4F}, {6.4F, 20.0F}, {8.0F, 13.8F}, {3.0F, 9.6F}, {9.4F, 9.2F}};
	inline const SubPath star_subs[] = {{star_p0, 10, true}};
	inline const GlyphDef star_def{star_subs, 1, true};

	inline const Foundation::Vec2 skull_p0[] = {{5.0F, 11.0F}, {5.036F, 10.191F}, {5.141F, 9.414F}, {5.312F, 8.674F}, {5.545F, 7.972F}, {5.838F, 7.311F}, {6.187F, 6.693F}, {6.587F, 6.122F}, {7.038F, 5.6F}, {7.533F, 5.129F}, {8.071F, 4.713F}, {8.648F, 4.353F}, {9.261F, 4.053F}, {9.906F, 3.815F}, {10.579F, 3.642F}, {11.279F, 3.536F}, {12.0F, 3.5F}, {12.721F, 3.536F}, {13.421F, 3.642F}, {14.094F, 3.815F}, {14.739F, 4.053F}, {15.352F, 4.353F}, {15.929F, 4.713F}, {16.467F, 5.129F}, {16.963F, 5.6F}, {17.413F, 6.122F}, {17.813F, 6.693F}, {18.162F, 7.311F}, {18.455F, 7.972F}, {18.688F, 8.674F}, {18.859F, 9.414F}, {18.964F, 10.191F}, {19.0F, 11.0F}, {18.989F, 11.369F}, {18.955F, 11.727F}, {18.901F, 12.072F}, {18.828F, 12.406F}, {18.738F, 12.729F}, {18.631F, 13.039F}, {18.51F, 13.338F}, {18.375F, 13.625F}, {18.229F, 13.9F}, {18.072F, 14.164F}, {17.907F, 14.416F}, {17.734F, 14.656F}, {17.556F, 14.885F}, {17.373F, 15.102F}, {17.187F, 15.307F}, {17.0F, 15.5F}, {17.0F, 18.0F}, {7.0F, 18.0F}, {7.0F, 15.5F}, {6.813F, 15.307F}, {6.627F, 15.102F}, {6.444F, 14.885F}, {6.266F, 14.656F}, {6.093F, 14.416F}, {5.928F, 14.164F}, {5.771F, 13.9F}, {5.625F, 13.625F}, {5.49F, 13.338F}, {5.369F, 13.039F}, {5.262F, 12.729F}, {5.172F, 12.406F}, {5.099F, 12.072F}, {5.045F, 11.727F}, {5.011F, 11.369F}, {5.0F, 11.0F}};
	inline const Foundation::Vec2 skull_p1[] = {{10.0F, 18.0F}, {10.0F, 20.0F}};
	inline const Foundation::Vec2 skull_p2[] = {{14.0F, 18.0F}, {14.0F, 20.0F}};
	inline const Foundation::Vec2 skull_p3[] = {{12.0F, 18.0F}, {12.0F, 20.5F}};
	inline const Foundation::Vec2 skull_p4[] = {{10.6F, 11.0F}, {10.565F, 11.312F}, {10.461F, 11.607F}, {10.295F, 11.873F}, {10.073F, 12.095F}, {9.807F, 12.261F}, {9.512F, 12.365F}, {9.2F, 12.4F}, {8.888F, 12.365F}, {8.593F, 12.261F}, {8.327F, 12.095F}, {8.105F, 11.873F}, {7.939F, 11.607F}, {7.835F, 11.312F}, {7.8F, 11.0F}, {7.835F, 10.688F}, {7.939F, 10.393F}, {8.105F, 10.127F}, {8.327F, 9.905F}, {8.593F, 9.739F}, {8.888F, 9.635F}, {9.2F, 9.6F}, {9.512F, 9.635F}, {9.807F, 9.739F}, {10.073F, 9.905F}, {10.295F, 10.127F}, {10.461F, 10.393F}, {10.565F, 10.688F}};
	inline const Foundation::Vec2 skull_p5[] = {{16.2F, 11.0F}, {16.165F, 11.312F}, {16.061F, 11.607F}, {15.895F, 11.873F}, {15.673F, 12.095F}, {15.407F, 12.261F}, {15.112F, 12.365F}, {14.8F, 12.4F}, {14.488F, 12.365F}, {14.193F, 12.261F}, {13.927F, 12.095F}, {13.705F, 11.873F}, {13.539F, 11.607F}, {13.435F, 11.312F}, {13.4F, 11.0F}, {13.435F, 10.688F}, {13.539F, 10.393F}, {13.705F, 10.127F}, {13.927F, 9.905F}, {14.193F, 9.739F}, {14.488F, 9.635F}, {14.8F, 9.6F}, {15.112F, 9.635F}, {15.407F, 9.739F}, {15.673F, 9.905F}, {15.895F, 10.127F}, {16.061F, 10.393F}, {16.165F, 10.688F}};
	inline const SubPath skull_subs[] = {{skull_p0, 68, true}, {skull_p1, 2, false}, {skull_p2, 2, false}, {skull_p3, 2, false}, {skull_p4, 28, true}, {skull_p5, 28, true}};
	inline const GlyphDef skull_def{skull_subs, 6, false};

	inline const Foundation::Vec2 refresh_p0[] = {{20.0F, 12.0F}, {19.958F, 12.815F}, {19.837F, 13.607F}, {19.639F, 14.372F}, {19.369F, 15.106F}, {19.031F, 15.805F}, {18.629F, 16.465F}, {18.167F, 17.081F}, {17.65F, 17.65F}, {17.081F, 18.167F}, {16.465F, 18.629F}, {15.805F, 19.031F}, {15.106F, 19.369F}, {14.372F, 19.639F}, {13.607F, 19.837F}, {12.815F, 19.958F}, {12.0F, 20.0F}, {11.185F, 19.958F}, {10.393F, 19.837F}, {9.628F, 19.639F}, {8.894F, 19.369F}, {8.195F, 19.031F}, {7.535F, 18.629F}, {6.919F, 18.167F}, {6.35F, 17.65F}, {5.833F, 17.081F}, {5.371F, 16.465F}, {4.969F, 15.805F}, {4.631F, 15.106F}, {4.361F, 14.372F}, {4.163F, 13.607F}, {4.042F, 12.815F}, {4.0F, 12.0F}, {4.042F, 11.185F}, {4.163F, 10.393F}, {4.361F, 9.628F}, {4.631F, 8.894F}, {4.969F, 8.195F}, {5.371F, 7.535F}, {5.833F, 6.919F}, {6.35F, 6.35F}, {6.919F, 5.833F}, {7.535F, 5.371F}, {8.195F, 4.969F}, {8.894F, 4.631F}, {9.628F, 4.361F}, {10.393F, 4.163F}, {11.185F, 4.042F}, {12.0F, 4.0F}, {12.558F, 4.019F}, {13.105F, 4.073F}, {13.64F, 4.163F}, {14.163F, 4.288F}, {14.671F, 4.444F}, {15.164F, 4.633F}, {15.641F, 4.852F}, {16.1F, 5.1F}, {16.54F, 5.376F}, {16.961F, 5.68F}, {17.36F, 6.009F}, {17.738F, 6.363F}, {18.091F, 6.74F}, {18.42F, 7.139F}, {18.724F, 7.56F}, {19.0F, 8.0F}};
	inline const Foundation::Vec2 refresh_p1[] = {{19.0F, 4.0F}, {19.0F, 8.0F}, {15.0F, 8.0F}};
	inline const SubPath refresh_subs[] = {{refresh_p0, 65, false}, {refresh_p1, 3, false}};
	inline const GlyphDef refresh_def{refresh_subs, 2, false};

	inline const Foundation::Vec2 eye_p0[] = {{2.0F, 12.0F}, {2.395F, 11.18F}, {2.828F, 10.407F}, {3.297F, 9.683F}, {3.802F, 9.008F}, {4.339F, 8.382F}, {4.909F, 7.808F}, {5.509F, 7.284F}, {6.138F, 6.813F}, {6.793F, 6.394F}, {7.474F, 6.028F}, {8.178F, 5.717F}, {8.905F, 5.461F}, {9.652F, 5.26F}, {10.418F, 5.116F}, {11.201F, 5.029F}, {12.0F, 5.0F}, {12.799F, 5.029F}, {13.582F, 5.116F}, {14.348F, 5.26F}, {15.095F, 5.461F}, {15.822F, 5.717F}, {16.526F, 6.028F}, {17.207F, 6.394F}, {17.863F, 6.813F}, {18.491F, 7.284F}, {19.091F, 7.808F}, {19.661F, 8.382F}, {20.198F, 9.008F}, {20.703F, 9.683F}, {21.172F, 10.407F}, {21.605F, 11.18F}, {22.0F, 12.0F}, {21.605F, 12.82F}, {21.172F, 13.593F}, {20.703F, 14.317F}, {20.198F, 14.992F}, {19.661F, 15.618F}, {19.091F, 16.192F}, {18.491F, 16.716F}, {17.863F, 17.188F}, {17.207F, 17.606F}, {16.526F, 17.972F}, {15.822F, 18.283F}, {15.095F, 18.539F}, {14.348F, 18.74F}, {13.582F, 18.884F}, {12.799F, 18.971F}, {12.0F, 19.0F}, {11.201F, 18.971F}, {10.418F, 18.884F}, {9.652F, 18.74F}, {8.905F, 18.539F}, {8.178F, 18.283F}, {7.474F, 17.972F}, {6.793F, 17.606F}, {6.138F, 17.188F}, {5.509F, 16.716F}, {4.909F, 16.192F}, {4.339F, 15.618F}, {3.802F, 14.992F}, {3.297F, 14.317F}, {2.828F, 13.593F}, {2.395F, 12.82F}, {2.0F, 12.0F}};
	inline const Foundation::Vec2 eye_p1[] = {{15.0F, 12.0F}, {14.925F, 12.668F}, {14.703F, 13.302F}, {14.345F, 13.87F}, {13.87F, 14.345F}, {13.302F, 14.703F}, {12.668F, 14.925F}, {12.0F, 15.0F}, {11.332F, 14.925F}, {10.698F, 14.703F}, {10.13F, 14.345F}, {9.655F, 13.87F}, {9.297F, 13.302F}, {9.075F, 12.668F}, {9.0F, 12.0F}, {9.075F, 11.332F}, {9.297F, 10.698F}, {9.655F, 10.13F}, {10.13F, 9.655F}, {10.698F, 9.297F}, {11.332F, 9.075F}, {12.0F, 9.0F}, {12.668F, 9.075F}, {13.302F, 9.297F}, {13.87F, 9.655F}, {14.345F, 10.13F}, {14.703F, 10.698F}, {14.925F, 11.332F}};
	inline const SubPath eye_subs[] = {{eye_p0, 65, true}, {eye_p1, 28, true}};
	inline const GlyphDef eye_def{eye_subs, 2, false};

	inline const Foundation::Vec2 clock_p0[] = {{12.0F, 7.0F}, {12.0F, 12.0F}, {15.5F, 14.0F}};
	inline const Foundation::Vec2 clock_p1[] = {{21.0F, 12.0F}, {20.774F, 14.003F}, {20.109F, 15.905F}, {19.036F, 17.611F}, {17.611F, 19.036F}, {15.905F, 20.109F}, {14.003F, 20.774F}, {12.0F, 21.0F}, {9.997F, 20.774F}, {8.095F, 20.109F}, {6.389F, 19.036F}, {4.964F, 17.611F}, {3.891F, 15.905F}, {3.226F, 14.003F}, {3.0F, 12.0F}, {3.226F, 9.997F}, {3.891F, 8.095F}, {4.964F, 6.389F}, {6.389F, 4.964F}, {8.095F, 3.891F}, {9.997F, 3.226F}, {12.0F, 3.0F}, {14.003F, 3.226F}, {15.905F, 3.891F}, {17.611F, 4.964F}, {19.036F, 6.389F}, {20.109F, 8.095F}, {20.774F, 9.997F}};
	inline const SubPath clock_subs[] = {{clock_p0, 3, false}, {clock_p1, 28, true}};
	inline const GlyphDef clock_def{clock_subs, 2, false};

	inline const Foundation::Vec2 list_p0[] = {{8.0F, 6.0F}, {20.0F, 6.0F}};
	inline const Foundation::Vec2 list_p1[] = {{8.0F, 12.0F}, {20.0F, 12.0F}};
	inline const Foundation::Vec2 list_p2[] = {{8.0F, 18.0F}, {20.0F, 18.0F}};
	inline const Foundation::Vec2 list_p3[] = {{4.0F, 6.0F}, {4.0F, 6.5F}};
	inline const Foundation::Vec2 list_p4[] = {{4.0F, 12.0F}, {4.0F, 12.5F}};
	inline const Foundation::Vec2 list_p5[] = {{4.0F, 18.0F}, {4.0F, 18.5F}};
	inline const SubPath list_subs[] = {{list_p0, 2, false}, {list_p1, 2, false}, {list_p2, 2, false}, {list_p3, 2, false}, {list_p4, 2, false}, {list_p5, 2, false}};
	inline const GlyphDef list_def{list_subs, 6, false};

	inline const Foundation::Vec2 layers_p0[] = {{12.0F, 3.0F}, {21.0F, 8.0F}, {12.0F, 13.0F}, {3.0F, 8.0F}};
	inline const Foundation::Vec2 layers_p1[] = {{3.0F, 13.0F}, {12.0F, 18.0F}, {21.0F, 13.0F}};
	inline const Foundation::Vec2 layers_p2[] = {{3.0F, 17.0F}, {12.0F, 22.0F}, {21.0F, 17.0F}};
	inline const SubPath layers_subs[] = {{layers_p0, 4, true}, {layers_p1, 3, false}, {layers_p2, 3, false}};
	inline const GlyphDef layers_def{layers_subs, 3, false};

	inline const Foundation::Vec2 save_p0[] = {{5.0F, 5.0F}, {16.0F, 5.0F}, {19.0F, 8.0F}, {19.0F, 19.0F}, {5.0F, 19.0F}};
	inline const Foundation::Vec2 save_p1[] = {{8.0F, 5.0F}, {8.0F, 9.0F}, {15.0F, 9.0F}};
	inline const Foundation::Vec2 save_p2[] = {{8.0F, 19.0F}, {8.0F, 14.0F}, {16.0F, 14.0F}, {16.0F, 19.0F}};
	inline const SubPath save_subs[] = {{save_p0, 5, true}, {save_p1, 3, false}, {save_p2, 4, false}};
	inline const GlyphDef save_def{save_subs, 3, false};

	inline const Foundation::Vec2 bolt_p0[] = {{13.0F, 2.0F}, {4.0F, 14.0F}, {11.0F, 14.0F}, {10.0F, 22.0F}, {19.0F, 10.0F}, {12.0F, 10.0F}};
	inline const SubPath bolt_subs[] = {{bolt_p0, 6, true}};
	inline const GlyphDef bolt_def{bolt_subs, 1, true};

	inline const Foundation::Vec2 shirt_p0[] = {{9.0F, 4.0F}, {6.0F, 4.0F}, {3.0F, 8.0F}, {6.0F, 11.0F}, {7.5F, 9.5F}, {7.5F, 20.0F}, {16.5F, 20.0F}, {16.5F, 9.5F}, {18.0F, 11.0F}, {21.0F, 8.0F}, {18.0F, 4.0F}, {15.0F, 4.0F}, {14.985F, 4.314F}, {14.941F, 4.617F}, {14.869F, 4.908F}, {14.77F, 5.186F}, {14.647F, 5.449F}, {14.499F, 5.696F}, {14.329F, 5.926F}, {14.138F, 6.138F}, {13.926F, 6.329F}, {13.696F, 6.499F}, {13.449F, 6.647F}, {13.186F, 6.77F}, {12.908F, 6.869F}, {12.617F, 6.941F}, {12.314F, 6.985F}, {12.0F, 7.0F}, {11.686F, 6.985F}, {11.383F, 6.941F}, {11.092F, 6.869F}, {10.814F, 6.77F}, {10.551F, 6.647F}, {10.304F, 6.499F}, {10.074F, 6.329F}, {9.863F, 6.138F}, {9.671F, 5.926F}, {9.501F, 5.696F}, {9.353F, 5.449F}, {9.23F, 5.186F}, {9.131F, 4.908F}, {9.059F, 4.617F}, {9.015F, 4.314F}, {9.0F, 4.0F}};
	inline const SubPath shirt_subs[] = {{shirt_p0, 44, true}};
	inline const GlyphDef shirt_def{shirt_subs, 1, false};

	inline const Foundation::Vec2 pants_p0[] = {{8.0F, 3.0F}, {16.0F, 3.0F}, {16.5F, 8.0F}, {17.0F, 21.0F}, {13.5F, 21.0F}, {12.5F, 11.0F}, {12.0F, 11.0F}, {11.5F, 11.0F}, {10.5F, 21.0F}, {7.0F, 21.0F}, {7.5F, 8.0F}};
	inline const SubPath pants_subs[] = {{pants_p0, 11, true}};
	inline const GlyphDef pants_def{pants_subs, 1, false};

	inline const Foundation::Vec2 boot_p0[] = {{9.0F, 3.0F}, {12.5F, 3.0F}, {12.5F, 12.0F}, {12.506F, 12.182F}, {12.523F, 12.352F}, {12.553F, 12.51F}, {12.594F, 12.656F}, {12.646F, 12.791F}, {12.711F, 12.914F}, {12.787F, 13.025F}, {12.875F, 13.125F}, {12.975F, 13.213F}, {13.086F, 13.289F}, {13.209F, 13.354F}, {13.344F, 13.406F}, {13.49F, 13.447F}, {13.648F, 13.477F}, {13.818F, 13.494F}, {14.0F, 13.5F}, {18.0F, 13.5F}, {18.314F, 13.515F}, {18.617F, 13.559F}, {18.908F, 13.631F}, {19.186F, 13.73F}, {19.449F, 13.853F}, {19.696F, 14.001F}, {19.926F, 14.171F}, {20.138F, 14.363F}, {20.329F, 14.574F}, {20.499F, 14.804F}, {20.647F, 15.051F}, {20.77F, 15.314F}, {20.869F, 15.592F}, {20.941F, 15.883F}, {20.985F, 16.186F}, {21.0F, 16.5F}, {21.0F, 20.0F}, {9.0F, 20.0F}};
	inline const SubPath boot_subs[] = {{boot_p0, 38, true}};
	inline const GlyphDef boot_def{boot_subs, 1, false};

	struct Entry {
		std::string_view name;
		const GlyphDef*	 def;
	};

	inline const Entry registry[] = {
		{"play", &play_def},
		{"pause", &pause_def},
		{"fast", &fast_def},
		{"veryFast", &veryFast_def},
		{"plus", &plus_def},
		{"minus", &minus_def},
		{"close", &close_def},
		{"gear", &gear_def},
		{"menu", &menu_def},
		{"chevronLeft", &chevronLeft_def},
		{"chevronRight", &chevronRight_def},
		{"chevronUp", &chevronUp_def},
		{"chevronDown", &chevronDown_def},
		{"arrowRight", &arrowRight_def},
		{"check", &check_def},
		{"alert", &alert_def},
		{"info", &info_def},
		{"globe", &globe_def},
		{"crosshair", &crosshair_def},
		{"user", &user_def},
		{"users", &users_def},
		{"heart", &heart_def},
		{"food", &food_def},
		{"water", &water_def},
		{"energy", &energy_def},
		{"rest", &rest_def},
		{"hammer", &hammer_def},
		{"box", &box_def},
		{"leaf", &leaf_def},
		{"search", &search_def},
		{"lock", &lock_def},
		{"dice", &dice_def},
		{"sprout", &sprout_def},
		{"mountain", &mountain_def},
		{"temp", &temp_def},
		{"rain", &rain_def},
		{"map", &map_def},
		{"home", &home_def},
		{"rocket", &rocket_def},
		{"star", &star_def},
		{"skull", &skull_def},
		{"refresh", &refresh_def},
		{"eye", &eye_def},
		{"clock", &clock_def},
		{"list", &list_def},
		{"layers", &layers_def},
		{"save", &save_def},
		{"bolt", &bolt_def},
		{"shirt", &shirt_def},
		{"pants", &pants_def},
		{"boot", &boot_def}};

	inline const GlyphDef* find(std::string_view name) {
		for (const Entry& e : registry) {
			if (e.name == name) return e.def;
		}
		return nullptr;
	}

	inline constexpr int count = 51;

} // namespace UI::Icons


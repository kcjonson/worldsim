#pragma once

// GENERATED from docs/ui-prototype/src/design-system/tokens.css via tokens.json
// by docs/ui-prototype/scripts/gen-cpp-theme.mjs. Do not edit by hand.
//
// Salvage design tokens for the C++ UI. Compile-time constexpr. Names mirror
// the CSS custom properties: --space-3 -> UI::space_3, --accent -> UI::accent.

#include "graphics/Color.h"

namespace UI {

	// Colors
	inline constexpr Foundation::Color bg_void{0.0275F, 0.0314F, 0.0431F, 1.0F};
	inline constexpr Foundation::Color bg_base{0.0431F, 0.051F, 0.0706F, 1.0F};
	inline constexpr Foundation::Color bg_panel{0.0706F, 0.0824F, 0.1098F, 1.0F};
	inline constexpr Foundation::Color bg_panel_raised{0.0941F, 0.1098F, 0.1412F, 1.0F};
	inline constexpr Foundation::Color bg_inset{0.0314F, 0.0353F, 0.051F, 1.0F};
	inline constexpr Foundation::Color bg_hover{1.0F, 1.0F, 1.0F, 0.045F};
	inline constexpr Foundation::Color bg_active{0.9098F, 0.6392F, 0.2431F, 0.1F};
	inline constexpr Foundation::Color line_hairline{0.7059F, 0.7843F, 0.902F, 0.1F};
	inline constexpr Foundation::Color line_edge{0.5882F, 0.7059F, 0.8627F, 0.2F};
	inline constexpr Foundation::Color line_strong{0.6667F, 0.7647F, 0.902F, 0.36F};
	inline constexpr Foundation::Color accent{0.9098F, 0.6392F, 0.2431F, 1.0F};
	inline constexpr Foundation::Color accent_bright{1.0F, 0.7725F, 0.4196F, 1.0F};
	inline constexpr Foundation::Color accent_dim{0.5412F, 0.3882F, 0.149F, 1.0F};
	inline constexpr Foundation::Color accent_contrast{0.102F, 0.0706F, 0.0235F, 1.0F};
	inline constexpr Foundation::Color accent_glow{0.9098F, 0.6392F, 0.2431F, 0.45F};
	inline constexpr Foundation::Color data{0.2706F, 0.7804F, 0.7529F, 1.0F};
	inline constexpr Foundation::Color data_bright{0.4824F, 0.902F, 0.8745F, 1.0F};
	inline constexpr Foundation::Color data_dim{0.1647F, 0.4314F, 0.4157F, 1.0F};
	inline constexpr Foundation::Color data_glow{0.2706F, 0.7804F, 0.7529F, 0.4F};
	inline constexpr Foundation::Color text_bright{0.9569F, 0.9333F, 0.8863F, 1.0F};
	inline constexpr Foundation::Color text{0.7765F, 0.7843F, 0.8078F, 1.0F};
	inline constexpr Foundation::Color text_dim{0.5412F, 0.5608F, 0.6078F, 1.0F};
	inline constexpr Foundation::Color text_faint{0.3529F, 0.3725F, 0.4196F, 1.0F};
	inline constexpr Foundation::Color text_disabled{0.2667F, 0.2824F, 0.3137F, 0.2F};
	inline constexpr Foundation::Color status_ok{0.3725F, 0.7216F, 0.4784F, 1.0F};
	inline constexpr Foundation::Color status_warn{0.9098F, 0.6392F, 0.2431F, 1.0F};
	inline constexpr Foundation::Color status_crit{0.8784F, 0.3255F, 0.2353F, 1.0F};
	inline constexpr Foundation::Color status_info{0.2706F, 0.7804F, 0.7529F, 1.0F};
	inline constexpr Foundation::Color scrim{0.0196F, 0.0235F, 0.0392F, 0.72F};

	// Spacing (px)
	inline constexpr float space_0 = 0.0F;
	inline constexpr float space_0_5 = 2.0F;
	inline constexpr float space_1 = 4.0F;
	inline constexpr float space_1_5 = 6.0F;
	inline constexpr float space_2 = 8.0F;
	inline constexpr float space_3 = 12.0F;
	inline constexpr float space_4 = 16.0F;
	inline constexpr float space_5 = 20.0F;
	inline constexpr float space_6 = 24.0F;
	inline constexpr float space_8 = 32.0F;
	inline constexpr float space_10 = 40.0F;
	inline constexpr float space_12 = 48.0F;
	inline constexpr float space_16 = 64.0F;
	inline constexpr float space_20 = 80.0F;
	inline constexpr float space_24 = 96.0F;

	// Radius (px)
	inline constexpr float r_0 = 0.0F;
	inline constexpr float r_xs = 1.0F;
	inline constexpr float r_sm = 2.0F;
	inline constexpr float r_md = 4.0F;
	inline constexpr float r_lg = 8.0F;
	inline constexpr float r_xl = 14.0F;
	inline constexpr float r_pill = 999.0F;

	// Border widths (px)
	inline constexpr float bw_hair = 1.0F;
	inline constexpr float bw = 1.0F;
	inline constexpr float bw_thick = 2.0F;
	inline constexpr float border_width = 1.0F;

	// Font sizes (px)
	inline constexpr float fs_2xs = 10.0F;
	inline constexpr float fs_xs = 11.0F;
	inline constexpr float fs_sm = 12.0F;
	inline constexpr float fs_base = 13.0F;
	inline constexpr float fs_md = 15.0F;
	inline constexpr float fs_lg = 18.0F;
	inline constexpr float fs_xl = 22.0F;
	inline constexpr float fs_2xl = 28.0F;
	inline constexpr float fs_3xl = 38.0F;
	inline constexpr float fs_4xl = 52.0F;
	inline constexpr float fs_5xl = 72.0F;

	// Line heights
	inline constexpr float lh_tight = 1.1F;
	inline constexpr float lh = 1.4F;
	inline constexpr float lh_loose = 1.6F;

	// Letter spacing (em)
	inline constexpr float ls_tight = -0.01F;
	inline constexpr float ls = 0.0F;
	inline constexpr float ls_wide = 0.08F;
	inline constexpr float ls_wider = 0.16F;
	inline constexpr float ls_widest = 0.3F;

	// Motion (ms)
	inline constexpr float dur_fast = 120.0F;
	inline constexpr float dur = 200.0F;
	inline constexpr float dur_slow = 360.0F;

	// Z layers
	inline constexpr int z_base = 0;
	inline constexpr int z_panel = 10;
	inline constexpr int z_raised = 20;
	inline constexpr int z_overlay = 100;
	inline constexpr int z_modal = 200;
	inline constexpr int z_toast = 300;
	inline constexpr int z_tooltip = 400;

	// Density & texture
	inline constexpr float density = 1.0F;
	inline constexpr float scanline_opacity = 0.05F;
	inline constexpr float grain_opacity = 0.04F;
	inline constexpr float vignette_opacity = 0.55F;

	// Typography (numeric)
	inline constexpr float title_weight = 600.0F;
} // namespace UI

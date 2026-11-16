#pragma once

// The OpenGL version every worldsim context asks for, core and forward compatible: the ground pass's
// shaders (tile.vert, tile.frag, land.glsl) are GLSL 4.10. The apps' windows take it through salal's
// WindowConfig::glVersion; asset-cli's and the GL tests' hidden contexts hint it themselves.

namespace worldview {

	inline constexpr int kGlMajor = 4;
	inline constexpr int kGlMinor = 1;

} // namespace worldview

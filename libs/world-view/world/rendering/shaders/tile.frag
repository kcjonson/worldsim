#version 330 core

// Tile pass fragment shader: the land pass (land.glsl) paints the chunk's ground
// from its render tiles, every surface boundary the isoline of a warped field, then
// water and shore go on top from the chunk's terrain distance field (water.glsl).
// A Water tile carries its bed surface, so under and around the waterline the
// ground reads as the adjacent land.

#include "includes/land.glsl"
#include "includes/water.glsl"

in vec2 v_worldPos;
in vec2 v_localPos;

out vec4 FragColor;

void main() {
	vec3 ground = landColor(v_localPos, u_metersPerPixel);
	if (!u_hasWater) {
		FragColor = vec4(ground, 1.0);
		return;
	}
	FragColor = vec4(shadeWater(ground, v_localPos, v_worldPos), 1.0);
}

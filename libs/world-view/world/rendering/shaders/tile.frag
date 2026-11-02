#version 410 core

// Tile pass fragment shader: the land pass (land.glsl) paints the chunk's ground
// from its render tiles, every surface boundary the isoline of a warped field, then
// water and shore go on top from the chunk's terrain distance field (water.glsl),
// which asks for the ground only where some of it shows. A Water tile carries its
// bed surface, so under and around the waterline the ground reads as the adjacent
// land.

#include "includes/land.glsl"
#include "includes/water.glsl"

in vec2 v_worldPos;
in vec2 v_localPos;

out vec4 FragColor;

void main() {
	WaterPixel water = u_hasWater ? waterPixel(v_localPos, v_worldPos) : dryPixel();
	// Fully under water the ground doesn't show (shadeWater mixes it out by 1).
	vec3 ground = water.water < 1.0 ? landColor(v_localPos) : vec3(0.0);
	FragColor	= vec4(water.shore ? shadeWater(water, ground, v_localPos, v_worldPos) : ground, 1.0);
}

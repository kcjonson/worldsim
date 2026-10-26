#version 330 core

// Tile pass vertex shader - one unit quad per chunk, expanded to world space.
// All per-tile data lives in a chunk tile-data texture sampled by tile.frag,
// so the vertex load is constant regardless of zoom or visible tile count.

layout(location = 0) in vec2 a_corner; // unit quad corner in [0,1]^2

uniform mat4 u_projection;      // screen-space ortho (0..w, h..0)
uniform vec2 u_chunkOrigin;     // chunk origin in world meters
uniform float u_chunkWorldSize; // chunk extent in world meters (kChunkSize * kTileSize)
uniform vec2 u_cameraPos;       // camera center in world meters
uniform float u_cameraZoom;
uniform float u_pixelsPerMeter;
uniform vec2 u_viewportSize;    // logical pixels

out vec2 v_worldPos; // world meters, for world-space noise (continuous across chunks)
out vec2 v_localPos; // chunk-local meters, for texture lookups (full precision far from the origin)

void main() {
	vec2 local = a_corner * u_chunkWorldSize;
	vec2 world = u_chunkOrigin + local;
	v_worldPos = world;
	v_localPos = local;
	vec2 screen = (world - u_cameraPos) * (u_pixelsPerMeter * u_cameraZoom) + u_viewportSize * 0.5;
	gl_Position = u_projection * vec4(screen, 0.0, 1.0);
}

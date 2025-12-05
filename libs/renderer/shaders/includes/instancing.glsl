// instancing.glsl - GPU Instancing support for uber shader
//
// Provides per-instance data attributes and world-to-screen transformation
// for efficient rendering of many instances of the same mesh.

// Instance data attributes (per-instance, divisor = 1)
// Layout:
//   a_instanceData1.xy = world position
//   a_instanceData1.z  = rotation (radians)
//   a_instanceData1.w  = scale
//   a_instanceData2    = color tint (RGBA)
layout(location = 6) in vec4 a_instanceData1;
layout(location = 7) in vec4 a_instanceData2;

// Instancing uniforms (camera and viewport for world→screen transform)
uniform vec2 u_cameraPosition;   // Camera world position (center of view)
uniform float u_cameraZoom;      // Zoom level (1.0 = normal, >1 = zoomed in)
uniform float u_pixelsPerMeter;  // World scale (pixels per world unit)
uniform vec2 u_viewportSize;     // Viewport dimensions in pixels
uniform int u_instanced;         // Flag: 1 for instanced rendering path, 0 otherwise

/// Transform a local mesh vertex to screen space using instance data.
/// @param localPos Vertex position in local mesh space (usually centered around origin)
/// @return Screen-space position
vec2 instanceToScreen(vec2 localPos) {
    // Unpack instance data
    vec2 worldPos = a_instanceData1.xy;
    float rotation = a_instanceData1.z;
    float scale = a_instanceData1.w;

    // Apply rotation around origin
    float cosR = cos(rotation);
    float sinR = sin(rotation);
    vec2 rotated = vec2(
        localPos.x * cosR - localPos.y * sinR,
        localPos.x * sinR + localPos.y * cosR
    );

    // Apply scale and translate to world position
    vec2 worldVertex = rotated * scale + worldPos;

    // Transform world → screen
    // Screen center is viewport center, camera position is world center
    float viewScale = u_pixelsPerMeter * u_cameraZoom;
    vec2 screenPos = (worldVertex - u_cameraPosition) * viewScale + u_viewportSize * 0.5;

    return screenPos;
}

/// Get the instance color tint
vec4 getInstanceColorTint() {
    return a_instanceData2;
}

#version 330 core

in vec2 vTexCoord;
in vec4 vColor;

out vec4 FragColor;

uniform sampler2D msdfAtlas;
uniform float pixelRange;  // Distance field range in screen pixels

// Median of three values (for MSDF)
float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

// Screen-space partial derivatives to calculate distance field scale
float screenPxRange() {
    vec2 unitRange = vec2(pixelRange) / vec2(textureSize(msdfAtlas, 0));
    vec2 screenTexSize = vec2(1.0) / fwidth(vTexCoord);
    return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

void main() {
    // Sample the multi-channel distance field
    vec3 msd = texture(msdfAtlas, vTexCoord).rgb;

    // Calculate signed distance using median-of-three
    float sd = median(msd.r, msd.g, msd.b);

    // Convert to screen-space distance
    float screenPxDistance = screenPxRange() * (sd - 0.5);

    // Anti-aliased opacity (smoothstep around 0)
    float opacity = clamp(screenPxDistance + 0.5, 0.0, 1.0);

    // Output color with opacity
    FragColor = vec4(vColor.rgb, vColor.a * opacity);
}

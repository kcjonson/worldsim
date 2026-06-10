#version 330 core

in vec3 v_worldPos;
in vec3 v_normal;
in vec2 v_uv;

uniform sampler2D u_colorTex;   // per-rhombus data texture
uniform vec3      u_sunDir;     // world-space, normalised
uniform vec3      u_cameraPos;

out vec4 fragColor;

void main() {
    vec4 baseColor = texture(u_colorTex, v_uv);

    // Diffuse + ambient
    float nDotL   = max(dot(v_normal, u_sunDir), 0.0);
    float ambient = 0.18;
    float diffuse = nDotL * 0.82;
    float light   = ambient + diffuse;

    // Atmosphere rim (Fresnel-ish)
    vec3  viewDir  = normalize(u_cameraPos - v_worldPos);
    float rim      = 1.0 - max(dot(v_normal, viewDir), 0.0);
    rim = pow(rim, 4.0);

    // Darken night side near terminator
    float terminator = smoothstep(-0.12, 0.12, dot(v_normal, u_sunDir));

    vec3 litColor = baseColor.rgb * light;

    // Blue atmosphere glow on rim
    vec3 atmColor  = vec3(0.25, 0.45, 0.90);
    float atmStrength = rim * (0.3 + 0.5 * terminator);
    litColor = mix(litColor, atmColor, atmStrength * 0.45);

    // Very slight night-side darkening beyond terminator
    litColor *= mix(0.05, 1.0, terminator + 0.1);
    litColor = clamp(litColor, 0.0, 1.0);

    fragColor = vec4(litColor, 1.0);
}

#version 330 core

in vec3 v_worldPos;
in vec3 v_normal;
in vec2 v_uv;

uniform sampler2D u_baseTex;  // per-rhombus RGBA8 + mips; one texel per tile (texSize == n)

uniform float u_n;            // grid subdivision

uniform vec3  u_sunDir;       // world-space, normalised
uniform vec3  u_cameraPos;

out vec4 fragColor;

const float HALF_SQRT3 = 0.86602540378443864676;

// Faint dark hex outline over the AA'd cell edges. The crisp color edges already
// define hexes with different colors; this keeps the lattice legible between
// same-colored tiles. Tunable; kGridDarken = 1.0 drops it.
const float kGridLinePx = 1.2;   // outline half-width in screen px
const float kGridDarken = 0.92;  // edge tint when fully on (1.0 = off)
const float kGridNearPx = 12.0;  // appears once tiles reach this size
const float kGridFullPx = 28.0;  // full (faint) strength by here

// Cube rounding in axial coords, mirroring SphereGrid::hexRound exactly
// (floor(x+0.5) ties, largest-delta fix). Returns integer (q, r) lattice coords.
ivec2 hexRound(vec2 axial) {
    float q = floor(axial.x + 0.5);
    float r = floor(axial.y + 0.5);
    float s = floor(-axial.x - axial.y + 0.5);
    float dq = abs(q - axial.x);
    float dr = abs(r - axial.y);
    float ds = abs(s + axial.x + axial.y);
    if (dq > dr && dq > ds) {
        q = -r - s;
    } else if (dr > ds) {
        r = -q - s;
    }
    return ivec2(int(q), int(r));
}

// Unskewed Cartesian for the 60-degree lattice basis (matches locateHex).
vec2 cart(vec2 a) { return vec2(a.x + 0.5 * a.y, a.y * HALF_SQRT3); }

// Exact (un-blended) color of a hex cell: its texel center sampled at mip 0.
// Cell (q,r): q is the 1-based i, r the 0-based j; tile (i,j) lives at texel
// (i-1, j) (texSize == n). Sampling the texel center returns it exactly even
// with the bilinear filter; mip 0 keeps it from pulling a coarser, pre-blended
// mip when slightly zoomed out.
vec3 cellColor(ivec2 cell) {
    vec2 uv = (vec2(cell) - vec2(1.0, 0.0) + 0.5) / u_n;
    uv = clamp(uv, 0.5 / u_n, 1.0 - 0.5 / u_n); // clamp seam/pole overshoot
    return textureLod(u_baseTex, uv, 0.0).rgb;
}

void main() {
    vec2  axial  = v_uv * u_n;
    ivec2 cell   = hexRound(axial);
    float tilePx = 1.0 / max(length(fwidth(axial)), 1e-8);

    // Voronoi distances to the 6 neighbors; keep the TWO nearest so the
    // three-cell hex vertices anti-alias cleanly instead of notching.
    vec2  p  = cart(axial);
    vec2  c0 = cart(vec2(cell));
    float d1 = length(p - c0);
    const ivec2 OFF[6] = ivec2[6](
        ivec2( 1, 0), ivec2(-1, 0), ivec2(0, 1),
        ivec2( 0,-1), ivec2( 1,-1), ivec2(-1, 1));
    float d2 = 1e30, d3 = 1e30;
    ivec2 nb2 = cell, nb3 = cell;
    for (int k = 0; k < 6; ++k) {
        ivec2 nc = cell + OFF[k];
        float d  = length(p - cart(vec2(nc)));
        if (d < d2)      { d3 = d2; nb3 = nb2; d2 = d; nb2 = nc; }
        else if (d < d3) { d3 = d;  nb3 = nc; }
    }

    // Crisp flat fills + signed-distance COVERAGE anti-aliasing. e2/e3 are 0 on
    // the edges to the nearest / 2nd-nearest cells; fwidth gives the true on-
    // screen edge gradient (vector-crisp). Clamp the width so the limb (grazing
    // fwidth blow-up) can't smear an edge past ~1px.
    vec3  cCol  = cellColor(cell);
    vec3  n2Col = cellColor(nb2);
    vec3  n3Col = cellColor(nb3);
    float e2 = 0.5 * (d2 - d1);
    float e3 = 0.5 * (d3 - d1);
    float w2 = clamp(0.5 * fwidth(e2), 1e-4, 0.7);
    float w3 = clamp(0.5 * fwidth(e3), 1e-4, 0.7);
    float cov2 = smoothstep(-w2, w2, e2);
    float cov3 = smoothstep(-w3, w3, e3);
    vec3  hexColor = mix(n3Col, mix(n2Col, cCol, cov2), cov3);

    // Faint dark outline on the cell edges.
    float ao   = max(fwidth(e2), 1e-5);
    float line = (1.0 - smoothstep(0.0, kGridLinePx * ao, e2))
                 * smoothstep(kGridNearPx, kGridFullPx, tilePx);
    hexColor = mix(hexColor, hexColor * kGridDarken, line);

    // Sub-pixel tiles sparkle under per-pixel hexRound, so cross-fade to the
    // mipmapped average (shimmer-free). The two agree in color at the handoff.
    vec3 mipped = texture(u_baseTex, v_uv).rgb;
    vec3 albedo = mix(mipped, hexColor, smoothstep(1.0, 2.0, tilePx));

    // ── lighting / atmosphere (geometric normal) ──
    float nDotL   = max(dot(v_normal, u_sunDir), 0.0);
    float ambient = 0.18;
    float diffuse = nDotL * 0.82;
    float light   = ambient + diffuse;

    vec3  viewDir  = normalize(u_cameraPos - v_worldPos);
    float rim      = 1.0 - max(dot(v_normal, viewDir), 0.0);
    rim = pow(rim, 4.0);

    float terminator = smoothstep(-0.12, 0.12, dot(v_normal, u_sunDir));

    vec3 litColor = albedo * light;

    vec3 atmColor  = vec3(0.25, 0.45, 0.90);
    float atmStrength = rim * (0.3 + 0.5 * terminator);
    litColor = mix(litColor, atmColor, atmStrength * 0.45);

    litColor *= mix(0.05, 1.0, terminator + 0.1);
    litColor = clamp(litColor, 0.0, 1.0);

    fragColor = vec4(litColor, 1.0);
}

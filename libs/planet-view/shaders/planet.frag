#version 330 core

in vec3 v_worldPos;
in vec3 v_normal;
in vec2 v_uv;

uniform sampler2D       u_baseTex;     // base tier: per-rhombus RGBA8 + mips
uniform usampler2DArray u_pageTable;   // R16UI: 0 = not resident, layer+1 resident
uniform sampler2DArray  u_atlas;       // detail pages: 130x130 RGBA8, NEAREST

uniform float u_n;            // grid subdivision
uniform int   u_rhombus;      // which rhombus this draw covers (page-table layer)
uniform int   u_pagesPerSide; // ceil(n/128)

uniform vec3  u_sunDir;       // world-space, normalised
uniform vec3  u_cameraPos;

out vec4 fragColor;

const int   PAGE_TILES  = 128;
const int   PAGE_BORDER = 1;
const int   PAGE_TEXELS = 130;     // 128 + 2*border
const float HALF_SQRT3  = 0.86602540378443864676;

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

void main() {
    // Tile centers sit at integer lattice coords (chart vertices), so the axial
    // coordinate is v_uv * n with no half-cell shift.
    vec2 axial = v_uv * u_n;
    ivec2 cell = hexRound(axial);   // (q, r) chart vertex this pixel belongs to

    // Pixels-per-tile from screen-space derivative of the axial coordinate.
    float tilePx = 1.0 / max(length(fwidth(axial)), 1e-8);

    vec4 baseColor;

    if (tilePx < 2.0) {
        // Zoomed out: tiles are sub-pixel, the mipmapped base tier is correct.
        baseColor = texture(u_baseTex, v_uv);
    } else {
        // --- detail-tier fetch ---
        // cell.x is i (1-based owned in [1..n]); cell.y is j (0-based [0..n-1]).
        // Page (pi,pj) owns i in [pi*128+1 .. +128], j in [pj*128 .. +127].
        int q = cell.x;
        int jc = cell.y;
        int pi = (q - 1) / PAGE_TILES;
        int pj = jc / PAGE_TILES;
        // Clamp page index into the rhombus so rounding overshoot at the chart
        // edge still lands in a real page; the padded border texel resolves the
        // true (canonicalized) tile baked on the CPU.
        pi = clamp(pi, 0, u_pagesPerSide - 1);
        pj = clamp(pj, 0, u_pagesPerSide - 1);

        uint entry = texelFetch(u_pageTable, ivec3(pi, pj, u_rhombus), 0).r;

        vec4 hexColor;
        bool resident = entry != 0u;
        if (resident) {
            int layer = int(entry) - 1;
            // Local texel within the page, +border. i = pi*128 + tx  =>
            // tx = i - pi*128; j = pj*128 + ty - 1 => ty = j - pj*128 + 1.
            int tx = q  - pi * PAGE_TILES;          // owned in [1..128]
            int ty = jc - pj * PAGE_TILES + PAGE_BORDER;
            tx = clamp(tx, 0, PAGE_TEXELS - 1);
            ty = clamp(ty, 0, PAGE_TEXELS - 1);
            hexColor = texelFetch(u_atlas, ivec3(tx, ty, layer), 0);
        } else {
            // Designed far tier: page not resident yet, sample the base mips.
            hexColor = texture(u_baseTex, v_uv);
        }

        // Voronoi edge distance in unskewed Cartesian over the 6 neighbor cells.
        vec2 p = cart(axial);
        vec2 c0 = cart(vec2(cell));
        float d1 = length(p - c0);
        const ivec2 OFF[6] = ivec2[6](
            ivec2( 1, 0), ivec2(-1, 0), ivec2(0, 1),
            ivec2( 0,-1), ivec2( 1,-1), ivec2(-1, 1));
        float d2 = 1e30;
        for (int k = 0; k < 6; ++k) {
            vec2 cc = cart(vec2(cell + OFF[k]));
            d2 = min(d2, length(p - cc));
        }
        float edgeDist = 0.5 * (d2 - d1); // 0 on the Voronoi edge, ~0.5 at center

        // Border line: a thin dark band hugging the Voronoi edge, AA'd by fwidth,
        // fading in only once tiles are large enough to read.
        float lineWidth = max(0.045, 1.25 / tilePx);
        float aa = max(fwidth(edgeDist), 1e-5);
        float line = (1.0 - smoothstep(lineWidth - aa, lineWidth + aa, edgeDist))
                     * smoothstep(14.0, 40.0, tilePx);

        // Near/far blend: detail page fades in over the base tier as tiles grow.
        baseColor = mix(texture(u_baseTex, v_uv), hexColor, smoothstep(2.0, 5.0, tilePx));
        baseColor = mix(baseColor, baseColor * 0.55, line);
    }

    // ── lighting / atmosphere (unchanged) ──
    float nDotL   = max(dot(v_normal, u_sunDir), 0.0);
    float ambient = 0.18;
    float diffuse = nDotL * 0.82;
    float light   = ambient + diffuse;

    vec3  viewDir  = normalize(u_cameraPos - v_worldPos);
    float rim      = 1.0 - max(dot(v_normal, viewDir), 0.0);
    rim = pow(rim, 4.0);

    float terminator = smoothstep(-0.12, 0.12, dot(v_normal, u_sunDir));

    vec3 litColor = baseColor.rgb * light;

    vec3 atmColor  = vec3(0.25, 0.45, 0.90);
    float atmStrength = rim * (0.3 + 0.5 * terminator);
    litColor = mix(litColor, atmColor, atmStrength * 0.45);

    litColor *= mix(0.05, 1.0, terminator + 0.1);
    litColor = clamp(litColor, 0.0, 1.0);

    fragColor = vec4(litColor, 1.0);
}

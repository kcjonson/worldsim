# SDF Text Rendering

## Overview

This document describes the Signed Distance Field (SDF) font rendering system for world-sim. SDF fonts provide crisp, scalable text rendering at any size with perfect anti-aliasing, matching the vector aesthetic of the game's SVG-based graphics.

## What are SDF Fonts?

**Signed Distance Field** rendering is the industry standard for high-quality text in games and real-time applications.

**Key Concept**: Instead of storing a bitmap of each glyph, we store the *distance* from each pixel to the nearest edge:
- **Positive** values = inside the glyph
- **Negative** values = outside the glyph
- **Zero** = exactly on the edge

The fragment shader reconstructs sharp edges at any scale by sampling this distance field.

## Why SDF Fonts?

**Used by:** Valve (Team Fortress 2, Dota 2), Unity, Unreal Engine, most modern game engines

**Advantages:**
- ✅ **Crisp at any scale**: One atlas works for 12px to 144px text
- ✅ **Perfect anti-aliasing**: Smooth edges via distance field gradient
- ✅ **Small memory footprint**: Single channel distance field (vs RGBA bitmap)
- ✅ **Effects for free**: Drop shadows, outlines, glow via shader
- ✅ **Matches vector aesthetic**: Consistent with SVG-based game graphics

**vs Traditional Bitmap Fonts:**
- ❌ Bitmap: Blurry when scaled up, aliased when scaled down
- ❌ Bitmap: Need multiple atlas sizes for different text sizes
- ❌ Bitmap: RGBA = 4 bytes per pixel vs 1 byte for SDF

## Architecture

### Components

1. **Font Atlas Generator** (offline tool)
   - Input: TrueType font file (.ttf)
   - Uses `msdfgen` library to generate distance fields
   - Output: PNG atlas + JSON metadata

2. **FontRenderer** (runtime)
   - Loads atlas texture and metadata
   - Generates glyph quads with UV coordinates
   - No immediate rendering - queues commands for batching

3. **SDF Text Shader** (GPU)
   - Samples distance field texture
   - Reconstructs sharp edge via `smoothstep()`
   - Outputs anti-aliased fragment color

4. **Primitives API Integration**
   - `DrawText()` queues text commands
   - Two-pass rendering sorts by z-index
   - All text batches into single draw call per atlas

### Data Flow

```
Offline:
  .ttf file → msdfgen → atlas.png + metadata.json

Runtime:
  DrawText() → GenerateGlyphQuads() → DrawCommand queue →
  EndFrame() → Sort by z-index/batch key → RenderBatch() →
  Bind SDF shader + atlas → glDrawArrays() (batched)
```

## SDF Atlas Generation

### Using msdfgen

**msdfgen** (Multi-channel Signed Distance Field Generator) is the industry standard:
- Developed by Viktor Chlumský
- Used by Adobe, Figma, game engines
- Generates better quality than single-channel SDF

**Installation:**
```bash
# Add to vcpkg.json
{
  "name": "msdfgen",
  "version>=": "1.10.0"
}
```

**Generation Script** (tools/generate_sdf_atlas.cpp):
```cpp
#include <msdfgen.h>
#include <msdfgen-ext.h>

struct AtlasConfig {
    const char* fontPath = "fonts/Roboto-Regular.ttf";
    const char* outputPath = "fonts/Roboto-SDF.png";
    const char* metadataPath = "fonts/Roboto-SDF.json";

    int atlasWidth = 512;
    int atlasHeight = 512;
    float pixelRange = 4.0F;  // Distance field range in pixels
    int fontSize = 32;         // Base size for distance field generation

    // Character set to include
    const char* charset =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
        "0123456789 !@#$%^&*()_+-=[]{}|;':\",./<>?`~";
};

void GenerateSDFAtlas(const AtlasConfig& config) {
    // 1. Load font with FreeType
    msdfgen::FreetypeHandle* ft = msdfgen::initializeFreetype();
    msdfgen::FontHandle* font = msdfgen::loadFont(ft, config.fontPath);

    // 2. Pack glyphs into atlas
    std::vector<msdfgen::GlyphBox> glyphs;
    msdfgen::TightAtlasPacker packer;
    packer.setDimensions(config.atlasWidth, config.atlasHeight);
    packer.setPixelRange(config.pixelRange);
    packer.setMiterLimit(1.0);

    // Add each character to packer
    for (char c : config.charset) {
        msdfgen::GlyphIndex glyph;
        if (msdfgen::getGlyphIndex(glyph, font, msdfgen::unicode_t(c))) {
            glyphs.push_back(packer.add(font, glyph));
        }
    }

    packer.pack();

    // 3. Generate distance fields
    msdfgen::Bitmap<float, 3> atlas(config.atlasWidth, config.atlasHeight);
    msdfgen::generate Atlasmsdfgen(atlas, glyphs.data(), glyphs.size());

    // 4. Export PNG
    msdfgen::savePng(atlas, config.outputPath);

    // 5. Export JSON metadata
    ExportMetadata(glyphs, config);

    msdfgen::destroyFont(font);
    msdfgen::deinitializeFreetype(ft);
}
```

**Metadata Format** (Roboto-SDF.json):
```json
{
  "atlas": {
    "width": 512,
    "height": 512,
    "pixelRange": 4.0
  },
  "glyphs": {
    "A": {
      "atlas": {"x": 10, "y": 10, "width": 28, "height": 32},
      "planeBounds": {"left": 0.5, "bottom": 0.0, "right": 27.5, "top": 32.0},
      "advance": 28.5
    },
    "B": { ... },
    ...
  }
}
```

## Runtime FontRenderer Implementation

### Atlas Loading

```cpp
class FontRenderer {
public:
    bool LoadSDFFont(const std::string& atlasPath, const std::string& metadataPath);

private:
    struct SDFGlyph {
        // Atlas texture coordinates (normalized 0-1)
        glm::vec2 uvMin;
        glm::vec2 uvMax;

        // Glyph metrics (in pixels at base size)
        glm::vec2 planeBoundsMin;
        glm::vec2 planeBoundsMax;
        float advance;
    };

    GLuint m_sdfAtlas = 0;        // Single texture for all glyphs
    float m_pixelRange = 4.0F;     // Distance field range
    std::map<char, SDFGlyph> m_glyphs;
};

bool FontRenderer::LoadSDFFont(const std::string& atlasPath,
                                const std::string& metadataPath) {
    // 1. Load PNG atlas
    m_sdfAtlas = LoadTextureFromPNG(atlasPath);

    // 2. Parse JSON metadata
    json metadata = ParseJSON(metadataPath);
    m_pixelRange = metadata["atlas"]["pixelRange"];

    for (auto& [character, data] : metadata["glyphs"].items()) {
        SDFGlyph glyph;

        // Atlas UV coordinates (normalized)
        int atlasWidth = metadata["atlas"]["width"];
        int atlasHeight = metadata["atlas"]["height"];
        glyph.uvMin = glm::vec2(
            data["atlas"]["x"] / atlasWidth,
            data["atlas"]["y"] / atlasHeight
        );
        glyph.uvMax = glm::vec2(
            (data["atlas"]["x"] + data["atlas"]["width"]) / atlasWidth,
            (data["atlas"]["y"] + data["atlas"]["height"]) / atlasHeight
        );

        // Glyph metrics
        glyph.planeBoundsMin = glm::vec2(
            data["planeBounds"]["left"],
            data["planeBounds"]["bottom"]
        );
        glyph.planeBoundsMax = glm::vec2(
            data["planeBounds"]["right"],
            data["planeBounds"]["top"]
        );
        glyph.advance = data["advance"];

        m_glyphs[character[0]] = glyph;
    }

    return true;
}
```

### Glyph Quad Generation

```cpp
void FontRenderer::GenerateGlyphQuads(
    const std::string& text,
    const glm::vec2& position,
    float scale,
    const glm::vec4& color,
    std::vector<GlyphQuad>& outQuads
) const {
    glm::vec2 pen = position;

    for (char c : text) {
        auto it = m_glyphs.find(c);
        if (it == m_glyphs.end()) continue;

        const SDFGlyph& glyph = it->second;

        // Calculate quad position (scaled)
        glm::vec2 quadPos = pen + glyph.planeBoundsMin * scale;
        glm::vec2 quadSize = (glyph.planeBoundsMax - glyph.planeBoundsMin) * scale;

        outQuads.push_back(GlyphQuad{
            .position = quadPos,
            .size = quadSize,
            .uvMin = glyph.uvMin,
            .uvMax = glyph.uvMax,
            .color = color
        });

        // Advance pen
        pen.x += glyph.advance * scale;
    }
}
```

## SDF Shader Implementation

### Vertex Shader (sdf_text.vert)

```glsl
#version 330 core

layout(location = 0) in vec2 aPosition;  // Quad vertex position
layout(location = 1) in vec2 aTexCoord;  // UV coordinates
layout(location = 2) in vec4 aColor;     // Text color

out vec2 vTexCoord;
out vec4 vColor;

uniform mat4 uProjection;

void main() {
    gl_Position = uProjection * vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTexCoord;
    vColor = aColor;
}
```

### Fragment Shader (sdf_text.frag)

```glsl
#version 330 core

in vec2 vTexCoord;
in vec4 vColor;

out vec4 FragColor;

uniform sampler2D uSDFAtlas;
uniform float uPixelRange;  // Distance field range (e.g., 4.0)

float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

void main() {
    // Sample multi-channel distance field
    vec3 msd = texture(uSDFAtlas, vTexCoord).rgb;
    float sd = median(msd.r, msd.g, msd.b);

    // Screen-space derivatives for anti-aliasing
    float screenPxDistance = uPixelRange * (sd - 0.5);
    float screenPxRange = length(vec2(dFdx(screenPxDistance), dFdy(screenPxDistance)));

    // Reconstruct sharp edge with smooth anti-aliasing
    float opacity = smoothstep(-screenPxRange, screenPxRange, screenPxDistance);

    FragColor = vec4(vColor.rgb, vColor.a * opacity);
}
```

**Key Insight**: `dFdx/dFdy` give us screen-space gradient → automatic AA scaling!

## Integration with Batched Rendering

### DrawText() Implementation

```cpp
void DrawText(const TextArgs& args) {
    if (!g_fontRenderer) return;

    DrawCommand cmd;
    cmd.batchKey = GetTextBatchKey(g_fontRenderer->GetSDFAtlas());
    cmd.zIndex = args.zIndex;
    cmd.isTransparent = args.color.a < 1.0F;
    cmd.primitiveType = GL_TRIANGLES;

    // Generate glyph quads (no rendering yet)
    std::vector<FontRenderer::GlyphQuad> quads;
    g_fontRenderer->GenerateGlyphQuads(
        args.text, args.position, args.scale, args.color, quads
    );

    // Convert quads to vertex data (6 vertices per quad)
    for (const auto& quad : quads) {
        // Vertex format: position (vec2) + texCoord (vec2) + color (vec4)
        float vertices[] = {
            // Triangle 1
            quad.position.x, quad.position.y,
            quad.uvMin.x, quad.uvMin.y,
            quad.color.r, quad.color.g, quad.color.b, quad.color.a,

            quad.position.x, quad.position.y + quad.size.y,
            quad.uvMin.x, quad.uvMax.y,
            quad.color.r, quad.color.g, quad.color.b, quad.color.a,

            quad.position.x + quad.size.x, quad.position.y + quad.size.y,
            quad.uvMax.x, quad.uvMax.y,
            quad.color.r, quad.color.g, quad.color.b, quad.color.a,

            // Triangle 2
            quad.position.x, quad.position.y,
            quad.uvMin.x, quad.uvMin.y,
            quad.color.r, quad.color.g, quad.color.b, quad.color.a,

            quad.position.x + quad.size.x, quad.position.y + quad.size.y,
            quad.uvMax.x, quad.uvMax.y,
            quad.color.r, quad.color.g, quad.color.b, quad.color.a,

            quad.position.x + quad.size.x, quad.position.y,
            quad.uvMax.x, quad.uvMin.y,
            quad.color.r, quad.color.g, quad.color.b, quad.color.a,
        };

        cmd.vertices.insert(cmd.vertices.end(), vertices, vertices + 48);
    }

    g_commandQueue.push_back(std::move(cmd));
}
```

### Batching Benefits

All text with the same SDF atlas batches into **one draw call**:
- Button labels
- Menu text
- HUD elements
- Debug output

**Performance**: 1000 characters = 1 draw call (vs 1000 draw calls with immediate mode)

## Effects and Extensions

### Drop Shadow

```glsl
// In fragment shader, sample twice with offset
vec3 msd = texture(uSDFAtlas, vTexCoord).rgb;
vec3 shadowMSD = texture(uSDFAtlas, vTexCoord + vec2(0.002, -0.002)).rgb;

float shadow = median(shadowMSD.r, shadowMSD.g, shadowMSD.b);
float shadowOpacity = smoothstep(-screenPxRange, screenPxRange,
                                 uPixelRange * (shadow - 0.5));

// Composite shadow under text
vec4 shadowColor = vec4(0.0, 0.0, 0.0, 0.5 * shadowOpacity);
FragColor = mix(shadowColor, vec4(vColor.rgb, opacity), opacity);
```

### Outline

```glsl
// Adjust threshold for outline thickness
float outlineDistance = uPixelRange * (sd - 0.4);  // Thicker threshold
float outlineOpacity = smoothstep(-screenPxRange, screenPxRange, outlineDistance);

// Composite outline + fill
vec4 outlineColor = vec4(0.0, 0.0, 0.0, 1.0);
FragColor = mix(outlineColor, vColor, outlineOpacity);
```

### Glow

```glsl
// Expand distance field for glow
float glowDistance = uPixelRange * (sd - 0.3);
float glowOpacity = smoothstep(-screenPxRange * 2.0, 0.0, glowDistance);

vec4 glowColor = vec4(vColor.rgb, 0.3 * glowOpacity);
FragColor = mix(glowColor, vColor, opacity);
```

## Implementation Plan

### Phase 1: Atlas Generation Tool
- [ ] Add msdfgen to vcpkg.json
- [ ] Create `tools/generate_sdf_atlas` executable
- [ ] Generate Roboto-Regular SDF atlas
- [ ] Create JSON metadata parser

### Phase 2: FontRenderer SDF Support
- [ ] Add `LoadSDFFont()` method
- [ ] Implement `GenerateGlyphQuads()` for SDF
- [ ] Add `GetSDFAtlas()` for batch key
- [ ] Remove old immediate rendering code

### Phase 3: SDF Shaders
- [ ] Create sdf_text.vert shader
- [ ] Create sdf_text.frag shader with median filtering
- [ ] Add uPixelRange uniform
- [ ] Test anti-aliasing quality

### Phase 4: Integration
- [ ] Update DrawText() to use SDF quads
- [ ] Update Text::Render() to use DrawText()
- [ ] Test with button scene
- [ ] Verify z-ordering works

### Phase 5: Testing & Polish
- [ ] Test at multiple scales (8px to 144px)
- [ ] Verify anti-aliasing quality
- [ ] Test transparency
- [ ] Performance profiling (batching effectiveness)

## Performance Targets

- **Draw calls**: 1 per SDF atlas (all text batched together)
- **Memory**: <1MB for 256-character atlas at 512×512
- **Quality**: Crisp text from 8px to 144px without artifacts
- **Frame time**: <0.1ms for 1000 characters (batched)

## References

- **Valve's original paper**: "Improved Alpha-Tested Magnification for Vector Textures and Special Effects" (2007)
- **msdfgen**: https://github.com/Chlumsky/msdfgen
- **Multi-channel SDF**: https://github.com/Chlumsky/msdf-atlas-gen

## Related Documents

- [Batched Text Rendering](./batched-text-rendering.md) - Command queue architecture
- [SDF Rendering](./sdf-rendering.md) - SDF approach for UI primitives (rectangles with rounded corners)
- [Primitive Rendering API](./primitive-rendering-api.md) - Unified drawing API

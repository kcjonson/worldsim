# Fixed SDF Text Rendering Horizontal Squishing

**Date:** 2025-11-20

**Summary:**
Fixed critical bug in SDF text rendering where glyphs were horizontally compressed, making narrow letters like 'I' and 'i' barely visible. The issue was that we were sampling the full 32×32 pixel atlas cell for each glyph instead of only sampling the actual glyph content within that cell. Implemented atlasBounds support following the official msdf-atlas-gen approach.

**Root Cause:**
The atlas generator allocated 32×32 pixel cells for each glyph but glyphs varied in actual size (e.g., 'I' is ~3 pixels wide, 'W' is ~20 pixels wide). The renderer was using the full cell UV coordinates, causing the shader to stretch narrow glyphs across the entire cell width, distorting their proportions.

**Files Modified:**
- `tools/generate_sdf_atlas/main.cpp` - Added atlasBounds calculation and export
- `libs/ui/font/font_renderer.h` - Added atlasBounds fields to SDFGlyph struct
- `libs/ui/font/font_renderer.cpp` - Read atlasBounds from JSON and use for UV coordinates
- `build/apps/ui-sandbox/fonts/Roboto-SDF.json` - Regenerated with atlasBounds data

**Technical Details:**

**1. Atlas Coordinate Types**
Following msdf-atlas-gen conventions, there are three coordinate systems:
- **atlas**: Full allocated cell (e.g., 32×32 pixels at x=0, y=0)
- **atlasBounds**: Actual glyph pixels within cell (e.g., 3×22 pixels at x=14, y=5)
- **plane**: Glyph positioning in EM units relative to baseline

**2. Generator Changes (`tools/generate_sdf_atlas/main.cpp`)**
Added atlasBounds calculation during generation:
```cpp
// Calculate actual glyph bounds within the atlas cell (in pixels)
// Apply transformation to plane bounds to get pixel coordinates
glyph.atlasBoundsLeft = (glyph.planeLeft + translateX) * uniformScale;
glyph.atlasBoundsBottom = (glyph.planeBottom + translateY) * uniformScale;
glyph.atlasBoundsRight = (glyph.planeRight + translateX) * uniformScale;
glyph.atlasBoundsTop = (glyph.planeTop + translateY) * uniformScale;
```

JSON export includes both cell and content bounds:
```json
"I": {
  "atlas": {"x": 0.5, "y": 0, "width": 0.0625, "height": 0.0625},
  "atlasBounds": {"left": 0.528, "bottom": 0.009, "right": 0.534, "top": 0.053},
  "plane": {"left": 0.089, "bottom": 0, "right": 0.184, "top": 0.711}
}
```

**3. Loader Changes (`libs/ui/font/font_renderer.cpp`)**
Added backward-compatible atlasBounds loading:
```cpp
if (glyphJson.contains("atlasBounds") && !glyphJson["atlasBounds"].is_null()) {
    glyph.atlasBoundsMin.x = glyphJson["atlasBounds"]["left"].get<float>();
    // ... read other bounds
} else {
    // Fallback: use full atlas cell if atlasBounds not available
    glyph.atlasBoundsMin = glyph.atlasUVMin;
    glyph.atlasBoundsMax = glyph.atlasUVMax;
}
```

**4. Renderer Changes**
Changed UV coordinates to use atlasBounds:
```cpp
quad.uvMin = glyph.atlasBoundsMin;  // Was: glyph.atlasUVMin
quad.uvMax = glyph.atlasBoundsMax;  // Was: glyph.atlasUVMax
```

**Key Learnings:**
1. **Uniform scaling is essential** - All glyphs must use the same pixels-per-EM scale for correct coordinate mapping
2. **Cell vs content distinction** - Atlas generators allocate fixed cells but must track actual glyph content bounds
3. **Research first** - Reading official msdf-atlas-gen documentation and source revealed the correct approach immediately
4. **Coordinate system complexity** - Three separate coordinate systems (atlas cells, atlas bounds, plane bounds) each serve a specific purpose

**References:**
- https://github.com/Chlumsky/msdf-atlas-gen/issues/2 (atlasBounds vs atlas)
- https://github.com/Chlumsky/msdf-atlas-gen/discussions/17 (plane bounds)
- https://github.com/Chlumsky/msdf-atlas-gen/discussions/47 (uniform scaling)



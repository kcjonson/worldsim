# SVG Parsing Options - Comparative Analysis

Created: 2025-10-24
Last Updated: 2025-10-24
Status: Research

## Overview

SVG (Scalable Vector Graphics) is an XML-based vector image format. For this game, all visual assets are stored as SVG files, which must be parsed at runtime (or import time) to extract vector path data for rendering.

**Two Approaches:**
1. **Full SVG Parser**: Parse complete SVG specification (complex, flexible)
2. **Subset Parser**: Parse only features used by game assets (simple, limited)

This document analyzes libraries and approaches for SVG parsing.

## Requirements

### Functional Requirements

**Minimal SVG Feature Set** (what our game assets actually need):
- **Path Elements**: `<path>` with commands (M, L, C, Q, Z, etc.)
- **Basic Shapes**: `<rect>`, `<circle>`, `<ellipse>`, `<polygon>`, `<polyline>`
- **Groups**: `<g>` for hierarchical organization
- **Transforms**: `translate`, `rotate`, `scale`, `matrix`
- **Fill/Stroke**: Solid colors, opacity
- **Custom Metadata**: Store animation data, collision shapes in metadata

**NOT Required** (unless we find we need them):
- Text rendering (`<text>`)
- Complex gradients (linear/radial)
- Filters (blur, drop shadow, etc.)
- Masks and clipping paths
- Embedded images (`<image>`)
- CSS styling
- JavaScript/SMIL animation

### Performance Requirements
- **Parse Time**: <10ms per asset on average
- **Memory**: Minimal allocations, prefer arena allocators
- **Lazy Loading**: Parse-on-demand, not all assets at startup
- **Thread-Safe**: Support concurrent parsing (asset streaming)

### Integration Requirements
- **C/C++20 Compatible**
- **License**: Permissive (MIT, BSD, zlib)
- **Cross-Platform**: macOS, Windows, Linux
- **No Heavy Dependencies**: Avoid large XML libraries if possible
- **Output Format**: Flexible, can convert to internal format

## Option A: NanoSVG

### Overview
NanoSVG is a simple, lightweight SVG parser written in C. It's designed for simplicity and ease of integration, supporting a minimal subset of SVG features. **Already included in the codebase (vcpkg.json)**.

**Repository**: https://github.com/memononen/nanosvg
**License**: zlib License (very permissive)
**Language**: C (header-only)
**Dependencies**: None

### Pros
- **Already in Project**: Listed in vcpkg.json, zero integration work
- **Header-Only**: Two files: nanosvg.h (parsing), nanosvgrast.h (rasterizing)
- **Lightweight**: ~3000 lines total, very minimal
- **Simple API**: Easy to use, well-documented
- **No Dependencies**: Pure C, no XML library needed
- **Rasterizer Included**: Can rasterize to bitmaps (useful for Tier 1)
- **Battle-Tested**: Used in many projects, proven in production
- **Fast**: Optimized for performance

### Cons
- **Limited SVG Support**: Only basic SVG features
- **No Gradient Parsing** (for complex fills)
- **No Text**: Cannot parse `<text>` elements
- **No Filter Effects**: blur, shadows, etc. not supported
- **C API**: Requires C++ wrapper for RAII
- **Fixed Parsing**: Less flexibility in output format
- **No Streaming**: Parses entire SVG at once

### Technical Details

**Supported SVG Features**:
- Path commands: M, L, H, V, C, S, Q, T, A, Z (all major commands)
- Basic shapes: rect, circle, ellipse, line, polyline, polygon
- Groups: `<g>` with transforms
- Transforms: translate, rotate, scale, skewX, skewY, matrix
- Fill/Stroke: Solid colors, gradients (basic)
- Styling: Inline styles, some CSS

**Performance Characteristics**:
- Parse time: ~1-5ms for typical SVG (100-500 elements)
- Memory: Allocates tree structure in memory
- Output: `NSVGimage` struct with linked list of shapes

**Memory Footprint**:
- Small: ~10-50 KB per parsed SVG
- Uses malloc internally (can override)

**Integration Complexity**:
- Very Low: Header-only, just #include and use
- C API requires basic wrapper

**Platform Compatibility**:
- Excellent: Pure C, no platform-specific code

**API Example**:
```cpp
#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>

// Parse SVG from file
NSVGimage* image = nsvgParseFromFile("asset.svg", "px", 96.0f);

// Iterate shapes
for (NSVGshape* shape = image->shapes; shape != nullptr; shape = shape->next) {
    // Access path data
    for (NSVGpath* path = shape->paths; path != nullptr; path = path->next) {
        // path->pts contains flattened cubic bezier curves (x,y pairs)
        // path->npts is number of points

        for (int i = 0; i < path->npts; i += 3) {
            // Cubic Bezier: pts[i], pts[i+1], pts[i+2], pts[i+3]
            // (NanoSVG flattens to cubic beziers)
        }
    }
}

// Cleanup
nsvgDelete(image);
```

### Alignment with Game Requirements

| Requirement | Alignment | Notes |
|-------------|-----------|-------|
| Path Elements | ✅ Excellent | All major path commands |
| Basic Shapes | ✅ Excellent | All standard shapes |
| Transforms | ✅ Excellent | All transforms supported |
| Fill/Stroke | ✅ Good | Solid colors, basic gradients |
| Custom Metadata | ⚠️ Limited | No built-in metadata parsing |
| Performance | ✅ Excellent | Fast, optimized |
| Integration | ✅ Excellent | Already in project |

### Recommendation
✅ **Strong Candidate** - Already integrated, handles our core needs, proven.

---

## Option B: LunaSVG

### Overview
LunaSVG is a modern, full-featured SVG parsing and rendering library written in C++11. It aims for high SVG specification compliance and includes a rasterizer.

**Repository**: https://github.com/sammycage/lunasvg
**License**: MIT
**Language**: C++11
**Dependencies**: PLutoVG (for rendering), Cairo (optional)

### Pros
- **Modern C++**: Clean C++11 API, STL-based
- **Full SVG Support**: Much higher spec compliance than NanoSVG
- **Gradient Support**: Linear/radial gradients
- **CSS Support**: Styling, classes
- **Actively Maintained**: Regular updates, responsive maintainer
- **Good Documentation**: Examples and API docs
- **Rasterizer**: Built-in, high-quality rasterization

### Cons
- **Not Header-Only**: Requires compilation and linking
- **Larger Codebase**: ~15k+ lines (more complexity)
- **Dependencies**: PLutoVG for rendering (can be disabled)
- **Heavier**: More features = more overhead for simple assets
- **Slower Parsing**: More thorough parsing takes longer
- **Not in Project**: Need to add to vcpkg.json and integrate

### Technical Details

**Supported SVG Features**:
- All path commands
- All basic shapes
- Gradients (linear, radial, patterns)
- Text (if needed)
- Filters (blur, etc.)
- Masks and clipping
- CSS styling
- Much of SVG 1.1 specification

**Performance Characteristics**:
- Parse time: ~5-15ms for typical SVG (more thorough parsing)
- Memory: Larger DOM tree structure
- Output: Document tree, can query elements

**Memory Footprint**:
- Moderate: ~50-200 KB per parsed SVG
- C++ objects, STL containers

**Integration Complexity**:
- Medium: CMake build, link library
- Modern C++ API, straightforward to use

**Platform Compatibility**:
- Excellent: C++11, cross-platform

**API Example**:
```cpp
#include <lunasvg.h>

// Parse SVG
auto document = lunasvg::Document::loadFromFile("asset.svg");

// Get bounds
auto box = document->boundingBox();

// Render to bitmap (if needed)
auto bitmap = document->renderToBitmap();

// Or access elements for vector data
auto root = document->rootElement();
// Traverse DOM tree...
```

### Alignment with Game Requirements

| Requirement | Alignment | Notes |
|-------------|-----------|-------|
| Path Elements | ✅ Excellent | Full support |
| Basic Shapes | ✅ Excellent | Full support |
| Transforms | ✅ Excellent | Full support |
| Fill/Stroke | ✅ Excellent | Gradients, patterns |
| Custom Metadata | ✅ Good | Can parse custom elements |
| Performance | ⚠️ Good | Slower than NanoSVG |
| Integration | ⚠️ Medium | Not yet in project |

### Recommendation
⚠️ **Consider if Complex Features Needed** - Overkill for basic assets, but useful if we need gradients, text, or advanced features.

---

## Option C: PlutoVG

### Overview
PlutoVG is a standalone 2D vector graphics library inspired by Cairo and Skia. While primarily a rendering library, it includes SVG parsing capabilities.

**Repository**: https://github.com/sammycage/plutovg
**License**: MIT
**Language**: C
**Dependencies**: None (standalone)

### Pros
- **Lightweight**: Smaller than LunaSVG
- **C API**: Simple, no C++ dependencies
- **Rendering Included**: Can render directly
- **Modern**: Recently created, clean codebase
- **No Dependencies**: Self-contained

### Cons
- **Rendering-Focused**: SVG parsing is secondary feature
- **Limited SVG Support**: Less complete than LunaSVG
- **Less Mature**: Newer project, less battle-tested
- **Not Header-Only**: Requires compilation
- **Documentation**: Less comprehensive

### Technical Details

**Supported SVG Features**:
- Basic path elements
- Basic shapes
- Transforms
- Fill/stroke with colors
- Some gradients

**Performance Characteristics**:
- Parse time: ~2-8ms (estimate, not extensively benchmarked)
- Memory: Moderate

**Integration Complexity**:
- Medium: C library, needs linking

**Platform Compatibility**:
- Good: C library, cross-platform

### Alignment with Game Requirements

| Requirement | Alignment | Notes |
|-------------|-----------|-------|
| Path Elements | ✅ Good | Core paths supported |
| Basic Shapes | ✅ Good | Standard shapes |
| Transforms | ✅ Good | Basic transforms |
| Fill/Stroke | ✅ Good | Colors, some gradients |
| Custom Metadata | ⚠️ Unknown | Less documented |
| Performance | ✅ Good | Lightweight |
| Integration | ⚠️ Medium | Not yet in project |

### Recommendation
⚠️ **Possible Alternative** - Middle ground between NanoSVG and LunaSVG, but less proven.

---

## Option D: No Library - Custom Subset Parser

### Overview
Implement a minimal SVG parser from scratch that handles only the exact features used by game assets. This would be a lightweight XML parser focused on extracting path data.

**Reference Implementations**:
- NanoSVG source code (for inspiration)
- SVG Path specification: https://www.w3.org/TR/SVG/paths.html
- Tiny XML parsers: TinyXML-2, pugixml

### Pros
- **Minimal Footprint**: Only code we actually need
- **Optimized for Assets**: Tailored to game asset format
- **Full Control**: Can optimize for performance and memory
- **Learning Opportunity**: Understand SVG parsing deeply
- **No External Dependencies**: Pure custom code
- **Custom Metadata**: Easy to add game-specific extensions

### Cons
- **Development Time**: 3-7 days of implementation + testing
- **Maintenance Burden**: Must maintain ourselves
- **SVG Spec Complexity**: Easy to miss edge cases
- **Robustness**: Hard to match battle-tested libraries
- **Reinventing the Wheel**: Solving a solved problem

### Technical Details

**Implementation Approach**:
```
1. XML Parsing:
   - Option A: Use lightweight XML parser (pugixml, TinyXML-2)
   - Option B: Write minimal SAX-style parser (if SVGs are simple)

2. SVG Element Handling:
   - Parse <path> elements
   - Convert path data string → commands (M, L, C, Q, etc.)
   - Parse basic shapes → equivalent path commands
   - Parse transforms (matrix math)

3. Output Format:
   - Internal representation optimized for game
   - Pre-flattened curves if desired
   - Direct integration with tessellation

4. Custom Extensions:
   - Parse custom <metadata> tags
   - Store animation data
   - Store collision shape data
```

**Complexity Estimate**:

| Component | Lines of Code | Effort |
|-----------|---------------|--------|
| XML Parser (if custom) | 300-500 | 1-2 days |
| Path Data Parser | 200-400 | 1-2 days |
| Shape → Path Conversion | 150-250 | 1 day |
| Transform Parsing | 100-200 | 0.5-1 day |
| Output Data Structure | 100-150 | 0.5 day |
| Testing & Edge Cases | - | 1-2 days |
| **Total** | **850-1500 lines** | **5-8 days** |

**Performance Characteristics**:
- Parse time: Likely 1-5ms (can optimize heavily)
- Memory: Can use arena allocators directly
- Custom output format

**Using Existing XML Library** (Recommended):

```cpp
#include <pugixml.hpp>

class CustomSVGParser {
    struct PathData {
        std::vector<PathCommand> commands;
        Transform transform;
        FillStyle fill;
        StrokeStyle stroke;
    };

    std::vector<PathData> ParseSVG(const char* filename) {
        pugi::xml_document doc;
        doc.load_file(filename);

        std::vector<PathData> paths;

        // Find all <path> elements
        for (auto path : doc.select_nodes("//path")) {
            PathData data;
            data.commands = ParsePathData(path.node().attribute("d").value());
            data.transform = ParseTransform(path.node().attribute("transform").value());
            data.fill = ParseFill(path.node().attribute("fill").value());
            // ... parse other attributes
            paths.push_back(data);
        }

        // Handle basic shapes
        for (auto rect : doc.select_nodes("//rect")) {
            paths.push_back(RectToPath(rect.node()));
        }
        // ... other shapes

        return paths;
    }

private:
    std::vector<PathCommand> ParsePathData(const char* d) {
        // Parse SVG path data string
        // "M 0 0 L 100 0 L 100 100 Z"
        // → {MoveTo{0,0}, LineTo{100,0}, LineTo{100,100}, ClosePath}
    }

    Transform ParseTransform(const char* transform) {
        // Parse "translate(10, 20) rotate(45)"
        // → Transform matrix
    }

    // ... other parsing functions
};
```

**Minimal XML Parser** (if avoiding dependencies):

Could write a minimal SAX-style parser if SVGs are very simple:

```cpp
// If SVG files are very controlled and simple
class MinimalSVGParser {
    // State machine parsing
    // Look for <path>, <rect>, etc.
    // Extract attribute strings
    // Ignore everything else

    // ~500 lines of careful string parsing
};
```

### Alignment with Game Requirements

| Requirement | Alignment | Notes |
|-------------|-----------|-------|
| Path Elements | ✅ Excellent* | Custom implementation |
| Basic Shapes | ✅ Excellent* | Convert to paths |
| Transforms | ✅ Excellent* | Matrix math |
| Fill/Stroke | ✅ Excellent* | Parse what we need |
| Custom Metadata | ✅ Excellent | Easy to add |
| Performance | ✅ Excellent* | Can optimize heavily |
| Integration | ⚠️ High Effort | Must implement |

\* = Depends on implementation quality

### Recommendation
⚠️ **Consider Only if Specific Needs** - NanoSVG already handles our needs. Only pursue if:
- Need perfect memory control
- Want custom game-specific extensions heavily integrated
- Have time for implementation

---

## Comparison Matrix

| Criteria | NanoSVG | LunaSVG | PlutoVG | Custom Parser |
|----------|---------|---------|---------|---------------|
| **Parse Performance** | ✅ Excellent (1-5ms) | ⚠️ Good (5-15ms) | ✅ Good (2-8ms*) | ✅ Excellent (1-5ms*) |
| **SVG Feature Coverage** | ⚠️ Basic subset | ✅ Extensive (SVG 1.1+) | ⚠️ Moderate | ✅ Exactly what we need* |
| **Paths** | ✅ All commands | ✅ All commands | ✅ All commands | ✅ All commands* |
| **Basic Shapes** | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes* |
| **Transforms** | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes* |
| **Gradients** | ⚠️ Basic | ✅ Full | ⚠️ Some | ⚠️ Optional* |
| **Text Support** | ❌ No | ✅ Yes | ❌ No | ⚠️ Optional* |
| **Filters/Effects** | ❌ No | ✅ Yes | ❌ No | ❌ No* |
| **Custom Metadata** | ⚠️ Manual | ✅ Can parse | ⚠️ Unknown | ✅ Easy* |
| **Memory Footprint** | ✅ Small (~10-50 KB) | ⚠️ Moderate (~50-200 KB) | ✅ Small-Med | ✅ Minimal* |
| **Integration** | ✅ Header-only | ⚠️ Compile+Link | ⚠️ Compile+Link | ⚠️ Implement |
| **Already in Project** | ✅ Yes (vcpkg.json) | ❌ No | ❌ No | ❌ N/A |
| **Dependencies** | ✅ None | ⚠️ PLutoVG | ✅ None | ⚠️ Possibly XML lib |
| **License** | ✅ zlib (very permissive) | ✅ MIT | ✅ MIT | ✅ Own code |
| **Maturity** | ✅✅✅ Very mature | ✅✅ Mature | ⚠️ Newer | ❌ New* |
| **Rasterizer** | ✅ Included | ✅ Included | ✅ Included | ❌ No* |
| **Documentation** | ✅ Good | ✅ Good | ⚠️ Limited | ❌ Own docs* |
| **Code Size** | ✅ Small (~3k lines) | ⚠️ Large (~15k+ lines) | ✅ Medium (~8k lines) | ✅ Small (~1k lines*) |

\* = Estimated/depends on implementation

## Open Questions

1. **Do we need gradient fills?**
   - If yes: LunaSVG or custom implementation
   - If no: NanoSVG is sufficient

2. **Will we need text rendering in SVG assets?**
   - If yes: LunaSVG (or rasterize text separately)
   - If no: NanoSVG is fine

3. **How complex will our SVG metadata be?**
   - Simple: NanoSVG + manual parsing
   - Complex: Custom parser or LunaSVG

4. **How important is parse performance?**
   - Critical: NanoSVG or custom
   - Not critical: LunaSVG acceptable

5. **Do we want to parse at import time or runtime?**
   - Import time: Can use slower, more complete parser
   - Runtime: Need fast parser

6. **Will SVG parsing be a bottleneck?**
   - If we cache parsed results: Doesn't matter much
   - If we parse frequently: NanoSVG or custom

## Decision Criteria

**Choose NanoSVG if:**
- Current basic SVG features are sufficient
- Want fastest integration (already in project)
- Performance is important
- Don't need gradients, text, or advanced features
- Want header-only simplicity

**Choose LunaSVG if:**
- Need full SVG specification support
- Want gradients, text, filters
- Willing to add another dependency
- Parse time <15ms is acceptable
- Want most complete solution

**Choose PlutoVG if:**
- Want middle ground (lighter than LunaSVG, more than NanoSVG)
- Interested in integrated rendering library
- Willing to try newer project

**Choose Custom Parser if:**
- Want perfect control over memory and performance
- Need heavily customized game-specific features
- Have 5-8 days for implementation
- Want zero dependencies (or minimal with pugixml)
- Current options don't meet specific needs

## Recommendations Framework

### Primary Recommendation: Start with NanoSVG

**Rationale:**
1. ✅ **Already integrated** (vcpkg.json)
2. ✅ **Covers our core needs** (paths, shapes, transforms)
3. ✅ **Fast** (1-5ms parse time)
4. ✅ **Battle-tested** (used widely)
5. ✅ **Header-only** (easy to use)
6. ✅ **Includes rasterizer** (useful for Tier 1 backgrounds)

**Limitations to Accept:**
- No gradient parsing (can add solid color gradients manually)
- No text (use separate text rendering if needed)
- No advanced filters (don't need for game)

### Fallback Plan: Custom Metadata Extension

If we need custom game metadata (animation data, collision shapes):

```cpp
// Extend NanoSVG output with custom parser
struct GameAsset {
    NSVGimage* svg;          // Parsed by NanoSVG
    AnimationData animation; // Parsed from custom <metadata>
    CollisionData collision; // Parsed from custom <metadata>
};

GameAsset ParseGameAsset(const char* filename) {
    // 1. Parse SVG with NanoSVG
    NSVGimage* svg = nsvgParseFromFile(filename, "px", 96.0f);

    // 2. Parse custom metadata with pugixml (lightweight)
    pugi::xml_document doc;
    doc.load_file(filename);

    AnimationData animation = ParseAnimation(doc.select_node("//metadata/animation"));
    CollisionData collision = ParseCollision(doc.select_node("//metadata/collision"));

    return {svg, animation, collision};
}
```

### Future Consideration: Upgrade if Needed

**Monitor during development:**
- Do assets need features NanoSVG doesn't support?
- Is parse performance adequate?
- Is metadata handling too cumbersome?

**Upgrade path if needed:**
- Gradients required → LunaSVG
- Perfect control needed → Custom parser
- Newer alternative → PlutoVG

## Prototyping Plan

### Phase 1: Validate NanoSVG (1-2 days)
1. Create test SVG assets (paths, shapes, transforms)
2. Parse with NanoSVG
3. Extract path data
4. Measure parse time
5. Test edge cases

### Phase 2: Custom Metadata (if needed, 1-2 days)
1. Design metadata format (XML in `<metadata>` tags)
2. Add pugixml if needed (lightweight)
3. Parse custom data
4. Integrate with NanoSVG output

### Phase 3: Evaluate Gaps (1 day)
1. Identify missing features
2. Decide: acceptable, workaround, or upgrade parser
3. If upgrade needed → prototype LunaSVG or custom

## Related Documentation

- [architecture.md](./architecture.md) - How SVG parsing fits into system
- [tessellation-options.md](./tessellation-options.md) - Consuming parsed path data
- [rendering-backend-options.md](./rendering-backend-options.md) - Rendering pipeline
- [asset-pipeline.md](./asset-pipeline.md) - Asset workflow from creation to runtime

## References

**SVG Specification**:
- SVG 1.1 Paths: https://www.w3.org/TR/SVG/paths.html
- SVG Transforms: https://www.w3.org/TR/SVG/coords.html

**Libraries**:
- NanoSVG: https://github.com/memononen/nanosvg
- LunaSVG: https://github.com/sammycage/lunasvg
- PlutoVG: https://github.com/sammycage/plutovg

**XML Parsers** (if needed for custom parser):
- pugixml: https://github.com/zeux/pugixml (header-only, fast)
- TinyXML-2: https://github.com/leethomason/tinyxml2 (lightweight)

## Revision History

- 2025-10-24: Initial comparative analysis based on research

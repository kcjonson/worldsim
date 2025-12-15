#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Renderer {

// Rasterize an SVG file into RGBA8 buffer at requested size. Returns false on failure.
bool bakeSvgToRgba(const std::string& filepath, int width, int height, std::vector<uint8_t>& outPixels);

} // namespace Renderer

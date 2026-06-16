#pragma once

// PNG encoding helpers, centralized so STB_IMAGE_WRITE_IMPLEMENTATION lives in
// exactly one translation unit (PngEncoder.cpp). Pixels are RGBA8, 4 bytes each.

#include <cstdint>
#include <string>
#include <vector>

namespace Foundation { // NOLINT(readability-identifier-naming)

	// Flip RGBA8 rows vertically (GL bottom-left origin -> image top-left origin).
	std::vector<unsigned char> flipRowsRGBA(const unsigned char* rgba, int width, int height);

	// Encode RGBA8 pixels (top-left origin) to PNG bytes in memory. Empty on failure.
	std::vector<unsigned char> encodePngToMemory(const unsigned char* rgba, int width, int height);

	// Write RGBA8 pixels (top-left origin) to a PNG file. Returns false on failure.
	bool writePngToFile(const unsigned char* rgba, int width, int height, const std::string& path);

} // namespace Foundation

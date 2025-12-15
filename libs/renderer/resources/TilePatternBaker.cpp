#include "resources/TilePatternBaker.h"

#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>
#include <nanosvg.h>
#include <utils/Log.h>

namespace Renderer {

bool bakeSvgToRgba(const std::string& filepath, int width, int height, std::vector<uint8_t>& outPixels) {
	outPixels.clear();
	if (width <= 0 || height <= 0) {
		return false;
	}

	NSVGimage* image = nsvgParseFromFile(filepath.c_str(), "px", 96.0F);
	if (image == nullptr) {
		LOG_ERROR(Renderer, "Failed to parse SVG: %s", filepath.c_str());
		return false;
	}

	NSVGrasterizer* rast = nsvgCreateRasterizer();
	if (rast == nullptr) {
		nsvgDelete(image);
		LOG_ERROR(Renderer, "Failed to create NanoSVG rasterizer");
		return false;
	}

	outPixels.resize(static_cast<size_t>(width * height * 4), 0);

	nsvgRasterize(rast, image, 0.0F, 0.0F, static_cast<float>(width) / image->width, outPixels.data(), width, height, width * 4);

	nsvgDeleteRasterizer(rast);
	nsvgDelete(image);
	return true;
}

} // namespace Renderer

#include "graphics/PngEncoder.h"

// Single definition site for stb_image_write across the whole build.
#ifndef WORLDSIM_STB_IMAGE_WRITE_IMPL
#define WORLDSIM_STB_IMAGE_WRITE_IMPL
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#endif

#include <cstring>

namespace Foundation {

	std::vector<unsigned char> flipRowsRGBA(const unsigned char* rgba, int width, int height) {
		const size_t			   rowBytes = static_cast<size_t>(width) * 4;
		std::vector<unsigned char> flipped(rowBytes * static_cast<size_t>(height));
		for (int y = 0; y < height; ++y) {
			std::memcpy(
				flipped.data() + (static_cast<size_t>(height - 1 - y) * rowBytes),
				rgba + (static_cast<size_t>(y) * rowBytes),
				rowBytes
			);
		}
		return flipped;
	}

	std::vector<unsigned char> encodePngToMemory(const unsigned char* rgba, int width, int height) {
		std::vector<unsigned char> out;
		auto					   writeFunc = [](void* context, void* data, int size) {
			auto* vec = static_cast<std::vector<unsigned char>*>(context);
			auto* bytes = static_cast<unsigned char*>(data);
			vec->insert(vec->end(), bytes, bytes + size); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
		};
		if (stbi_write_png_to_func(writeFunc, &out, width, height, 4, rgba, width * 4) == 0) {
			out.clear();
		}
		return out;
	}

	bool writePngToFile(const unsigned char* rgba, int width, int height, const std::string& path) {
		return stbi_write_png(path.c_str(), width, height, 4, rgba, width * 4) != 0;
	}

} // namespace Foundation

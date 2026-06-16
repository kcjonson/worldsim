#include "assets/AssetRenderer.h"

#include "assets/AssetRegistry.h"
#include "graphics/PngEncoder.h"
#include "primitives/Primitives.h"
#include "resources/RenderToTexture.h"
#include "utils/Log.h"
#include "vector/MeshBounds.h"

#include <GL/glew.h>

#include <algorithm>

namespace engine::assets {

	PreparedAsset prepareAsset(const std::string& defName, const Foundation::Rect& target, uint32_t seed) {
		PreparedAsset result;
		if (!AssetRegistry::Get().buildMesh(defName, seed, result.mesh)) {
			result.hasOutput = false;
			return result;
		}
		const Foundation::Rect bounds = renderer::computeBounds(result.mesh);
		renderer::fitToRect(result.mesh, bounds, target);
		return result;
	}

	std::vector<PreparedAsset> prepareSamples(const std::string& defName, const Foundation::Rect& target, int count, uint32_t baseSeed) {
		std::vector<PreparedAsset> samples;
		samples.reserve(static_cast<size_t>(std::max(0, count)));
		for (int i = 0; i < count; ++i) {
			samples.push_back(prepareAsset(defName, target, baseSeed + static_cast<uint32_t>(i)));
		}
		return samples;
	}

	std::vector<uint8_t> renderToPixels(const std::string& defName, int width, int height, Foundation::Color background, uint32_t seed) {
		if (width <= 0 || height <= 0) {
			LOG_ERROR(Engine, "renderToPixels: invalid size %dx%d", width, height);
			return {};
		}

		// Prepare geometry fit to the framebuffer in pixel space.
		const Foundation::Rect frame{0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height)};
		PreparedAsset		   prepared = prepareAsset(defName, frame, seed);

		// Force the screen-space ortho fallback (no window/DPI coordinate system) so
		// the projection matches the FBO size exactly.
		Renderer::Primitives::setCoordinateSystem(nullptr);
		Renderer::Primitives::setViewport(width, height);

		Renderer::RenderToTexture rtt(width, height);
		rtt.begin();

		glClearColor(background.r, background.g, background.b, background.a);
		glClear(GL_COLOR_BUFFER_BIT);

		Renderer::Primitives::beginFrame();
		if (prepared.hasOutput && !prepared.mesh.indices.empty()) {
			Renderer::Primitives::drawTriangles({
				.vertices = prepared.mesh.vertices.data(),
				.indices = prepared.mesh.indices.data(),
				.vertexCount = prepared.mesh.vertices.size(),
				.indexCount = prepared.mesh.indices.size(),
				.color = Foundation::Color(0.7F, 0.7F, 0.7F, 1.0F), // fallback if the mesh has no per-vertex colors
				.colors = prepared.mesh.hasColors() ? prepared.mesh.colors.data() : nullptr,
			});
		}
		Renderer::Primitives::endFrame();

		std::vector<uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
		glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

		rtt.end();

		// GL origin is bottom-left; flip to top-left for image output.
		return Foundation::flipRowsRGBA(pixels.data(), width, height);
	}

	bool renderToPng(const std::string& defName, const std::string& outPath, int width, int height, Foundation::Color background, uint32_t seed) {
		const std::vector<uint8_t> pixels = renderToPixels(defName, width, height, background, seed);
		if (pixels.empty()) {
			return false;
		}
		return Foundation::writePngToFile(pixels.data(), width, height, outPath);
	}

} // namespace engine::assets

#include "assets/AssetRenderer.h"

#include "assets/AssetRegistry.h"
#include "primitives/Primitives.h"
#include "resources/RenderTarget.h"
#include "utils/Log.h"
#include "vector/MeshBounds.h"

#include <PngEncoder.h>

#include <algorithm>
#include <cstdint>

namespace assets {

	namespace {

		Foundation::Rect unionBounds(const std::vector<renderer::TessellatedMesh>& meshes) {
			Foundation::Rect bounds{0.0F, 0.0F, 0.0F, 0.0F};
			bool			 first = true;
			for (const auto& mesh : meshes) {
				const Foundation::Rect b = renderer::computeBounds(mesh);
				if (first) {
					bounds = b;
					first = false;
					continue;
				}
				const float right = std::max(bounds.x + bounds.width, b.x + b.width);
				const float bottom = std::max(bounds.y + bounds.height, b.y + b.height);
				bounds.x = std::min(bounds.x, b.x);
				bounds.y = std::min(bounds.y, b.y);
				bounds.width = right - bounds.x;
				bounds.height = bottom - bounds.y;
			}
			return bounds;
		}

		// Where renderer::fitToRect puts `src` inside `dst`.
		Foundation::Rect fittedRect(const Foundation::Rect& src, const Foundation::Rect& dst) {
			const float scale = std::min(dst.width / src.width, dst.height / src.height);
			const float w = src.width * scale;
			const float h = src.height * scale;
			return {dst.x + ((dst.width - w) * 0.5F), dst.y + ((dst.height - h) * 0.5F), w, h};
		}

	} // namespace

	PreparedAsset prepareAsset(const std::string& defName, const Foundation::Rect& target, uint32_t seed) {
		PreparedAsset		   result;
		AssetRegistry&		   registry = AssetRegistry::Get();
		const AssetDefinition* def = registry.getDefinition(defName);
		if (def == nullptr || !registry.buildMeshes(defName, seed, result.meshes)) {
			result.hasOutput = false;
			return result;
		}
		const bool			   ground = def->role == AssetRole::Ground;
		const float			   period = def->ground.periodMeters;
		const Foundation::Rect bounds = ground ? Foundation::Rect{0.0F, 0.0F, period, period} : unionBounds(result.meshes);
		for (auto& mesh : result.meshes) {
			renderer::fitToRect(mesh, bounds, target);
		}
		result.sourceBounds = bounds;
		result.targetRect = target;
		if (ground && bounds.width > 0.0F) {
			result.clip = fittedRect(bounds, target);
		}
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

		Renderer::RenderTarget target = Renderer::RenderTarget::create(width, height);
		if (!target.isValid()) {
			LOG_ERROR(Engine, "renderToPixels: could not create a %dx%d render target", width, height);
			return {};
		}
		target.begin();

		// Force the screen-space ortho fallback (no window/DPI coordinate system) so
		// the projection matches the target size exactly. After begin(), so the size
		// is the target's and not remembered as the backbuffer's.
		Renderer::Primitives::setCoordinateSystem(nullptr);
		Renderer::Primitives::setViewport(width, height);

		Renderer::Primitives::clear(background);

		Renderer::Primitives::beginFrame();
		if (prepared.clip) {
			Renderer::Primitives::pushClip(*prepared.clip);
		}
		for (const auto& mesh : prepared.meshes) {
			if (mesh.indices.empty()) {
				continue;
			}
			Renderer::Primitives::drawTriangles({
				.vertices = mesh.vertices.data(),
				.indices = mesh.indices.data(),
				.vertexCount = mesh.vertices.size(),
				.indexCount = mesh.indices.size(),
				.color = Foundation::Color(0.7F, 0.7F, 0.7F, 1.0F), // fallback if the mesh has no per-vertex colors
				.colors = mesh.hasColors() ? mesh.colors.data() : nullptr,
			});
		}
		if (prepared.clip) {
			Renderer::Primitives::popClip();
		}
		Renderer::Primitives::endFrame();

		std::vector<uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
		target.readPixels(pixels);

		target.end();

		// The target holds premultiplied colour; images and callers take straight alpha.
		for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
			const uint32_t alpha = pixels[i + 3];
			if (alpha == 0 || alpha == 255) {
				continue;
			}
			for (size_t c = 0; c < 3; ++c) {
				pixels[i + c] = static_cast<uint8_t>(std::min<uint32_t>(255U, ((pixels[i + c] * 255U) + (alpha / 2U)) / alpha));
			}
		}

		// readPixels gives rows bottom-up; flip to top-left for image output.
		return Renderer::flipRowsRGBA(pixels.data(), width, height);
	}

	bool renderToPng(const std::string& defName, const std::string& outPath, int width, int height, Foundation::Color background, uint32_t seed) {
		const std::vector<uint8_t> pixels = renderToPixels(defName, width, height, background, seed);
		if (pixels.empty()) {
			return false;
		}
		return Renderer::writePngToFile(pixels.data(), width, height, outPath);
	}

} // namespace assets

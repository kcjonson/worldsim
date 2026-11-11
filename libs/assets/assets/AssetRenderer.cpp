#include "assets/AssetRenderer.h"

#include "assets/AssetRegistry.h"
#include "primitives/Primitives.h"
#include "resources/RenderTarget.h"
#include "utils/Log.h"
#include "vector/MeshBounds.h"

#include <PngEncoder.h>

#include <algorithm>
#include <cmath>
#include <functional>
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

		void drawMesh(const renderer::TessellatedMesh& mesh) {
			if (mesh.indices.empty()) {
				return;
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

		// Clears a width x height offscreen target to `background`, runs `draw` in its pixel space,
		// and reads it back as straight-alpha RGBA8 with the top row first.
		std::vector<uint8_t> renderOffscreen(int width, int height, Foundation::Color background, const std::function<void()>& draw) {
			Renderer::RenderTarget target = Renderer::RenderTarget::create(width, height);
			if (!target.isValid()) {
				LOG_ERROR(Engine, "renderOffscreen: could not create a %dx%d render target", width, height);
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
			draw();
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

	} // namespace

	PreparedAsset prepareAsset(const std::string& defName, const Foundation::Rect& target, uint32_t seed, float pixelScale) {
		PreparedAsset		   result;
		AssetRegistry&		   registry = AssetRegistry::Get();
		const AssetDefinition* def = registry.getDefinition(defName);
		const bool			   ground = def != nullptr && def->role == AssetRole::Ground;
		const float			   period = ground ? def->ground.periodMeters : 0.0F;
		const float			   pixelsPerMeter =
			   ground && period > 0.0F ? std::min(target.width, target.height) * pixelScale / period : AssetRegistry::kFullDetail;
		if (def == nullptr || !registry.buildMeshes(defName, seed, result.meshes, pixelsPerMeter)) {
			result.hasOutput = false;
			return result;
		}
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
		const PreparedAsset	   prepared = prepareAsset(defName, frame, seed);

		return renderOffscreen(width, height, background, [&prepared]() {
			if (prepared.clip) {
				Renderer::Primitives::pushClip(*prepared.clip);
			}
			for (const auto& mesh : prepared.meshes) {
				drawMesh(mesh);
			}
			if (prepared.clip) {
				Renderer::Primitives::popClip();
			}
		});
	}

	bool renderToPng(const std::string& defName, const std::string& outPath, int width, int height, Foundation::Color background, uint32_t seed) {
		const std::vector<uint8_t> pixels = renderToPixels(defName, width, height, background, seed);
		if (pixels.empty()) {
			return false;
		}
		return Renderer::writePngToFile(pixels.data(), width, height, outPath);
	}

	RenderedImage renderMeshSheet(
		const std::vector<std::vector<const renderer::TessellatedMesh*>>& meshes, float pixelsPerMeter, Foundation::Color background, bool silhouette
	) {
		static constexpr float kPadPixels = 8.0F;
		static constexpr float kMaxSide = 16384.0F;

		size_t columns = 0;
		// Reach of the origin in each direction over every template, in metres.
		float left = 0.0F;
		float right = 0.0F;
		float up = 0.0F;
		float down = 0.0F;
		for (const auto& row : meshes) {
			for (const renderer::TessellatedMesh* mesh : row) {
				if (mesh == nullptr || mesh->vertices.empty()) {
					LOG_ERROR(Engine, "renderMeshSheet: a cell has no mesh");
					return {};
				}
				const Foundation::Rect b = renderer::computeBounds(*mesh);
				left = std::max(left, -b.x);
				right = std::max(right, b.x + b.width);
				up = std::max(up, -b.y);
				down = std::max(down, b.y + b.height);
			}
			columns = std::max(columns, row.size());
		}
		const float cellW = std::ceil((left + right) * pixelsPerMeter + (2.0F * kPadPixels));
		const float cellH = std::ceil((up + down) * pixelsPerMeter + (2.0F * kPadPixels));
		const float width = cellW * static_cast<float>(columns);
		const float height = cellH * static_cast<float>(meshes.size());
		if (columns == 0 || width > kMaxSide || height > kMaxSide) {
			LOG_ERROR(Engine, "renderMeshSheet: a %.0fx%.0f sheet is empty or too large", width, height);
			return {};
		}

		// Each cell keeps its own copies until the frame ends, so the draws may hold them unflushed.
		const Foundation::Color					   flat(0.16F, 0.16F, 0.16F, 1.0F);
		std::vector<std::vector<Foundation::Vec2>> placedCells;
		std::vector<std::vector<uint16_t>>		   keptCells;
		RenderedImage							   image{.width = static_cast<int>(width), .height = static_cast<int>(height)};
		image.pixels = renderOffscreen(image.width, image.height, background, [&]() {
			for (size_t r = 0; r < meshes.size(); ++r) {
				for (size_t c = 0; c < meshes[r].size(); ++c) {
					const renderer::TessellatedMesh& mesh = *meshes[r][c];
					const float						 ox = (static_cast<float>(c) * cellW) + kPadPixels + (left * pixelsPerMeter);
					const float						 oy = (static_cast<float>(r) * cellH) + kPadPixels + (up * pixelsPerMeter);
					auto& placed = placedCells.emplace_back();
					for (const Foundation::Vec2& v : mesh.vertices) {
						placed.emplace_back(ox + (v.x * pixelsPerMeter), oy + (v.y * pixelsPerMeter));
					}
					if (!silhouette) {
						Renderer::Primitives::drawTriangles({
							.vertices = placed.data(),
							.indices = mesh.indices.data(),
							.vertexCount = placed.size(),
							.indexCount = mesh.indices.size(),
							.color = Foundation::Color(0.7F, 0.7F, 0.7F, 1.0F),
							.colors = mesh.hasColors() ? mesh.colors.data() : nullptr,
						});
						continue;
					}
					const auto faint = [&mesh](uint16_t i) {
						return mesh.hasColors() && mesh.colors[i].a < AssetRegistry::kSilhouetteMinAlpha;
					};
					auto& kept = keptCells.emplace_back();
					for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
						const uint16_t i0 = mesh.indices[t];
						const uint16_t i1 = mesh.indices[t + 1];
						const uint16_t i2 = mesh.indices[t + 2];
						if (!(faint(i0) && faint(i1) && faint(i2))) {
							kept.insert(kept.end(), {i0, i1, i2});
						}
					}
					if (!kept.empty()) {
						Renderer::Primitives::drawTriangles({
							.vertices = placed.data(),
							.indices = kept.data(),
							.vertexCount = placed.size(),
							.indexCount = kept.size(),
							.color = flat,
						});
					}
				}
			}
		});
		if (image.pixels.empty()) {
			return {};
		}
		return image;
	}

} // namespace assets

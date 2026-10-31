#include "AssetThumbnail.h"

#include <assets/AssetRegistry.h>
#include <assets/AssetRenderer.h>
#include <assets/MotionEval.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <theme/Tokens.h>
#include <ui/Context.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace asset_manager {

	namespace {

		// Mesh plus the raw (pre-fit) bounds it was built from, so the same fit
		// transform can be replayed for overlays (e.g. collision outlines).
		struct CachedMesh {
			renderer::TessellatedMesh mesh;
			Foundation::Rect		  sourceBounds{0.0F, 0.0F, 0.0F, 0.0F};
		};

		// Shared across all thumbnails so rebuilt rows reuse meshes (no re-tessellation). The
		// asset-manager is single-threaded -- reload runs synchronously on the UI loop, never
		// concurrent with render -- so this map and cacheGeneration() need no locking.
		std::unordered_map<std::string, CachedMesh>& meshCache() {
			static std::unordered_map<std::string, CachedMesh> cache;
			return cache;
		}

		// Bumped whenever meshCache is cleared. A thumbnail that fetched its mesh at an older
		// generation must refetch before use -- the CachedMesh it points into is destroyed by
		// clearCache(), so drawing through the stale pointer is a use-after-free.
		uint64_t& cacheGeneration() {
			static uint64_t gen = 0;
			return gen;
		}

		// Wall-clock-driven walk phase [0,1) for the preview sweep, so an asset with a motion
		// animates continuously through its cycle. ~0.8 cycles/sec reads as a relaxed walk.
		float sweepPhase() {
			static const auto start = std::chrono::steady_clock::now();
			const float		  secs = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
			const float		  p = secs * 0.8F;
			return p - std::floor(p);
		}

	} // namespace

	AssetThumbnail::AssetThumbnail(const Args& args)
		: defName(args.defName),
		  seed(args.seed),
		  animated(args.animated),
		  collider(args.collider) {}

	void AssetThumbnail::clearCache() {
		meshCache().clear();
		++cacheGeneration();
	}

	void AssetThumbnail::setAsset(std::string name, uint32_t assetSeed) {
		if (name != defName || assetSeed != seed) {
			defName = std::move(name);
			seed = assetSeed;
			dirty = true;
		}
	}

	void AssetThumbnail::ensureMesh() {
		// Refetch when dirty, resized, or when the cache was cleared since the last fetch: clearCache()
		// frees the CachedMesh the mesh points into, so a stale pointer is a use-after-free.
		const Foundation::Vec2 size = getSize();
		const uint64_t		   gen = cacheGeneration();
		if (!dirty && cacheGen == gen && targetRect.width == size.x && targetRect.height == size.y) {
			return;
		}
		dirty = false;
		cacheGen = gen;
		mesh = nullptr;
		sourceBounds = {0.0F, 0.0F, 0.0F, 0.0F};
		targetRect = {0.0F, 0.0F, size.x, size.y};
		if (defName.empty() || size.x <= 0.0F || size.y <= 0.0F) {
			return;
		}
		const std::string key = defName + "@" + std::to_string(static_cast<int>(size.x)) + "x" + std::to_string(static_cast<int>(size.y)) +
								"#" + std::to_string(seed);
		auto& cache = meshCache();
		auto  it = cache.find(key);
		if (it == cache.end()) {
			assets::PreparedAsset		  prepared = assets::prepareAsset(defName, targetRect, seed);
			CachedMesh					  entry;
			entry.mesh = std::move(prepared.mesh);
			entry.sourceBounds = prepared.sourceBounds;
			it = cache.emplace(key, std::move(entry)).first;
		}
		mesh = &it->second.mesh;
		sourceBounds = it->second.sourceBounds;
	}

	// Replays fitToRect (see renderer::fitToRect): scale to fit the source bounds into this box
	// preserving aspect, and centre.
	Foundation::Vec2 AssetThumbnail::meshToLocal(const glm::vec2& point) const {
		const Foundation::Rect& src = sourceBounds;
		const Foundation::Rect& dst = targetRect;
		// Degenerate source: fitToRect leaves verts untransformed, so the overlay must too.
		if (src.width <= 0.0F || src.height <= 0.0F) {
			return {point.x, point.y};
		}
		const float scale = std::min(dst.width / src.width, dst.height / src.height);
		const float srcCenterX = src.x + (src.width * 0.5F);
		const float srcCenterY = src.y + (src.height * 0.5F);
		return {
			dst.x + (dst.width * 0.5F) + ((point.x - srcCenterX) * scale), dst.y + (dst.height * 0.5F) + ((point.y - srcCenterY) * scale)
		};
	}

	// A point relative to the asset's bbox centre (the entity origin): fitToRect maps that centre to the
	// box's centre, so this just scales from there.
	Foundation::Vec2 AssetThumbnail::centeredToLocal(const glm::vec2& point) const {
		const Foundation::Rect& src = sourceBounds;
		const Foundation::Rect& dst = targetRect;
		if (src.width <= 0.0F || src.height <= 0.0F) {
			return {point.x, point.y};
		}
		const float scale = std::min(dst.width / src.width, dst.height / src.height);
		return {dst.x + (dst.width * 0.5F) + (point.x * scale), dst.y + (dst.height * 0.5F) + (point.y * scale)};
	}

	void AssetThumbnail::render() {
		ensureMesh();
		if (mesh != nullptr && !mesh->vertices.empty()) {
			// Reused across frames/instances so posing the verts doesn't allocate each frame.
			thread_local std::vector<Foundation::Vec2> verts;
			verts.assign(mesh->vertices.begin(), mesh->vertices.end());

			// Animate: if this asset declares a motion, sweep its first clip's phase over time and
			// deform the part vertex ranges in the fitted preview space, so the preview shows the
			// walk cycle in motion. Assets without a motion (the vast majority) skip this entirely.
			const auto*				motion = animated && !mesh->parts.empty() ? assets::AssetRegistry::Get().getMotion(defName) : nullptr;
			const Foundation::Rect& src = sourceBounds;
			const Foundation::Rect& dst = targetRect;
			if (motion != nullptr && src.width > 0.0F && src.height > 0.0F) {
				const assets::MotionClip* walk = motion->findClip("walk");
				const assets::MotionClip* clip = (walk != nullptr) ? walk : &motion->clips.front();
				if (!clip->drivers.empty()) {
					std::unordered_map<std::string, assets::PartTransform> xforms;
					assets::evaluateClip(*clip, sweepPhase(), xforms);
					const float		s = std::min(dst.width / src.width, dst.height / src.height);
					const glm::vec2 srcC{src.x + (src.width * 0.5F), src.y + (src.height * 0.5F)};
					const glm::vec2 dstC{dst.x + (dst.width * 0.5F), dst.y + (dst.height * 0.5F)};
					for (const auto& part : mesh->parts) {
						if (part.name.empty()) {
							continue;
						}
						auto it = xforms.find(part.name);
						if (it == xforms.end()) {
							continue;
						}
						// Convert pivot + translate from meter space into the fitted preview space
						// (rotation and scale are scale-invariant).
						assets::PartTransform pt = it->second;
						pt.pivot = {dstC.x + ((pt.pivot.x - srcC.x) * s), dstC.y + ((pt.pivot.y - srcC.y) * s)};
						pt.translate = {pt.translate.x * s, pt.translate.y * s};
						const uint32_t end = std::min<uint32_t>(part.vertexStart + part.vertexCount, static_cast<uint32_t>(verts.size()));
						for (uint32_t i = part.vertexStart; i < end; ++i) {
							const glm::vec2 r = pt.apply({verts[i].x, verts[i].y});
							verts[i] = {r.x, r.y};
						}
					}
				}
			}

			const float									fade = opacity();
			const Foundation::Color*					colors = mesh->hasColors() ? mesh->colors.data() : nullptr;
			thread_local std::vector<Foundation::Color> faded;
			if (colors != nullptr && fade < 1.0F) {
				faded.assign(mesh->colors.begin(), mesh->colors.end());
				for (Foundation::Color& color : faded) {
					color.a *= fade;
				}
				colors = faded.data();
			}
			Renderer::Primitives::drawTriangles({
				.vertices = verts.data(),
				.indices = mesh->indices.data(),
				.vertexCount = verts.size(),
				.indexCount = mesh->indices.size(),
				.color = Foundation::Color(0.7F, 0.7F, 0.7F, fade),
				.colors = colors,
				.key = drawKey(),
			});
		}
		if (collider) {
			drawCollider();
		}
	}

	// The asset's collision shape over the art, re-resolved from the name each frame, since a reload
	// clears the registry and a stored definition would dangle.
	void AssetThumbnail::drawCollider() {
		const assets::AssetDefinition* def = assets::AssetRegistry::Get().getDefinition(defName);
		if (def == nullptr) {
			return;
		}
		if (!def->collision.blocks()) {
			Foundation::Color faint = getContext()->getTheme().get(theme::tokens::theme::color::textFaint);
			faint.a *= opacity();
			Renderer::Primitives::drawText({
				.text = "no collider",
				.position = {8.0F, 8.0F},
				.scale = 12.0F / 16.0F,
				.color = faint,
				.id = "am_no_collider",
				.key = drawKey(),
			});
			return;
		}
		if (mesh == nullptr || mesh->vertices.empty()) {
			return;
		}
		// Collision points are in the asset's local meter space. Simple (SVG) assets author the collider
		// in SVG <metadata>, converted to the bbox-centred frame (relative to the entity origin);
		// procedural colliders are in the generator's mesh-coord frame.
		std::vector<glm::vec2> points;
		if (def->collision.type == assets::CollisionShapeType::Rect) {
			const std::array<glm::vec2, 4> corners = def->collision.rectCornersLocal();
			points.assign(corners.begin(), corners.end());
		} else {
			points = def->collision.pointsMeters;
		}
		if (points.size() < 2) {
			return;
		}
		const bool				centered = def->assetType == assets::AssetType::Simple;
		const Foundation::Color outline{0.0F, 1.0F, 1.0F, opacity()}; // bright cyan, high contrast on art
		for (std::size_t i = 0; i < points.size(); ++i) {
			const glm::vec2& a = points[i];
			const glm::vec2& b = points[(i + 1) % points.size()];
			Renderer::Primitives::drawLine({
				.start = centered ? centeredToLocal(a) : meshToLocal(a),
				.end = centered ? centeredToLocal(b) : meshToLocal(b),
				.style = {.color = outline, .width = 2.0F},
				.id = "am_collision_edge",
				.key = drawKey(),
			});
		}
	}

} // namespace asset_manager

#include "AssetThumbnail.h"

#include <CoordinateSystem/CoordinateSystem.h>
#include <assets/AssetRegistry.h>
#include <assets/AssetRenderer.h>
#include <assets/MotionEval.h>
#include <graphics/Color.h>
#include <math/Types.h>
#include <primitives/Primitives.h>
#include <theme/Tokens.h>
#include <ui/Context.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace asset_manager {

	// Meshes plus the raw (pre-fit) bounds they were built from, so the same fit
	// transform can be replayed for overlays (e.g. collision outlines).
	struct ThumbnailMesh {
		std::vector<renderer::TessellatedMesh> meshes;
		Foundation::Rect					   sourceBounds{0.0F, 0.0F, 0.0F, 0.0F};
		std::optional<Foundation::Rect>		   clip;
	};

	namespace {

		// What the cache may hold before it drops the least recently fetched meshes. A thumbnail drawing one
		// keeps it alive through its own reference.
		constexpr size_t kMeshCacheBudget = size_t{128} * 1024 * 1024;

		struct CacheEntry {
			std::shared_ptr<const ThumbnailMesh> mesh;
			size_t								 bytes = 0;
			uint64_t							 lastFetch = 0;
		};

		// Shared across all thumbnails so rebuilt rows reuse meshes (no re-tessellation). The
		// asset-manager is single-threaded -- reload runs synchronously on the UI loop, never
		// concurrent with render -- so this cache needs no locking.
		struct MeshCache {
			std::unordered_map<std::string, CacheEntry> entries;
			size_t										bytes = 0;
			uint64_t									fetches = 0;
			// Bumped on every clear, so a thumbnail that fetched before a reload fetches the new art.
			uint64_t generation = 0;
		};

		MeshCache& meshCache() {
			static MeshCache cache;
			return cache;
		}

		size_t meshBytes(const ThumbnailMesh& mesh) {
			size_t bytes = 0;
			for (const renderer::TessellatedMesh& m : mesh.meshes) {
				bytes += (m.vertices.size() * sizeof(Foundation::Vec2)) + (m.colors.size() * sizeof(Foundation::Color)) +
						 (m.indices.size() * sizeof(uint16_t));
			}
			return bytes;
		}

		// Drops the least recently fetched entries, but never `keep`, until the cache fits its budget.
		void trimMeshCache(const std::string& keep) {
			MeshCache& cache = meshCache();
			while (cache.bytes > kMeshCacheBudget && cache.entries.size() > 1) {
				auto oldest = cache.entries.end();
				for (auto it = cache.entries.begin(); it != cache.entries.end(); ++it) {
					if (it->first != keep && (oldest == cache.entries.end() || it->second.lastFetch < oldest->second.lastFetch)) {
						oldest = it;
					}
				}
				cache.bytes -= oldest->second.bytes;
				cache.entries.erase(oldest);
			}
		}

		// Wall-clock-driven walk phase [0,1) for the preview sweep, so an asset with a motion
		// animates continuously through its cycle. ~0.8 cycles/sec reads as a relaxed walk.
		float sweepPhase() {
			static const auto start = std::chrono::steady_clock::now();
			const float		  secs = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
			const float		  p = secs * 0.8F;
			return p - std::floor(p);
		}

		// Draw one mesh, its parts posed by `xforms` (meter space) in the preview space that `src`
		// was fit into `dst`, and faded by `fade`.
		void drawMesh(
			const renderer::TessellatedMesh&							  mesh,
			const std::unordered_map<std::string, assets::PartTransform>& xforms,
			const Foundation::Rect&										  src,
			const Foundation::Rect&										  dst,
			float														  fade,
			int32_t														  key
		) {
			if (mesh.indices.empty()) {
				return;
			}
			// Reused across frames/instances so posing the verts doesn't allocate each frame.
			thread_local std::vector<Foundation::Vec2> posed;
			const Foundation::Vec2*					   verts = mesh.vertices.data();
			if (!xforms.empty() && !mesh.parts.empty()) {
				posed.assign(mesh.vertices.begin(), mesh.vertices.end());
				const float		s = std::min(dst.width / src.width, dst.height / src.height);
				const glm::vec2 srcC{src.x + (src.width * 0.5F), src.y + (src.height * 0.5F)};
				const glm::vec2 dstC{dst.x + (dst.width * 0.5F), dst.y + (dst.height * 0.5F)};
				for (const auto& part : mesh.parts) {
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
					const uint32_t end = std::min<uint32_t>(part.vertexStart + part.vertexCount, static_cast<uint32_t>(posed.size()));
					for (uint32_t i = part.vertexStart; i < end; ++i) {
						const glm::vec2 r = pt.apply({posed[i].x, posed[i].y});
						posed[i] = {r.x, r.y};
					}
				}
				verts = posed.data();
			}

			const Foundation::Color*					colors = mesh.hasColors() ? mesh.colors.data() : nullptr;
			thread_local std::vector<Foundation::Color> faded;
			if (colors != nullptr && fade < 1.0F) {
				faded.assign(mesh.colors.begin(), mesh.colors.end());
				for (Foundation::Color& color : faded) {
					color.a *= fade;
				}
				colors = faded.data();
			}
			Renderer::Primitives::drawTriangles({
				.vertices = verts,
				.indices = mesh.indices.data(),
				.vertexCount = mesh.vertices.size(),
				.indexCount = mesh.indices.size(),
				.color = Foundation::Color(0.7F, 0.7F, 0.7F, fade),
				.colors = colors,
				.key = key,
			});
		}

	} // namespace

	AssetThumbnail::AssetThumbnail(const Args& args)
		: defName(args.defName),
		  seed(args.seed),
		  animated(args.animated),
		  collider(args.collider),
		  tiles(std::max(1, args.tiles)),
		  coordinateSystem(args.coordinateSystem) {}

	void AssetThumbnail::clearCache() {
		MeshCache& cache = meshCache();
		cache.entries.clear();
		cache.bytes = 0;
		++cache.generation;
	}

	float AssetThumbnail::pixelScale() const {
		const float ratio = coordinateSystem != nullptr ? coordinateSystem->getPixelRatio() : 1.0F;
		return ratio * (getContext() != nullptr ? getContext()->getScale() : 1.0F);
	}

	void AssetThumbnail::setAsset(std::string name, uint32_t assetSeed) {
		if (name != defName || assetSeed != seed) {
			defName = std::move(name);
			seed = assetSeed;
			dirty = true;
		}
	}

	void AssetThumbnail::ensureMesh() {
		// Refetch when dirty, resized, moved to a screen of another pixel ratio, or after a reload cleared
		// the cache, which means the art may have changed.
		const Foundation::Vec2 size = getSize();
		const float			   scale = pixelScale();
		MeshCache&			   cache = meshCache();
		if (!dirty && cacheGen == cache.generation && fetchedSize.x == size.x && fetchedSize.y == size.y && fetchedScale == scale) {
			return;
		}
		dirty = false;
		cacheGen = cache.generation;
		fetchedSize = size;
		fetchedScale = scale;
		mesh.reset();
		repeat = 1;
		sourceBounds = {0.0F, 0.0F, 0.0F, 0.0F};
		targetRect = {0.0F, 0.0F, size.x, size.y};
		clip.reset();
		if (defName.empty() || size.x <= 0.0F || size.y <= 0.0F) {
			return;
		}
		// A tiled ground tile fits its centre tile; render() repeats it around that one.
		const assets::AssetDefinition* def = assets::AssetRegistry::Get().getDefinition(defName);
		repeat = def != nullptr && def->role == assets::AssetRole::Ground ? tiles : 1;
		if (repeat > 1) {
			const float cell = std::min(size.x, size.y) / static_cast<float>(repeat);
			const float mid = static_cast<float>(repeat / 2);
			targetRect = {
				((size.x - (cell * static_cast<float>(repeat))) * 0.5F) + (mid * cell),
				((size.y - (cell * static_cast<float>(repeat))) * 0.5F) + (mid * cell),
				cell,
				cell
			};
		}
		const std::string key = defName + "@" + std::to_string(static_cast<int>(size.x)) + "x" + std::to_string(static_cast<int>(size.y)) +
								"#" + std::to_string(seed) + "*" + std::to_string(repeat) + "/" + std::to_string(scale);
		auto it = cache.entries.find(key);
		if (it == cache.entries.end()) {
			assets::PreparedAsset prepared = assets::prepareAsset(defName, targetRect, seed, scale);
			auto				  built = std::make_shared<ThumbnailMesh>();
			built->meshes = std::move(prepared.meshes);
			built->sourceBounds = prepared.sourceBounds;
			built->clip = prepared.clip;
			const size_t bytes = meshBytes(*built);
			it = cache.entries.emplace(key, CacheEntry{.mesh = std::move(built), .bytes = bytes}).first;
			cache.bytes += bytes;
		}
		it->second.lastFetch = ++cache.fetches;
		mesh = it->second.mesh;
		sourceBounds = mesh->sourceBounds;
		clip = mesh->clip;
		trimMeshCache(key);
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
		if (mesh != nullptr && !mesh->meshes.empty()) {
			drawMeshes();
		}
		if (collider) {
			drawCollider();
		}
	}

	void AssetThumbnail::drawMeshes() {
		// Animate: if this asset declares a motion, sweep its first clip's phase over time and
		// deform the part vertex ranges in the fitted preview space, so the preview shows the
		// walk cycle in motion. Assets without a motion (the vast majority) skip this entirely.
		std::unordered_map<std::string, assets::PartTransform> xforms;
		const bool											   framed = sourceBounds.width > 0.0F && sourceBounds.height > 0.0F;
		const auto* motion = animated && framed ? assets::AssetRegistry::Get().getMotion(defName) : nullptr;
		if (motion != nullptr) {
			const assets::MotionClip* walk = motion->findClip("walk");
			const assets::MotionClip* motionClip = (walk != nullptr) ? walk : &motion->clips.front();
			if (!motionClip->drivers.empty()) {
				assets::evaluateClip(*motionClip, sweepPhase(), xforms);
			}
		}
		const float fade = opacity();

		if (!clip.has_value()) {
			for (const renderer::TessellatedMesh& part : mesh->meshes) {
				drawMesh(part, xforms, sourceBounds, targetRect, fade, drawKey());
			}
			return;
		}
		// A ground tile: each copy clipped to its own square, so the art crossing its edges shows only
		// where the wrap put it. The meshes are fit to the centre copy.
		const int mid = repeat / 2;
		for (int row = 0; row < repeat; ++row) {
			for (int col = 0; col < repeat; ++col) {
				const float dx = static_cast<float>(col - mid) * clip->width;
				const float dy = static_cast<float>(row - mid) * clip->height;
				Renderer::Primitives::pushClip({clip->x + dx, clip->y + dy, clip->width, clip->height});
				Renderer::Primitives::PushTransform(glm::translate(Foundation::Mat4(1.0F), glm::vec3(dx, dy, 0.0F)));
				for (const renderer::TessellatedMesh& part : mesh->meshes) {
					drawMesh(part, xforms, sourceBounds, targetRect, fade, drawKey());
				}
				Renderer::Primitives::PopTransform();
				Renderer::Primitives::popClip();
			}
		}
	}

	// The asset's collision shape over the art, re-resolved from the name each frame, since a reload
	// clears the registry and a stored definition would dangle.
	void AssetThumbnail::drawCollider() {
		const assets::AssetDefinition* def = assets::AssetRegistry::Get().getDefinition(defName);
		if (def == nullptr) {
			return;
		}
		const assets::CollisionShape& collision = def->collisionFor(0);
		if (!collision.blocks()) {
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
		if (mesh == nullptr || mesh->meshes.empty()) {
			return;
		}
		// Collision points are in the asset's local meter space. Simple (SVG) assets author the collider
		// in SVG <metadata>, converted to the bbox-centred frame (relative to the entity origin);
		// procedural colliders are in the generator's mesh-coord frame.
		std::vector<glm::vec2> points;
		if (collision.type == assets::CollisionShapeType::Rect) {
			const std::array<glm::vec2, 4> corners = collision.rectCornersLocal();
			points.assign(corners.begin(), corners.end());
		} else {
			points = collision.pointsMeters;
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

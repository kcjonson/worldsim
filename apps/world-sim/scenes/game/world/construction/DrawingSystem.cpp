#include "DrawingSystem.h"

#include <assets/ConstructionRegistry.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Structure.h>
#include <ecs/components/StructureBlueprint.h>
#include <ecs/components/StructureHealth.h>
#include <ecs/components/Transform.h>
#include <primitives/Primitives.h>
#include <theme/Theme.h>
#include <utils/Log.h>

#include <cmath>

namespace world_sim {

	namespace {

		using engine::assets::ConstructionRegistry;
		namespace ec = engine::construction;

		// Centroid of a polygon (world meters). Falls back to the vertex average
		// for a degenerate ring so the spawned entity always has a sane Position.
		Foundation::Vec2 polygonCentroid(const std::vector<Foundation::Vec2>& pts) {
			double cx = 0.0;
			double cy = 0.0;
			double a  = 0.0;
			const std::size_t n = pts.size();
			for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
				const double cross = static_cast<double>(pts[j].x) * pts[i].y - static_cast<double>(pts[i].x) * pts[j].y;
				a += cross;
				cx += (pts[j].x + pts[i].x) * cross;
				cy += (pts[j].y + pts[i].y) * cross;
			}
			if (std::abs(a) < 1e-9) {
				Foundation::Vec2 avg{0.0F, 0.0F};
				for (const auto& p : pts) {
					avg.x += p.x;
					avg.y += p.y;
				}
				if (n > 0) {
					avg.x /= static_cast<float>(n);
					avg.y /= static_cast<float>(n);
				}
				return avg;
			}
			a *= 0.5;
			return {static_cast<float>(cx / (6.0 * a)), static_cast<float>(cy / (6.0 * a))};
		}

	} // namespace

	DrawingSystem::DrawingSystem(const Args& args)
		: ecsWorld_(args.world), camera_(args.camera), callbacks_(args.callbacks) {}

	void DrawingSystem::activateFoundationTool() {
		state_ = DrawingState::Drawing;
		points_.clear();
		lastSnap_		= {};
		lastValidation_ = {};
		if (callbacks_.onToolActive) {
			callbacks_.onToolActive(true);
		}
		LOG_INFO(Game, "Foundation tool activated (material=%s)", activeMaterial_.c_str());
	}

	void DrawingSystem::deactivate() {
		state_ = DrawingState::Idle;
		points_.clear();
		lastSnap_		= {};
		lastValidation_ = {};
		if (callbacks_.onToolActive) {
			callbacks_.onToolActive(false);
		}
	}

	void DrawingSystem::setActiveMaterial(const std::string& material) {
		activeMaterial_ = material;
	}

	void DrawingSystem::handleMouseMove(float screenX, float screenY, int viewportW, int viewportH, bool freeform) {
		if (state_ != DrawingState::Drawing || camera_ == nullptr) {
			return;
		}
		const auto world = camera_->screenToWorld(screenX, screenY, viewportW, viewportH, kPixelsPerMeter);

		auto&			 registry = ConstructionRegistry::Get();
		ec::SnapEngine	 snap(registry.snapping(), constructionWorld_);
		lastSnap_	= snap.snap(points_, {world.x, world.y}, freeform);
		cursor_		= lastSnap_.point;

		ec::ConstructionValidator validator(registry.constraints(), constructionWorld_);
		lastValidation_ = validator.validatePoint(points_, cursor_);
	}

	bool DrawingSystem::handleClick(float screenX, float screenY, int viewportW, int viewportH, bool freeform) {
		if (state_ != DrawingState::Drawing || camera_ == nullptr) {
			return false;
		}

		// Refresh snap/validity at the exact click position so a click commits the
		// same point the preview showed.
		handleMouseMove(screenX, screenY, viewportW, viewportH, freeform);

		// Origin-close (>= 3 points) commits the shape.
		if (lastSnap_.closesShape() && points_.size() >= 3) {
			commitShape();
			return true;
		}

		// Reject the point if it would violate a hard per-vertex constraint.
		if (!lastValidation_.ok()) {
			if (callbacks_.onToast) {
				callbacks_.onToast("Invalid point", ec::validationReason(lastValidation_.code));
			}
			return true; // consumed: the click was a deliberate (rejected) action
		}

		points_.push_back(cursor_);
		return true;
	}

	bool DrawingSystem::cancel() {
		if (state_ != DrawingState::Drawing) {
			return false;
		}
		if (!points_.empty()) {
			// Esc / right-click with points in progress: discard the shape, stay in
			// the tool (matches the design: a second Esc, now empty, exits).
			points_.clear();
			lastValidation_ = {};
			return true;
		}
		// Nothing in progress: exit the tool.
		deactivate();
		return true;
	}

	bool DrawingSystem::removeLastPoint() {
		if (state_ != DrawingState::Drawing || points_.empty()) {
			return false;
		}
		points_.pop_back();
		lastValidation_ = {};
		return true;
	}

	void DrawingSystem::commitShape() {
		auto&					  registry = ConstructionRegistry::Get();
		ec::ConstructionValidator validator(registry.constraints(), constructionWorld_);
		const auto				  result = validator.validateRing(points_);
		if (!result.ok()) {
			if (callbacks_.onToast) {
				callbacks_.onToast("Cannot close", ec::validationReason(result.code));
			}
			return;
		}

		const auto commit = constructionWorld_.commitFoundation(points_, activeMaterial_);
		if (!commit.ok()) {
			// The structural/overlap reject path: surface it plainly. The validator
			// already gates most of these, so this catches the rare race / rounding.
			if (callbacks_.onToast) {
				callbacks_.onToast("Commit failed", "foundation rejected");
			}
			return;
		}

		spawnBlueprintEntity(commit.id);

		if (callbacks_.onToast) {
			callbacks_.onToast("Foundation placed", activeMaterial_ + " blueprint");
		}

		// Stay in the tool, ready for the next shape.
		points_.clear();
		lastValidation_ = {};
	}

	ecs::EntityID DrawingSystem::spawnBlueprintEntity(ec::FoundationId id) {
		if (ecsWorld_ == nullptr) {
			return ecs::EntityID{0};
		}

		const auto* foundation = constructionWorld_.get(id);
		if (foundation == nullptr) {
			return ecs::EntityID{0};
		}

		const float area = constructionWorld_.areaSquareMeters(id);

		// Material-driven manifest, work, and HP. Invariant: the manifest defName IS
		// the material name; construction config keys materials by name and the haul
		// chain resolves items by that same name, so the two stay aligned by design.
		const auto& registry = ConstructionRegistry::Get();
		const auto* material = registry.getMaterial(activeMaterial_);
		float		costRate = 0.0F;
		float		workRate = 0.0F;
		float		hpRate	 = 0.0F;
		if (material != nullptr) {
			costRate = material->costRatePerSquareMeter;
			workRate = material->workRatePerSquareMeter;
			hpRate	 = material->hp;
		}

		auto entity = ecsWorld_->createEntity();

		// Position at the polygon centroid (world meters). Centroid keeps the
		// entity's transform inside its own footprint for concave shapes too.
		const Foundation::Vec2 centroid = polygonCentroid(points_);
		ecsWorld_->addComponent<ecs::Position>(entity, ecs::Position{{centroid.x, centroid.y}});

		ecsWorld_->addComponent<ecs::Structure>(entity, ecs::Structure{ecs::StructureKind::Foundation, id});

		ecs::StructureBlueprint blueprint;
		blueprint.phase = ecs::StructureBlueprint::BuildPhase::Clearing;
		const auto requiredQty = static_cast<uint32_t>(std::ceil(static_cast<double>(area) * static_cast<double>(costRate)));
		if (requiredQty > 0) {
			blueprint.required.emplace_back(activeMaterial_, requiredQty);
		}
		blueprint.workTotal = area * workRate;
		ecsWorld_->addComponent<ecs::StructureBlueprint>(entity, std::move(blueprint));

		// Delivery inventory: hauled materials land here, and ConstructionSystem reconciles the
		// blueprint's delivered[] manifest from it. One slot per material type is enough; large
		// stacks so a whole foundation's Wood fits in a single slot.
		ecs::Inventory deliveryInv;
		deliveryInv.maxCapacity	 = 8;
		deliveryInv.maxStackSize = 100000;
		ecsWorld_->addComponent<ecs::Inventory>(entity, std::move(deliveryInv));

		// HP scales with area; full HP is only meaningful once built but the
		// component exists from creation (architecture: avoid a later migration).
		const float maxHp = area * hpRate;
		ecsWorld_->addComponent<ecs::StructureHealth>(entity, ecs::StructureHealth{maxHp, maxHp});

		constructionWorld_.setEntity(id, entity);

		LOG_INFO(
			Game,
			"Foundation #%llu spawned: %.1f m^2, %s, %u materials, %.0f work, entity %u",
			static_cast<unsigned long long>(id),
			static_cast<double>(area),
			activeMaterial_.c_str(),
			requiredQty,
			static_cast<double>(blueprint.workTotal),
			static_cast<uint32_t>(entity)
		);
		return entity;
	}

	DrawingStatus DrawingSystem::status() const {
		DrawingStatus s;
		s.active	 = (state_ == DrawingState::Drawing);
		s.pointCount = static_cast<int>(points_.size());
		s.material	 = activeMaterial_;

		// Area preview: only meaningful once a closeable shape exists.
		if (points_.size() >= 3) {
			auto&			 registry = ConstructionRegistry::Get();
			ec::ConstructionValidator validator(registry.constraints(), constructionWorld_);
			const auto		 ring = validator.validateRing(points_);
			s.valid	  = ring.ok();
			s.message = ec::validationReason(ring.code);
			// Recompute area regardless of validity so the readout tracks the shape.
			geometry::Ring quantized;
			quantized.reserve(points_.size());
			for (const auto& p : points_) {
				quantized.push_back(geometry::quantize(p));
			}
			s.areaSquareMeters = static_cast<float>(std::abs(geometry::signedAreaSquareMeters(quantized)));
		} else {
			s.valid	  = lastValidation_.ok();
			s.message = ec::validationReason(lastValidation_.code);
		}
		return s;
	}

	// =========================================================================
	// INTERIM rendering. Committed-foundation rendering here is a placeholder;
	// C6 replaces it with the baked element-emitter + build-progress prefix.
	// =========================================================================

	void DrawingSystem::render(int viewportW, int viewportH) {
		if (camera_ == nullptr) {
			return;
		}

		const float scale = kPixelsPerMeter * camera_->zoom();

		auto toScreen = [&](Foundation::Vec2 w) -> Foundation::Vec2 {
			return camera_->worldToScreen(w.x, w.y, viewportW, viewportH, kPixelsPerMeter);
		};

		// --- Committed foundations: build-progress visualization (D8) ----------
		// Each foundation renders in three layers: a faint translucent "blueprint" base
		// fill always, then a material-colored fill whose alpha ramps with build progress
		// (workDone/workTotal from the blueprint entity), then an outline whose weight/
		// brightness firms up as the structure approaches Built. A proportional opacity
		// ramp is the slice's progress viz; the baked element-emitter index-prefix (a
		// deterministic per-element reveal) is the later optimization noted in D8.
		for (const auto& f : constructionWorld_.foundations()) {
			const std::size_t n = f.ring.size();
			if (n < 3) {
				continue;
			}
			const bool built = (f.state == engine::construction::FoundationState::Built);

			// Progress in [0,1] from the ECS mirror's blueprint. Built foundations render full.
			float progress = built ? 1.0F : 0.0F;
			if (!built && ecsWorld_ != nullptr && f.entity != ecs::kInvalidEntity) {
				if (const auto* bp = ecsWorld_->getComponent<ecs::StructureBlueprint>(f.entity)) {
					progress = bp->progress();
				}
			}

			// Material color (palette front) drives the progress fill; fall back to blue.
			const auto* mat = ConstructionRegistry::Get().getMaterial(f.material);
			Foundation::Color matColor{0.5F, 0.65F, 0.9F, 1.0F};
			if (mat != nullptr && !mat->pattern.palette.empty()) {
				const auto& c = mat->pattern.palette.front();
				matColor = {c.r / 255.0F, c.g / 255.0F, c.b / 255.0F, 1.0F};
			}

			std::vector<Foundation::Vec2> screen;
			screen.reserve(n);
			for (const auto& v : f.ring) {
				screen.push_back(toScreen(geometry::dequantize(v)));
			}
			std::vector<uint16_t> indices;
			indices.reserve((n - 2) * 3);
			for (std::size_t i = 1; i + 1 < n; ++i) {
				indices.push_back(0);
				indices.push_back(static_cast<uint16_t>(i));
				indices.push_back(static_cast<uint16_t>(i + 1));
			}

			// Layer 1: faint blueprint base (always present, reads as the planned footprint).
			Renderer::Primitives::drawTriangles({
				.vertices	 = screen.data(),
				.indices	 = indices.data(),
				.vertexCount = screen.size(),
				.indexCount	 = indices.size(),
				.color		 = {0.5F, 0.65F, 0.9F, 0.18F},
				.id			 = "committed_foundation_base",
				.zIndex		 = 50,
			});

			// Layer 2: progress fill, alpha proportional to workDone/workTotal. Ramps from a
			// barely-there tint at 0% to a solid floor at 100% / Built.
			if (progress > 0.0F) {
				const float fillAlpha = built ? 0.85F : (0.15F + 0.7F * progress);
				Renderer::Primitives::drawTriangles({
					.vertices	 = screen.data(),
					.indices	 = indices.data(),
					.vertexCount = screen.size(),
					.indexCount	 = indices.size(),
					.color		 = {matColor.r, matColor.g, matColor.b, fillAlpha},
					.id			 = "committed_foundation_progress",
					.zIndex		 = 51,
				});
			}

			// Layer 3: outline, brighter/heavier as it firms up toward Built.
			const float			   outlineAlpha = 0.6F + 0.4F * progress;
			const Foundation::Color outline{0.55F, 0.72F, 1.0F, built ? 1.0F : outlineAlpha};
			for (std::size_t i = 0; i < n; ++i) {
				Renderer::Primitives::drawLine({
					.start = screen[i],
					.end   = screen[(i + 1) % n],
					.style = {.color = outline, .width = built ? 2.0F : 1.5F},
					.id	   = "committed_foundation_edge",
					.zIndex = 52,
				});
			}
		}

		// --- In-progress preview ----------------------------------------------
		if (state_ != DrawingState::Drawing) {
			return;
		}

		const bool			   valid = lastValidation_.ok();
		const Foundation::Color okColor	   = UI::Theme::Colors::statusActive;	// green
		const Foundation::Color badColor   = UI::Theme::Colors::statusBlocked;	// red
		const Foundation::Color lineColor  = valid ? okColor : badColor;

		std::vector<Foundation::Vec2> screen;
		screen.reserve(points_.size());
		for (const auto& p : points_) {
			screen.push_back(toScreen(p));
		}

		// Faint fill once >= 3 points (the implied closed polygon).
		if (points_.size() >= 3) {
			std::vector<Foundation::Vec2> fillPts = screen;
			std::vector<uint16_t>		  indices;
			for (std::size_t i = 1; i + 1 < fillPts.size(); ++i) {
				indices.push_back(0);
				indices.push_back(static_cast<uint16_t>(i));
				indices.push_back(static_cast<uint16_t>(i + 1));
			}
			Foundation::Color fillColor = valid ? Foundation::Color{okColor.r, okColor.g, okColor.b, 0.15F}
												 : Foundation::Color{badColor.r, badColor.g, badColor.b, 0.15F};
			Renderer::Primitives::drawTriangles({
				.vertices	 = fillPts.data(),
				.indices	 = indices.data(),
				.vertexCount = fillPts.size(),
				.indexCount	 = indices.size(),
				.color		 = fillColor,
				.id			 = "drawing_fill_preview",
				.zIndex		 = 900,
			});
		}

		// Committed edges between placed points.
		for (std::size_t i = 0; i + 1 < screen.size(); ++i) {
			Renderer::Primitives::drawLine({
				.start = screen[i],
				.end   = screen[i + 1],
				.style = {.color = okColor, .width = 2.0F},
				.id	   = "drawing_edge",
				.zIndex = 901,
			});
		}

		// Rubber-band from the last placed point to the snapped cursor.
		if (!points_.empty()) {
			Renderer::Primitives::drawLine({
				.start = screen.back(),
				.end   = toScreen(cursor_),
				.style = {.color = lineColor, .width = 2.0F},
				.id	   = "drawing_rubberband",
				.zIndex = 902,
			});
		}

		// Angle-snap guide line (faint) when the cursor is angle-snapped.
		if (lastSnap_.kind == engine::construction::SnapKind::Angle) {
			Renderer::Primitives::drawLine({
				.start = toScreen(lastSnap_.guideFrom),
				.end   = toScreen(lastSnap_.guideTo),
				.style = {.color = {0.7F, 0.85F, 1.0F, 0.4F}, .width = 1.0F},
				.id	   = "drawing_guide",
				.zIndex = 899,
			});
		}

		// Vertex dots.
		for (const auto& sp : screen) {
			Renderer::Primitives::drawCircle({
				.center = sp,
				.radius = 4.0F,
				.style	= {.fill = okColor},
				.id		= "drawing_vertex",
				.zIndex = 903,
			});
		}

		// Origin halo when the shape can close.
		if (points_.size() >= 3) {
			const float originRadius = ConstructionRegistry::Get().snapping().originCloseRadiusMeters * scale;
			const bool	closing		 = lastSnap_.closesShape();
			Renderer::Primitives::drawCircle({
				.center = screen.front(),
				.radius = std::max(8.0F, originRadius),
				.style	= {.fill	= {0.0F, 0.0F, 0.0F, 0.0F},
						   .border = Foundation::BorderStyle{.color = closing ? okColor : Foundation::Color{0.7F, 0.85F, 1.0F, 0.6F},
															 .width = closing ? 3.0F : 1.5F}},
				.id		= "drawing_origin_halo",
				.zIndex = 904,
			});
		}

		// Red highlight on the offending edge/vertex when invalid.
		if (!valid && !screen.empty()) {
			const std::size_t vi = std::min(lastValidation_.vertexIndex, screen.size() - 1);
			Renderer::Primitives::drawCircle({
				.center = screen[vi],
				.radius = 7.0F,
				.style	= {.fill = {badColor.r, badColor.g, badColor.b, 0.5F}},
				.id		= "drawing_invalid_vertex",
				.zIndex = 905,
			});
		}
	}

} // namespace world_sim

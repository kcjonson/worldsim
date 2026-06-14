#include "DrawingSystem.h"

#include <assets/ConstructionRegistry.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Structure.h>
#include <ecs/components/StructureBlueprint.h>
#include <ecs/components/StructureHealth.h>
#include <ecs/components/Transform.h>
#include <offset/WallOffset.h>
#include <primitives/Primitives.h>
#include <theme/Theme.h>
#include <utils/Log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace world_sim {

	namespace {

		using engine::assets::ConstructionRegistry;
		namespace ec = engine::construction;

		// Centroid of a polygon (world meters). Falls back to the vertex average
		// for a degenerate ring so the spawned entity always has a sane Position.
		Foundation::Vec2 polygonCentroid(const std::vector<Foundation::Vec2>& pts) {
			double			  cx = 0.0;
			double			  cy = 0.0;
			double			  a = 0.0;
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
		: ecsWorld_(args.world),
		  camera_(args.camera),
		  callbacks_(args.callbacks) {}

	void DrawingSystem::activateFoundationTool() {
		state_ = DrawingState::Drawing;
		activeTool_ = ToolKind::Foundation;
		points_.clear();
		lastSnap_ = {};
		lastValidation_ = {};
		wallHost_ = ec::kInvalidFoundation;
		if (callbacks_.onToolActive) {
			callbacks_.onToolActive(true);
		}
		LOG_INFO(Game, "Foundation tool activated (material=%s)", activeMaterial_.c_str());
	}

	void DrawingSystem::activateWallTool() {
		state_ = DrawingState::Drawing;
		activeTool_ = ToolKind::Wall;
		points_.clear();
		lastSnap_ = {};
		lastValidation_ = {};
		wallHost_ = ec::kInvalidFoundation;
		if (callbacks_.onToolActive) {
			callbacks_.onToolActive(true);
		}
		LOG_INFO(Game, "Wall tool activated (material=%s, preset=%s)", activeMaterial_.c_str(), activeThicknessPreset_.c_str());
	}

	void DrawingSystem::deactivate() {
		state_ = DrawingState::Idle;
		points_.clear();
		lastSnap_ = {};
		lastValidation_ = {};
		wallHost_ = ec::kInvalidFoundation;
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

		if (activeTool_ == ToolKind::Wall) {
			handleWallMove({world.x, world.y}, freeform);
			return;
		}

		auto&		   registry = ConstructionRegistry::Get();
		ec::SnapEngine snap(registry.snapping(), constructionWorld_);
		lastSnap_ = snap.snap(points_, {world.x, world.y}, freeform);
		cursor_ = lastSnap_.point;

		ec::ConstructionValidator validator(registry.constraints(), constructionWorld_);
		lastValidation_ = validator.validatePoint(points_, cursor_);
	}

	bool DrawingSystem::handleClick(float screenX, float screenY, int viewportW, int viewportH, bool freeform, bool ctrl) {
		if (state_ != DrawingState::Drawing || camera_ == nullptr) {
			return false;
		}

		// Refresh snap/validity at the exact click position so a click commits the
		// same point the preview showed.
		handleMouseMove(screenX, screenY, viewportW, viewportH, freeform);

		if (activeTool_ == ToolKind::Wall) {
			const auto world = camera_->screenToWorld(screenX, screenY, viewportW, viewportH, kPixelsPerMeter);
			return handleWallClick({world.x, world.y}, freeform, ctrl);
		}

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

		if (activeTool_ == ToolKind::Wall) {
			// Walls are an open chain: right-click / Esc ENDS the chain, committing
			// whatever segments are in progress (design: "Right-click or Esc ends the
			// chain"). With a single point (or none) there is nothing to commit, so it
			// just clears; an empty chain then exits the tool.
			if (points_.size() >= 2) {
				commitWallChain();
				return true;
			}
			if (!points_.empty()) {
				points_.clear();
				wallHost_ = ec::kInvalidFoundation;
				lastValidation_ = {};
				return true;
			}
			deactivate();
			return true;
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

	bool DrawingSystem::finishChain() {
		if (state_ != DrawingState::Drawing || activeTool_ != ToolKind::Wall) {
			return false;
		}
		if (points_.size() >= 2) {
			commitWallChain();
			return true;
		}
		// One or zero points: nothing to commit, just reset the chain.
		points_.clear();
		wallHost_ = ec::kInvalidFoundation;
		lastValidation_ = {};
		return true;
	}

	bool DrawingSystem::removeLastPoint() {
		if (state_ != DrawingState::Drawing || points_.empty()) {
			return false;
		}
		points_.pop_back();
		lastValidation_ = {};
		if (points_.empty()) {
			// The chain's host is determined by its first point; once empty, the next
			// first click re-picks it.
			wallHost_ = ec::kInvalidFoundation;
		}
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
		float		hpRate = 0.0F;
		if (material != nullptr) {
			costRate = material->costRatePerSquareMeter;
			workRate = material->workRatePerSquareMeter;
			hpRate = material->hp;
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
		deliveryInv.maxCapacity = 8;
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

	// =========================================================================
	// Wall tool. Mirrors the foundation path: snapWall + validateWallPoint live,
	// per-segment validateWallSegment + commitSegment on finish, one blueprint
	// entity per committed segment. The chain is OPEN and hosted by one foundation.
	// =========================================================================

	const engine::assets::ThicknessPreset* DrawingSystem::activePreset() const {
		return ConstructionRegistry::Get().getThicknessPreset(activeMaterial_, activeThicknessPreset_);
	}

	void DrawingSystem::handleWallMove(Foundation::Vec2 world, bool freeform) {
		auto&		   registry = ConstructionRegistry::Get();
		ec::SnapEngine snap(registry.snapping(), constructionWorld_);
		lastSnap_ = snap.snapWall(points_, world, freeform);
		cursor_ = lastSnap_.point;

		// Live validity: the in-progress chain with the snapped cursor appended,
		// measured against the host foundation's footprint. Without a preset (a
		// material with no wall config) we can't measure thickness, so skip.
		const auto* preset = activePreset();
		if (preset == nullptr) {
			lastValidation_ = {};
			return;
		}

		// The host is the foundation the FIRST point landed on. For the live preview
		// before the first click, probe the cursor's foundation so the colorize and
		// readouts reflect where the chain would start.
		ec::FoundationId host = wallHost_;
		if (host == ec::kInvalidFoundation && points_.empty()) {
			host = constructionWorld_.foundationAt(geometry::quantize(cursor_));
		}

		ec::ConstructionValidator validator(registry.constraints(), constructionWorld_);
		lastValidation_ = validator.validateWallPoint(points_, cursor_, *preset, host);
	}

	bool DrawingSystem::handleWallClick(Foundation::Vec2 world, bool freeform, bool ctrl) {
		const auto* preset = activePreset();
		if (preset == nullptr) {
			if (callbacks_.onToast) {
				callbacks_.onToast("No wall preset", activeMaterial_ + " has no wall thickness");
			}
			return true;
		}

		// Ctrl+click edge fill: place a wall along the whole foundation edge under the
		// cursor, no chain. Right-click is reserved for ending the chain, so Ctrl gets
		// edge fill (design: Edge Fill).
		if (ctrl) {
			tryEdgeFill(world);
			return true;
		}

		// First point: it must land on a foundation; that foundation hosts the chain.
		if (points_.empty()) {
			const ec::FoundationId host = constructionWorld_.foundationAt(geometry::quantize(cursor_));
			if (host == ec::kInvalidFoundation) {
				if (callbacks_.onToast) {
					callbacks_.onToast("No foundation", "walls must start on a foundation");
				}
				return true;
			}
			wallHost_ = host;
			points_.push_back(cursor_);
			return true;
		}

		// Subsequent point: it must stay on the host foundation, and the new segment
		// must pass validation (length, junction angle, containment, clearance).
		if (constructionWorld_.foundationAt(geometry::quantize(cursor_)) != wallHost_) {
			if (callbacks_.onToast) {
				callbacks_.onToast("Off host foundation", "the chain must stay on one foundation");
			}
			return true;
		}

		if (!lastValidation_.ok()) {
			if (callbacks_.onToast) {
				callbacks_.onToast("Invalid point", ec::validationReason(lastValidation_.code));
			}
			return true;
		}

		points_.push_back(cursor_);
		return true;
	}

	void DrawingSystem::commitWallChain() {
		const auto* preset = activePreset();
		if (preset == nullptr || points_.size() < 2) {
			points_.clear();
			wallHost_ = ec::kInvalidFoundation;
			lastValidation_ = {};
			return;
		}

		auto&					  registry = ConstructionRegistry::Get();
		ec::ConstructionValidator validator(registry.constraints(), constructionWorld_);

		int committed = 0;
		int rejected = 0;
		for (std::size_t i = 0; i + 1 < points_.size(); ++i) {
			const Foundation::Vec2 a = points_[i];
			const Foundation::Vec2 b = points_[i + 1];

			const auto v = validator.validateWallSegment(a, b, *preset, wallHost_);
			if (!v.ok()) {
				rejected++;
				continue; // reject-don't-repair: skip the bad segment, keep the rest
			}

			const auto result = constructionWorld_.commitSegment(
				geometry::quantize(a), geometry::quantize(b), activeMaterial_, activeThicknessPreset_, wallHost_
			);
			if (!result.ok()) {
				rejected++;
				continue;
			}

			spawnWallBlueprintEntity(result.id, a, b, *preset);
			committed++;
		}

		if (callbacks_.onToast) {
			if (committed > 0) {
				char buf[48];
				std::snprintf(buf, sizeof(buf), "%d segment%s", committed, committed == 1 ? "" : "s");
				callbacks_.onToast("Wall placed", std::string(buf) + " (" + activeMaterial_ + ")");
			} else if (rejected > 0) {
				callbacks_.onToast("Wall rejected", "no valid segments");
			}
		}

		// Stay in the tool, ready for the next chain.
		points_.clear();
		wallHost_ = ec::kInvalidFoundation;
		lastValidation_ = {};
	}

	bool DrawingSystem::tryEdgeFill(Foundation::Vec2 world) {
		const auto* preset = activePreset();
		if (preset == nullptr) {
			return false;
		}

		const ec::FoundationId host = constructionWorld_.foundationAt(geometry::quantize(world));
		if (host == ec::kInvalidFoundation) {
			if (callbacks_.onToast) {
				callbacks_.onToast("No foundation", "Ctrl+click an edge inside a foundation");
			}
			return false;
		}

		const auto* foundation = constructionWorld_.get(host);
		if (foundation == nullptr || foundation->ring.size() < 2) {
			return false;
		}

		// Nearest foundation edge to the click: place a segment along its full span.
		// (v1 edge fill: the whole edge; gap-aware partial fill against existing walls
		// is a follow-up. Segments still pass through validateWallSegment, which
		// rejects an edge already walled as WallsOverlap, so this never double-stacks.)
		const auto&		  ring = foundation->ring;
		const std::size_t n = ring.size();
		double			  bestDistSq = std::numeric_limits<double>::max();
		Foundation::Vec2  bestA{0.0F, 0.0F};
		Foundation::Vec2  bestB{0.0F, 0.0F};
		for (std::size_t i = 0; i < n; ++i) {
			const Foundation::Vec2 a = geometry::dequantize(ring[i]);
			const Foundation::Vec2 b = geometry::dequantize(ring[(i + 1) % n]);
			const double		   ex = b.x - a.x;
			const double		   ey = b.y - a.y;
			const double		   lenSq = ex * ex + ey * ey;
			double				   t = 0.0;
			if (lenSq > 1e-9) {
				t = ((world.x - a.x) * ex + (world.y - a.y) * ey) / lenSq;
				t = std::clamp(t, 0.0, 1.0);
			}
			const double px = a.x + t * ex;
			const double py = a.y + t * ey;
			const double dSq = (world.x - px) * (world.x - px) + (world.y - py) * (world.y - py);
			if (dSq < bestDistSq) {
				bestDistSq = dSq;
				bestA = a;
				bestB = b;
			}
		}

		auto&					  registry = ConstructionRegistry::Get();
		ec::ConstructionValidator validator(registry.constraints(), constructionWorld_);
		const auto				  v = validator.validateWallSegment(bestA, bestB, *preset, host);
		if (!v.ok()) {
			if (callbacks_.onToast) {
				callbacks_.onToast("Edge fill rejected", ec::validationReason(v.code));
			}
			return false;
		}

		const auto result = constructionWorld_.commitSegment(
			geometry::quantize(bestA), geometry::quantize(bestB), activeMaterial_, activeThicknessPreset_, host
		);
		if (!result.ok()) {
			if (callbacks_.onToast) {
				callbacks_.onToast("Edge fill failed", "segment rejected");
			}
			return false;
		}

		spawnWallBlueprintEntity(result.id, bestA, bestB, *preset);
		if (callbacks_.onToast) {
			callbacks_.onToast("Wall placed", "edge fill (" + activeMaterial_ + ")");
		}
		return true;
	}

	ecs::EntityID DrawingSystem::spawnWallBlueprintEntity(
		ec::SegmentId						   segmentId,
		Foundation::Vec2					   a,
		Foundation::Vec2					   b,
		const engine::assets::ThicknessPreset& preset
	) {
		if (ecsWorld_ == nullptr) {
			return ecs::EntityID{0};
		}

		const double dx = static_cast<double>(b.x) - a.x;
		const double dy = static_cast<double>(b.y) - a.y;
		const float	 lengthMeters = static_cast<float>(std::sqrt(dx * dx + dy * dy));
		const float	 thicknessMeters = preset.thicknessMeters;

		// Wall cost/work/HP scale with the area of the band (length x thickness),
		// times the material's per-m^2 rate, times the preset multiplier
		// (materials.xml: "length x thicknessMeters x rate x multiplier").
		const float areaEquivalent = lengthMeters * thicknessMeters;
		const auto& registry = ConstructionRegistry::Get();
		const auto* material = registry.getMaterial(activeMaterial_);
		float		costRate = 0.0F;
		float		workRate = 0.0F;
		float		hpRate = 0.0F;
		if (material != nullptr) {
			costRate = material->costRatePerSquareMeter * preset.costMultiplier;
			workRate = material->workRatePerSquareMeter * preset.workMultiplier;
			hpRate = material->hp * preset.hpMultiplier;
		}

		auto entity = ecsWorld_->createEntity();

		// Position at the segment midpoint (world meters); the build site / haul
		// destination centers on the wall.
		const Foundation::Vec2 mid{(a.x + b.x) * 0.5F, (a.y + b.y) * 0.5F};
		ecsWorld_->addComponent<ecs::Position>(entity, ecs::Position{{mid.x, mid.y}});

		ecsWorld_->addComponent<ecs::Structure>(entity, ecs::Structure{ecs::StructureKind::Wall, segmentId});

		ecs::StructureBlueprint blueprint;
		// Walls sit on a cleared, built foundation: no clear phase. They start
		// AwaitingMaterials; ConstructionSystem holds them there (umbrella Blocked)
		// until the host foundation is Built, then haul + build proceed.
		blueprint.phase = ecs::StructureBlueprint::BuildPhase::AwaitingMaterials;
		const auto requiredQty = static_cast<uint32_t>(std::ceil(static_cast<double>(areaEquivalent) * static_cast<double>(costRate)));
		if (requiredQty > 0) {
			blueprint.required.emplace_back(activeMaterial_, requiredQty);
		}
		blueprint.workTotal = areaEquivalent * workRate;
		ecsWorld_->addComponent<ecs::StructureBlueprint>(entity, std::move(blueprint));

		ecs::Inventory deliveryInv;
		deliveryInv.maxCapacity = 8;
		deliveryInv.maxStackSize = 100000;
		ecsWorld_->addComponent<ecs::Inventory>(entity, std::move(deliveryInv));

		const float maxHp = areaEquivalent * hpRate;
		ecsWorld_->addComponent<ecs::StructureHealth>(entity, ecs::StructureHealth{maxHp, maxHp});

		constructionWorld_.setSegmentEntity(segmentId, entity);

		const ec::WallSegment* committed = constructionWorld_.getSegment(segmentId);
		const ec::FoundationId hostId = committed != nullptr ? committed->hostFoundation : ec::kInvalidFoundation;

		LOG_INFO(
			Game,
			"Wall segment #%llu spawned: %.2f m x %.2f m (%s/%s), %u materials, %.0f work, host #%llu, entity %u",
			static_cast<unsigned long long>(segmentId),
			static_cast<double>(lengthMeters),
			static_cast<double>(thicknessMeters),
			activeMaterial_.c_str(),
			activeThicknessPreset_.c_str(),
			requiredQty,
			static_cast<double>(blueprint.workTotal),
			static_cast<unsigned long long>(hostId),
			static_cast<uint32_t>(entity)
		);
		return entity;
	}

	DrawingStatus DrawingSystem::status() const {
		DrawingStatus s;
		s.active = (state_ == DrawingState::Drawing);
		s.wall = (activeTool_ == ToolKind::Wall);
		s.pointCount = static_cast<int>(points_.size());
		s.material = activeMaterial_;

		if (activeTool_ == ToolKind::Wall) {
			s.thicknessPreset = activeThicknessPreset_;
			s.valid = lastValidation_.ok();
			s.message = ec::validationReason(lastValidation_.code);

			// Length of the committed chain so far, plus the live rubber-band segment.
			auto dist = [](Foundation::Vec2 a, Foundation::Vec2 b) -> float {
				const double dx = static_cast<double>(b.x) - a.x;
				const double dy = static_cast<double>(b.y) - a.y;
				return static_cast<float>(std::sqrt(dx * dx + dy * dy));
			};
			float total = 0.0F;
			for (std::size_t i = 0; i + 1 < points_.size(); ++i) {
				total += dist(points_[i], points_[i + 1]);
			}
			if (!points_.empty()) {
				s.segmentLengthMeters = dist(points_.back(), cursor_);
			}
			s.totalLengthMeters = total + s.segmentLengthMeters;

			// Cost / work preview = (chain length) x thickness x rate x multiplier.
			const auto* preset = activePreset();
			const auto* material = ConstructionRegistry::Get().getMaterial(activeMaterial_);
			if (preset != nullptr && material != nullptr) {
				const float areaEq = s.totalLengthMeters * preset->thicknessMeters;
				s.wallCost = areaEq * material->costRatePerSquareMeter * preset->costMultiplier;
				s.wallWork = areaEq * material->workRatePerSquareMeter * preset->workMultiplier;
			}
			return s;
		}

		// Foundation: area preview only meaningful once a closeable shape exists.
		if (points_.size() >= 3) {
			auto&					  registry = ConstructionRegistry::Get();
			ec::ConstructionValidator validator(registry.constraints(), constructionWorld_);
			const auto				  ring = validator.validateRing(points_);
			s.valid = ring.ok();
			s.message = ec::validationReason(ring.code);
			// Recompute area regardless of validity so the readout tracks the shape.
			geometry::Ring quantized;
			quantized.reserve(points_.size());
			for (const auto& p : points_) {
				quantized.push_back(geometry::quantize(p));
			}
			s.areaSquareMeters = static_cast<float>(std::abs(geometry::signedAreaSquareMeters(quantized)));
		} else {
			s.valid = lastValidation_.ok();
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
			const auto*		  mat = ConstructionRegistry::Get().getMaterial(f.material);
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
				.vertices = screen.data(),
				.indices = indices.data(),
				.vertexCount = screen.size(),
				.indexCount = indices.size(),
				.color = {0.5F, 0.65F, 0.9F, 0.18F},
				.id = "committed_foundation_base",
				.zIndex = 50,
			});

			// Layer 2: progress fill, alpha proportional to workDone/workTotal. Ramps from a
			// barely-there tint at 0% to a solid floor at 100% / Built.
			if (progress > 0.0F) {
				const float fillAlpha = built ? 0.85F : (0.15F + 0.7F * progress);
				Renderer::Primitives::drawTriangles({
					.vertices = screen.data(),
					.indices = indices.data(),
					.vertexCount = screen.size(),
					.indexCount = indices.size(),
					.color = {matColor.r, matColor.g, matColor.b, fillAlpha},
					.id = "committed_foundation_progress",
					.zIndex = 51,
				});
			}

			// Layer 3: outline, brighter/heavier as it firms up toward Built.
			const float				outlineAlpha = 0.6F + 0.4F * progress;
			const Foundation::Color outline{0.55F, 0.72F, 1.0F, built ? 1.0F : outlineAlpha};
			for (std::size_t i = 0; i < n; ++i) {
				Renderer::Primitives::drawLine({
					.start = screen[i],
					.end = screen[(i + 1) % n],
					.style = {.color = outline, .width = built ? 2.0F : 1.5F},
					.id = "committed_foundation_edge",
					.zIndex = 52,
				});
			}
		}

		// --- Committed walls: trimmed bands + junction polygons (D8) -----------
		// Build a geometry::WallSegment per committed segment (centerline + the
		// preset's halfThicknessMm) and run the whole graph through resolveWallBands;
		// it groups segments by shared integer vertex and emits one trimmed band per
		// segment (same order) plus a junction polygon per join, tiling with no
		// overlap/gap so translucent blueprint fills don't double-draw (D8). Bands
		// take each segment's style (blueprint / progress-ramp / built, like the
		// foundation render); junction polygons take a neutral built-vs-blueprint
		// fill from their incident segments. This is INTERIM, same as the foundation
		// render above.
		renderCommittedWalls(viewportW, viewportH);

		// --- In-progress preview ----------------------------------------------
		if (state_ != DrawingState::Drawing) {
			return;
		}

		// Wall chain preview is a centerline + thickness band, not a closed polygon.
		if (activeTool_ == ToolKind::Wall) {
			renderWallChainPreview(viewportW, viewportH);
			return;
		}

		const bool				valid = lastValidation_.ok();
		const Foundation::Color okColor = UI::Theme::Colors::statusActive;	 // green
		const Foundation::Color badColor = UI::Theme::Colors::statusBlocked; // red
		const Foundation::Color lineColor = valid ? okColor : badColor;

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
				.vertices = fillPts.data(),
				.indices = indices.data(),
				.vertexCount = fillPts.size(),
				.indexCount = indices.size(),
				.color = fillColor,
				.id = "drawing_fill_preview",
				.zIndex = 900,
			});
		}

		// Committed edges between placed points.
		for (std::size_t i = 0; i + 1 < screen.size(); ++i) {
			Renderer::Primitives::drawLine({
				.start = screen[i],
				.end = screen[i + 1],
				.style = {.color = okColor, .width = 2.0F},
				.id = "drawing_edge",
				.zIndex = 901,
			});
		}

		// Rubber-band from the last placed point to the snapped cursor.
		if (!points_.empty()) {
			Renderer::Primitives::drawLine({
				.start = screen.back(),
				.end = toScreen(cursor_),
				.style = {.color = lineColor, .width = 2.0F},
				.id = "drawing_rubberband",
				.zIndex = 902,
			});
		}

		// Angle-snap guide line (faint) when the cursor is angle-snapped.
		if (lastSnap_.kind == engine::construction::SnapKind::Angle) {
			Renderer::Primitives::drawLine({
				.start = toScreen(lastSnap_.guideFrom),
				.end = toScreen(lastSnap_.guideTo),
				.style = {.color = {0.7F, 0.85F, 1.0F, 0.4F}, .width = 1.0F},
				.id = "drawing_guide",
				.zIndex = 899,
			});
		}

		// Vertex dots.
		for (const auto& sp : screen) {
			Renderer::Primitives::drawCircle({
				.center = sp,
				.radius = 4.0F,
				.style = {.fill = okColor},
				.id = "drawing_vertex",
				.zIndex = 903,
			});
		}

		// Origin halo when the shape can close.
		if (points_.size() >= 3) {
			const float originRadius = ConstructionRegistry::Get().snapping().originCloseRadiusMeters * scale;
			const bool	closing = lastSnap_.closesShape();
			Renderer::Primitives::drawCircle({
				.center = screen.front(),
				.radius = std::max(8.0F, originRadius),
				.style =
					{.fill = {0.0F, 0.0F, 0.0F, 0.0F},
					 .border =
						 Foundation::BorderStyle{
							 .color = closing ? okColor : Foundation::Color{0.7F, 0.85F, 1.0F, 0.6F}, .width = closing ? 3.0F : 1.5F
						 }},
				.id = "drawing_origin_halo",
				.zIndex = 904,
			});
		}

		// Red highlight on the offending edge/vertex when invalid.
		if (!valid && !screen.empty()) {
			const std::size_t vi = std::min(lastValidation_.vertexIndex, screen.size() - 1);
			Renderer::Primitives::drawCircle({
				.center = screen[vi],
				.radius = 7.0F,
				.style = {.fill = {badColor.r, badColor.g, badColor.b, 0.5F}},
				.id = "drawing_invalid_vertex",
				.zIndex = 905,
			});
		}
	}

	namespace {

		// Triangulate a CCW ring (fan from vertex 0) and emit a filled polygon plus an
		// outline via Primitives. Screen-space points. Shared by band + junction draws.
		void fillRing(
			const std::vector<Foundation::Vec2>& screen,
			Foundation::Color					 fill,
			Foundation::Color					 outline,
			float								 outlineWidth,
			const char*							 fillId,
			const char*							 edgeId,
			int									 zFill,
			int									 zEdge
		) {
			const std::size_t n = screen.size();
			if (n < 3) {
				return;
			}
			std::vector<uint16_t> indices;
			indices.reserve((n - 2) * 3);
			for (std::size_t i = 1; i + 1 < n; ++i) {
				indices.push_back(0);
				indices.push_back(static_cast<uint16_t>(i));
				indices.push_back(static_cast<uint16_t>(i + 1));
			}
			Renderer::Primitives::drawTriangles({
				.vertices = screen.data(),
				.indices = indices.data(),
				.vertexCount = screen.size(),
				.indexCount = indices.size(),
				.color = fill,
				.id = fillId,
				.zIndex = zFill,
			});
			for (std::size_t i = 0; i < n; ++i) {
				Renderer::Primitives::drawLine({
					.start = screen[i],
					.end = screen[(i + 1) % n],
					.style = {.color = outline, .width = outlineWidth},
					.id = edgeId,
					.zIndex = zEdge,
				});
			}
		}

	} // namespace

	void DrawingSystem::renderCommittedWalls(int viewportW, int viewportH) {
		namespace ec = engine::construction;

		const auto& segs = constructionWorld_.segments();
		if (segs.empty()) {
			return;
		}

		auto toScreen = [&](geometry::Vec2i64 mm) -> Foundation::Vec2 {
			const auto w = geometry::dequantize(mm);
			return camera_->worldToScreen(w.x, w.y, viewportW, viewportH, kPixelsPerMeter);
		};

		// Build the offsetter input for the WHOLE graph at once: shared integer
		// vertices mean resolveWallBands derives every junction by exact-endpoint
		// grouping. Bands come back one-per-segment in this same order, so index i
		// maps straight back to segs[i] for per-segment styling.
		std::vector<geometry::WallSegment> offsetSegs;
		offsetSegs.reserve(segs.size());
		bool anyBuilt = false;
		for (const auto& wseg : segs) {
			const ec::Vertex* v0 = constructionWorld_.getVertex(wseg.v0);
			const ec::Vertex* v1 = constructionWorld_.getVertex(wseg.v1);
			std::int64_t	  halfThick = 0;
			if (const auto* preset = ConstructionRegistry::Get().getThicknessPreset(wseg.material, wseg.thicknessPreset)) {
				halfThick = preset->halfThicknessMm;
			}
			if (v0 == nullptr || v1 == nullptr || halfThick <= 0) {
				offsetSegs.push_back({{0, 0}, {0, 0}, 0}); // placeholder keeps index alignment
				continue;
			}
			offsetSegs.push_back({v0->pos, v1->pos, halfThick});
			if (wseg.state == ec::FoundationState::Built) {
				anyBuilt = true;
			}
		}

		const geometry::WallBands bands = geometry::resolveWallBands(offsetSegs, geometry::kDefaultMiterLimit);
		if (bands.status != geometry::OffsetStatus::Ok) {
			// Reject-don't-repair: a degenerate offset means the topology fed it bad
			// input; skip the band render this frame rather than draw garbage. The
			// centerlines below still show the walls exist.
			for (const auto& wseg : segs) {
				const ec::Vertex* v0 = constructionWorld_.getVertex(wseg.v0);
				const ec::Vertex* v1 = constructionWorld_.getVertex(wseg.v1);
				if (v0 == nullptr || v1 == nullptr) {
					continue;
				}
				Renderer::Primitives::drawLine({
					.start = toScreen(v0->pos),
					.end = toScreen(v1->pos),
					.style = {.color = {0.55F, 0.72F, 1.0F, 0.8F}, .width = 2.0F},
					.id = "committed_wall_fallback",
					.zIndex = 60,
				});
			}
			return;
		}

		// Per-segment bands, styled like the foundation render (z 60-62, above
		// foundations at 50-52, below the in-progress preview at 900+).
		for (std::size_t i = 0; i < bands.bands.size() && i < segs.size(); ++i) {
			const ec::WallSegment& wseg = segs[i];
			const geometry::Ring&  ring = bands.bands[i];
			if (ring.size() < 3) {
				continue;
			}

			const bool built = (wseg.state == ec::FoundationState::Built);
			float	   progress = built ? 1.0F : 0.0F;
			if (!built && ecsWorld_ != nullptr && wseg.entity != ecs::kInvalidEntity) {
				if (const auto* bp = ecsWorld_->getComponent<ecs::StructureBlueprint>(wseg.entity)) {
					progress = bp->progress();
				}
			}

			const auto*		  mat = ConstructionRegistry::Get().getMaterial(wseg.material);
			Foundation::Color matColor{0.5F, 0.65F, 0.9F, 1.0F};
			if (mat != nullptr && !mat->pattern.palette.empty()) {
				const auto& c = mat->pattern.palette.front();
				matColor = {c.r / 255.0F, c.g / 255.0F, c.b / 255.0F, 1.0F};
			}

			std::vector<Foundation::Vec2> screen;
			screen.reserve(ring.size());
			for (const auto& v : ring) {
				screen.push_back(toScreen(v));
			}

			// Blueprint base always; progress fill ramps with workDone/workTotal.
			fillRing(screen, {0.5F, 0.65F, 0.9F, 0.22F}, {0.55F, 0.72F, 1.0F, 0.0F}, 0.0F, "committed_wall_base", "", 60, 60);
			const float				fillAlpha = built ? 0.9F : (0.2F + 0.7F * progress);
			const Foundation::Color outline{0.6F, 0.78F, 1.0F, built ? 1.0F : (0.65F + 0.35F * progress)};
			fillRing(
				screen,
				{matColor.r, matColor.g, matColor.b, fillAlpha},
				outline,
				built ? 2.0F : 1.5F,
				"committed_wall_progress",
				"committed_wall_edge",
				61,
				62
			);
		}

		// Junction polygons fill the corner gaps the trimmed bands leave. Interim
		// styling: a neutral material-tinted fill so chains read continuous; built
		// state lifts the alpha (a per-junction progress mapping is later polish).
		const float junctionAlpha = anyBuilt ? 0.8F : 0.4F;
		for (const geometry::Ring& ring : bands.junctions) {
			if (ring.size() < 3) {
				continue;
			}
			std::vector<Foundation::Vec2> screen;
			screen.reserve(ring.size());
			for (const auto& v : ring) {
				screen.push_back(toScreen(v));
			}
			fillRing(screen, {0.5F, 0.65F, 0.9F, junctionAlpha}, {0.6F, 0.78F, 1.0F, 0.0F}, 0.0F, "committed_wall_junction", "", 61, 61);
		}
	}

	void DrawingSystem::renderWallChainPreview(int viewportW, int viewportH) {
		const float scale = kPixelsPerMeter * camera_->zoom();

		auto toScreen = [&](Foundation::Vec2 w) -> Foundation::Vec2 {
			return camera_->worldToScreen(w.x, w.y, viewportW, viewportH, kPixelsPerMeter);
		};

		const bool				valid = lastValidation_.ok();
		const Foundation::Color okColor = UI::Theme::Colors::statusActive;	 // green
		const Foundation::Color badColor = UI::Theme::Colors::statusBlocked; // red

		std::vector<Foundation::Vec2> screen;
		screen.reserve(points_.size());
		for (const auto& p : points_) {
			screen.push_back(toScreen(p));
		}

		// Thickness band preview for the committed chain + the rubber-band segment,
		// so the player sees the real footprint, not just a centerline.
		const auto* preset = activePreset();
		if (preset != nullptr) {
			const std::int64_t halfThick = preset->halfThicknessMm;

			auto drawBand = [&](Foundation::Vec2 a, Foundation::Vec2 b, Foundation::Color color) {
				const geometry::Ring band = geometry::band(geometry::quantize(a), geometry::quantize(b), halfThick);
				if (band.size() < 3) {
					return;
				}
				std::vector<Foundation::Vec2> bandScreen;
				bandScreen.reserve(band.size());
				for (const auto& v : band) {
					bandScreen.push_back(toScreen(geometry::dequantize(v)));
				}
				fillRing(bandScreen, color, {color.r, color.g, color.b, 0.0F}, 0.0F, "wall_preview_band", "", 906, 906);
			};

			const Foundation::Color bandColor{okColor.r, okColor.g, okColor.b, 0.2F};
			for (std::size_t i = 0; i + 1 < points_.size(); ++i) {
				drawBand(points_[i], points_[i + 1], bandColor);
			}
			if (!points_.empty()) {
				const Foundation::Color rb = valid ? Foundation::Color{okColor.r, okColor.g, okColor.b, 0.2F}
												   : Foundation::Color{badColor.r, badColor.g, badColor.b, 0.2F};
				drawBand(points_.back(), cursor_, rb);
			}
		}

		// Centerlines between committed points.
		for (std::size_t i = 0; i + 1 < screen.size(); ++i) {
			Renderer::Primitives::drawLine({
				.start = screen[i],
				.end = screen[i + 1],
				.style = {.color = okColor, .width = 2.0F},
				.id = "wall_centerline",
				.zIndex = 907,
			});
		}

		// Rubber-band centerline from the last point to the snapped cursor.
		if (!points_.empty()) {
			Renderer::Primitives::drawLine({
				.start = screen.back(),
				.end = toScreen(cursor_),
				.style = {.color = valid ? okColor : badColor, .width = 2.0F},
				.id = "wall_rubberband",
				.zIndex = 908,
			});
		}

		// Angle-snap guide line (faint) when the cursor is angle-snapped.
		if (lastSnap_.kind == engine::construction::SnapKind::Angle) {
			Renderer::Primitives::drawLine({
				.start = toScreen(lastSnap_.guideFrom),
				.end = toScreen(lastSnap_.guideTo),
				.style = {.color = {0.7F, 0.85F, 1.0F, 0.4F}, .width = 1.0F},
				.id = "wall_guide",
				.zIndex = 905,
			});
		}

		// Vertex dots, plus a snap-target ring colored by the snap kind so the player
		// sees endpoint / T-junction / foundation-vertex snaps.
		for (const auto& sp : screen) {
			Renderer::Primitives::drawCircle({
				.center = sp,
				.radius = 4.0F,
				.style = {.fill = okColor},
				.id = "wall_vertex",
				.zIndex = 909,
			});
		}

		using engine::construction::SnapKind;
		if (lastSnap_.kind == SnapKind::WallEndpoint || lastSnap_.kind == SnapKind::WallSegment || lastSnap_.kind == SnapKind::Vertex ||
			lastSnap_.kind == SnapKind::Edge) {
			const Foundation::Color snapColor = (lastSnap_.kind == SnapKind::WallEndpoint || lastSnap_.kind == SnapKind::Vertex)
													? Foundation::Color{1.0F, 0.85F, 0.3F, 0.9F}  // vertex snap: amber
													: Foundation::Color{0.4F, 0.85F, 1.0F, 0.9F}; // edge / T-junction: cyan
			Renderer::Primitives::drawCircle({
				.center = toScreen(cursor_),
				.radius = std::max(6.0F, ConstructionRegistry::Get().snapping().vertexSnapRadiusMeters * scale * 0.5F),
				.style = {.fill = {0.0F, 0.0F, 0.0F, 0.0F}, .border = Foundation::BorderStyle{.color = snapColor, .width = 2.0F}},
				.id = "wall_snap_target",
				.zIndex = 910,
			});
		}
	}

} // namespace world_sim

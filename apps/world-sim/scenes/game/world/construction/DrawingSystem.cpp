#include "DrawingSystem.h"

#include <assets/ConstructionRegistry.h>
#include <construction/OpeningGeometry.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Structure.h>
#include <ecs/components/StructureBlueprint.h>
#include <ecs/components/StructureHealth.h>
#include <ecs/components/Transform.h>
#include <offset/WallOffset.h>
#include <predicates/Predicates.h>
#include <primitives/Primitives.h>
#include <theme/Tokens.h>
#include <utils/Log.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <span>

namespace world_sim {

	namespace {

		using engine::assets::ConstructionRegistry;
		using engine::assets::StyleColor;
		namespace ec = engine::construction;

		// StyleColor (engine config, float rgba) -> Foundation::Color (renderer rgba).
		Foundation::Color toColor(StyleColor c) {
			return {c.r, c.g, c.b, c.a};
		}
		// Same, but with the alpha replaced — for colors whose alpha comes from a
		// progress ramp rather than the configured color's own alpha channel.
		Foundation::Color toColor(StyleColor c, float alpha) {
			return {c.r, c.g, c.b, alpha};
		}

		// Multiply rgb by `factor` (clamped to [0,1]); alpha untouched. Darkens a
		// material color for built edges, jambs, and seams so they read against the
		// flat fill. A built structure wears a darker shade of its own material rather
		// than the blueprint-blue outline, which is reserved for the planned state.
		Foundation::Color darken(Foundation::Color c, float factor) {
			return {
				std::clamp(c.r * factor, 0.0F, 1.0F),
				std::clamp(c.g * factor, 0.0F, 1.0F),
				std::clamp(c.b * factor, 0.0F, 1.0F),
				c.a,
			};
		}

		// True if integer-mm point p lies on segment [a,b] (collinear and within the
		// bounding box, endpoints inclusive).
		bool pointOnSegment(const geometry::Vec2i64& a, const geometry::Vec2i64& b, const geometry::Vec2i64& p) {
			if (geometry::orientation(a, b, p) != geometry::Orientation::Collinear) {
				return false;
			}
			return p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x) && p.y >= std::min(a.y, b.y) && p.y <= std::max(a.y, b.y);
		}

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

		// Segment length in mm (its two vertex positions), or 0 if either vertex is
		// missing. The opening's clear width maps to a centerline half-extent of
		// (widthMm/2) / lengthMm, so a zero-length segment yields no intervals.
		double segmentLengthMm(const ec::ConstructionWorld& world, const ec::WallSegment& seg) {
			const ec::Vertex* v0 = world.getVertex(seg.v0);
			const ec::Vertex* v1 = world.getVertex(seg.v1);
			if (v0 == nullptr || v1 == nullptr) {
				return 0.0;
			}
			const double dx = static_cast<double>(v1->pos.x - v0->pos.x);
			const double dy = static_cast<double>(v1->pos.y - v0->pos.y);
			return std::sqrt(dx * dx + dy * dy);
		}

		// The centerline [t0,t1] intervals every opening on `segmentId` occupies,
		// sorted ascending and clamped to [0,1]. An opening at param t with clear
		// width w on a segment of length L spans [t - (w/2)/L, t + (w/2)/L]. Stable
		// order (openings() insertion order, then t) keeps the gap render deterministic.
		std::vector<std::pair<float, float>> openingIntervalsForSegment(const ec::ConstructionWorld& world, ec::SegmentId segmentId) {
			std::vector<std::pair<float, float>> intervals;
			const ec::WallSegment*				 seg = world.getSegment(segmentId);
			if (seg == nullptr) {
				return intervals;
			}
			const double lengthMm = segmentLengthMm(world, *seg);
			if (lengthMm <= 0.0) {
				return intervals;
			}
			for (const auto& op : world.openings()) {
				if (op.segment != segmentId) {
					continue;
				}
				const auto* type = ConstructionRegistry::Get().getOpeningType(op.type);
				if (type == nullptr || type->widthMm <= 0) {
					continue;
				}
				const float half = static_cast<float>((static_cast<double>(type->widthMm) * 0.5) / lengthMm);
				const float t0 = std::clamp(op.t - half, 0.0F, 1.0F);
				const float t1 = std::clamp(op.t + half, 0.0F, 1.0F);
				if (t1 > t0) {
					intervals.emplace_back(t0, t1);
				}
			}
			std::sort(intervals.begin(), intervals.end());
			return intervals;
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
		willClose_ = false;
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
		willClose_ = false;
		wallHost_ = ec::kInvalidFoundation;
		if (callbacks_.onToolActive) {
			callbacks_.onToolActive(true);
		}
		LOG_INFO(Game, "Wall tool activated (material=%s, preset=%s)", activeMaterial_.c_str(), activeThicknessPreset_.c_str());
	}

	void DrawingSystem::activateOpeningTool(const std::string& openingType) {
		state_ = DrawingState::Drawing;
		activeTool_ = ToolKind::Opening;
		activeOpeningType_ = openingType;
		points_.clear();
		lastSnap_ = {};
		lastValidation_ = {};
		willClose_ = false;
		wallHost_ = ec::kInvalidFoundation;
		openingSnap_ = {};
		openingValidation_ = {};
		if (callbacks_.onToolActive) {
			callbacks_.onToolActive(true);
		}
		LOG_INFO(Game, "Opening tool activated (type=%s)", activeOpeningType_.c_str());
	}

	void DrawingSystem::deactivate() {
		state_ = DrawingState::Idle;
		points_.clear();
		lastSnap_ = {};
		lastValidation_ = {};
		willClose_ = false;
		wallHost_ = ec::kInvalidFoundation;
		openingSnap_ = {};
		openingValidation_ = {};
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
		if (activeTool_ == ToolKind::Opening) {
			handleOpeningMove({world.x, world.y});
			return;
		}

		auto&		   registry = ConstructionRegistry::Get();
		ec::SnapEngine snap(registry.snapping(), constructionWorld_);
		lastSnap_ = snap.snap(points_, {world.x, world.y}, freeform, effectiveOriginCloseRadiusMeters());
		cursor_ = lastSnap_.point;

		ec::ConstructionValidator validator(registry.constraints(), constructionWorld_);
		lastValidation_ = validator.validatePoint(points_, cursor_);

		// "Snap not block": with a closeable shape, a click near the start that would
		// otherwise be rejected closes instead of erroring -- but only when the closed
		// ring is itself valid (never rescue-close into a bad foundation). The explicit
		// origin-close is the SnapKind::Origin case and needs no validity check.
		willClose_ = false;
		if (points_.size() >= 3) {
			if (lastSnap_.closesShape()) {
				willClose_ = true;
			} else if (!lastValidation_.ok() && nearStartVertex(cursor_, closeRescueRadiusMeters()) &&
					   validator.validateRing(points_).ok()) {
				willClose_ = true;
			}
		}
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
		if (activeTool_ == ToolKind::Opening) {
			const auto world = camera_->screenToWorld(screenX, screenY, viewportW, viewportH, kPixelsPerMeter);
			return handleOpeningClick({world.x, world.y});
		}

		// Close the shape (>= 3 points) when the click resolves to a close: the explicit
		// origin-close, or the near-start "snap not block" rescue. willClose_ was set by
		// handleMouseMove (called above) and already requires a valid closed ring for the
		// rescue case, so this never closes into an invalid foundation.
		if (points_.size() >= 3 && willClose_) {
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

		if (activeTool_ == ToolKind::Opening) {
			// The opening tool has no in-progress state to discard (each click is a
			// one-shot placement), so Esc / right-click exits the tool.
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
		willClose_ = false; // recomputed on the next move; don't leave a stale closing halo
		if (points_.empty()) {
			// The chain's host is determined by its first point; once empty, the next
			// first click re-picks it.
			wallHost_ = ec::kInvalidFoundation;
		}
		return true;
	}

	float DrawingSystem::pixelsPerWorldMeter() const {
		const float zoom = (camera_ != nullptr) ? camera_->zoom() : 1.0F;
		return kPixelsPerMeter * (zoom > 0.0F ? zoom : 1.0F);
	}

	float DrawingSystem::effectiveOriginCloseRadiusMeters() const {
		auto&		registry = ConstructionRegistry::Get();
		const float configMeters = registry.snapping().originCloseRadiusMeters;
		// Floor to the same screen-px minimum the origin halo is drawn at, so the
		// catch radius and the visible halo agree at every zoom.
		const float floorPx = registry.rendering().preview.originHaloMinRadiusPx;
		const float scale = pixelsPerWorldMeter();
		const float floorMeters = (scale > 0.0F) ? floorPx / scale : 0.0F;
		return std::max(configMeters, floorMeters);
	}

	float DrawingSystem::closeRescueRadiusMeters() const {
		// Cover the non-adjacent edge-clearance dead-zone around the start (a near-start
		// closing edge within segmentClearance of the first edge is what gets rejected),
		// but never smaller than the zoom-stable origin-close radius.
		const float clearance = ConstructionRegistry::Get().constraints().segmentClearanceMeters;
		return std::max(clearance, effectiveOriginCloseRadiusMeters());
	}

	bool DrawingSystem::nearStartVertex(Foundation::Vec2 p, float radiusMeters) const {
		if (points_.empty()) {
			return false;
		}
		const Foundation::Vec2 o = points_.front();
		const float			   dx = p.x - o.x;
		const float			   dy = p.y - o.y;
		return (dx * dx + dy * dy) <= radiusMeters * radiusMeters;
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
		willClose_ = false;
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

		// The snap insets foundation-edge/corner hits by the wall's half-thickness for
		// outer-face-flush alignment, so resolve the preset first. Without a preset (a
		// material with no wall config) there is no thickness to measure: snap with no
		// inset and skip validation below.
		const auto*		   preset = activePreset();
		const std::int64_t halfThick = preset != nullptr ? preset->halfThicknessMm : 0;
		lastSnap_ = snap.snapWall(points_, world, freeform, halfThick);
		cursor_ = lastSnap_.point;

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

			// A single pair can create several segments (drawn through a junction,
			// or a T-split of an existing wall); count them all as committed.
			committed += static_cast<int>(result.createdSegments.size());
		}

		// One reconcile after the whole chain: spawns a correctly-sized entity for
		// every segment created/split above, and destroys any orphaned by a split.
		reconcileSegmentEntities();

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

	int DrawingSystem::devCommitWalls(
		const std::vector<Foundation::Vec2>& pts,
		const std::string&					 material,
		const std::string&					 thicknessPreset,
		ec::FoundationId					 host,
		bool								 built
	) {
		// Dev/test bypass (mirrors the /api/dev/foundation path): commit each
		// consecutive pair straight to the topology with no soft-validator pass, so a
		// caller can stamp a wall loop in one shot. commitSegment still enforces the
		// hard topology invariants (zero-length, X-crossing, duplicate, T-split).
		int committed = 0;
		for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
			const geometry::Vec2i64 qa = geometry::quantize(pts[i]);
			const geometry::Vec2i64 qb = geometry::quantize(pts[i + 1]);
			const auto				result = constructionWorld_.commitSegment(qa, qb, material, thicknessPreset, host);
			if (!result.ok()) {
				continue;
			}
			if (built) {
				// Flip only the segments lying on the requested chain edge to Built (so
				// the spawned entity mirrors a complete wall and the version bump makes
				// them enclose a room now). A pre-existing wall this edge T-splits yields
				// halves that lie OFF [qa,qb]; they must keep their own (e.g. Blueprint)
				// state rather than be force-completed by the dev call.
				for (const ec::SegmentId id : result.createdSegments) {
					const ec::WallSegment* seg = constructionWorld_.getSegment(id);
					if (seg == nullptr) {
						continue;
					}
					const ec::Vertex* v0 = constructionWorld_.getVertex(seg->v0);
					const ec::Vertex* v1 = constructionWorld_.getVertex(seg->v1);
					if (v0 == nullptr || v1 == nullptr) {
						continue;
					}
					if (pointOnSegment(qa, qb, v0->pos) && pointOnSegment(qa, qb, v1->pos)) {
						constructionWorld_.setSegmentState(id, ec::FoundationState::Built);
					}
				}
			}
			committed += static_cast<int>(result.createdSegments.size());
		}
		reconcileSegmentEntities();
		return committed;
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
		std::size_t		  bestEdge = 0;
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
				bestEdge = i;
			}
		}

		// Outer-face-flush: the wall spans the two mitered inset corners, not the raw
		// foundation corners, so its full thickness sits on the foundation (design
		// "Alignment"). Adjacent edge fills share each corner exactly, joining cleanly.
		const Foundation::Vec2 bestA = ec::outerFaceFlushCorner(ring, bestEdge, preset->halfThicknessMm);
		const Foundation::Vec2 bestB = ec::outerFaceFlushCorner(ring, (bestEdge + 1) % n, preset->halfThicknessMm);

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

		// Reconcile rather than spawn one entity: an edge that crosses an existing
		// wall splits it, so this commit can touch more than result.id.
		reconcileSegmentEntities();
		if (callbacks_.onToast) {
			callbacks_.onToast("Wall placed", "edge fill (" + activeMaterial_ + ")");
		}
		return true;
	}

	ecs::EntityID DrawingSystem::spawnWallSegmentEntity(ec::SegmentId segmentId) {
		if (ecsWorld_ == nullptr) {
			return ecs::EntityID{0};
		}

		const ec::WallSegment* segment = constructionWorld_.getSegment(segmentId);
		if (segment == nullptr || segment->entity != ecs::kInvalidEntity) {
			return segment != nullptr ? segment->entity : ecs::EntityID{0};
		}
		const ec::Vertex* vert0 = constructionWorld_.getVertex(segment->v0);
		const ec::Vertex* vert1 = constructionWorld_.getVertex(segment->v1);
		if (vert0 == nullptr || vert1 == nullptr) {
			return ecs::EntityID{0};
		}

		// Geometry from THIS segment's own endpoints, so a sub-segment of a split
		// span is priced by its real length, not the whole drawn span.
		const Foundation::Vec2 a = geometry::dequantize(vert0->pos);
		const Foundation::Vec2 b = geometry::dequantize(vert1->pos);
		const double		   dx = static_cast<double>(b.x) - a.x;
		const double		   dy = static_cast<double>(b.y) - a.y;
		const float			   lengthMeters = static_cast<float>(std::sqrt(dx * dx + dy * dy));

		// Resolve material + preset from the SEGMENT, not the active tool: split
		// halves inherit the old wall's material/thickness, which may differ.
		const auto& registry = ConstructionRegistry::Get();
		const auto* preset = registry.getThicknessPreset(segment->material, segment->thicknessPreset);
		const auto* material = registry.getMaterial(segment->material);
		const float thicknessMeters = preset != nullptr ? preset->thicknessMeters : 0.0F;

		// Wall cost/work/HP scale with the area of the band (length x thickness),
		// times the material's per-m^2 rate, times the preset multiplier
		// (materials.xml: "length x thicknessMeters x rate x multiplier").
		const float areaEquivalent = lengthMeters * thicknessMeters;
		float		costRate = 0.0F;
		float		workRate = 0.0F;
		float		hpRate = 0.0F;
		if (material != nullptr && preset != nullptr) {
			costRate = material->costRatePerSquareMeter * preset->costMultiplier;
			workRate = material->workRatePerSquareMeter * preset->workMultiplier;
			hpRate = material->hp * preset->hpMultiplier;
		}

		auto entity = ecsWorld_->createEntity();

		// Position at the segment midpoint (world meters); the build site / haul
		// destination centers on the wall.
		const Foundation::Vec2 mid{(a.x + b.x) * 0.5F, (a.y + b.y) * 0.5F};
		ecsWorld_->addComponent<ecs::Position>(entity, ecs::Position{{mid.x, mid.y}});

		ecsWorld_->addComponent<ecs::Structure>(entity, ecs::Structure{ecs::StructureKind::Wall, segmentId});

		const bool				built = segment->state == ec::FoundationState::Built;
		ecs::StructureBlueprint blueprint;
		const auto requiredQty = static_cast<uint32_t>(std::ceil(static_cast<double>(areaEquivalent) * static_cast<double>(costRate)));
		blueprint.workTotal = areaEquivalent * workRate;
		if (built) {
			// A Built segment's halves come from splitting an already-finished wall:
			// their entity mirrors a complete structure (full work done, manifest
			// already delivered) so the construction loop does not re-haul/re-build.
			blueprint.phase = ecs::StructureBlueprint::BuildPhase::Complete;
			blueprint.workDone = blueprint.workTotal;
			if (requiredQty > 0) {
				blueprint.required.emplace_back(segment->material, requiredQty);
				blueprint.delivered.emplace_back(segment->material, requiredQty);
			}
		} else {
			// Walls sit on a cleared, built foundation: no clear phase. They start
			// AwaitingMaterials; ConstructionSystem holds them there (umbrella
			// Blocked) until the host foundation is Built, then haul + build proceed.
			blueprint.phase = ecs::StructureBlueprint::BuildPhase::AwaitingMaterials;
			if (requiredQty > 0) {
				blueprint.required.emplace_back(segment->material, requiredQty);
			}
		}
		ecsWorld_->addComponent<ecs::StructureBlueprint>(entity, std::move(blueprint));

		ecs::Inventory deliveryInv;
		deliveryInv.maxCapacity = 8;
		deliveryInv.maxStackSize = 100000;
		ecsWorld_->addComponent<ecs::Inventory>(entity, std::move(deliveryInv));

		const float maxHp = areaEquivalent * hpRate;
		ecsWorld_->addComponent<ecs::StructureHealth>(entity, ecs::StructureHealth{maxHp, maxHp});

		constructionWorld_.setSegmentEntity(segmentId, entity);

		LOG_INFO(
			Game,
			"Wall segment #%llu spawned: %.2f m x %.2f m (%s/%s), %u materials, %.0f work, host #%llu, entity %u, %s",
			static_cast<unsigned long long>(segmentId),
			static_cast<double>(lengthMeters),
			static_cast<double>(thicknessMeters),
			segment->material.c_str(),
			segment->thicknessPreset.c_str(),
			requiredQty,
			static_cast<double>(areaEquivalent * workRate),
			static_cast<unsigned long long>(segment->hostFoundation),
			static_cast<uint32_t>(entity),
			built ? "built" : "blueprint"
		);
		return entity;
	}

	void DrawingSystem::reconcileSegmentEntities() {
		if (ecsWorld_ == nullptr) {
			return;
		}

		// Destroy wall entities whose segment is gone (orphaned by a T-junction
		// split: splitSegmentAt clears the old segment's entity handle but the
		// entity itself outlives the topology record). Collect first, destroy
		// after, so we never mutate the registry mid-view-iteration.
		std::vector<ecs::EntityID> orphans;
		for (auto [entity, structure] : ecsWorld_->view<ecs::Structure>()) {
			if (structure.kind != ecs::StructureKind::Wall) {
				continue;
			}
			if (constructionWorld_.getSegment(structure.graphId) == nullptr) {
				orphans.push_back(entity);
			}
		}
		for (const ecs::EntityID orphan : orphans) {
			ecsWorld_->destroyEntity(orphan);
		}

		// Spawn an entity for every segment that lacks one (newly created chain
		// segments and the two halves of any split). spawnWallSegmentEntity no-ops
		// on a segment that already has one, so this is idempotent.
		for (const ec::WallSegment& segment : constructionWorld_.segments()) {
			if (segment.entity == ecs::kInvalidEntity) {
				spawnWallSegmentEntity(segment.id);
			}
		}
	}

	// =========================================================================
	// Opening tool. Unlike foundation/wall there is no multi-click chain: each
	// click is a one-shot placement of a door/window onto the nearest BUILT wall.
	// snapOpening picks the segment + centerline t; validateOpening gates the
	// commit (margins, overlap, segment length); spawnOpeningBlueprintEntity
	// mirrors spawnWallSegmentEntity with the type's CONSTANT manifest/work.
	// =========================================================================

	const engine::assets::OpeningTypeDef* DrawingSystem::activeOpeningType() const {
		return ConstructionRegistry::Get().getOpeningType(activeOpeningType_);
	}

	void DrawingSystem::handleOpeningMove(Foundation::Vec2 world) {
		const auto* type = activeOpeningType();
		if (type == nullptr) {
			openingSnap_ = {};
			openingValidation_ = {};
			return;
		}

		auto&		   registry = ConstructionRegistry::Get();
		ec::SnapEngine snap(registry.snapping(), constructionWorld_);
		openingSnap_ = snap.snapOpening(world, type->widthMeters);
		cursor_ = openingSnap_.point;

		if (openingSnap_.valid) {
			ec::ConstructionValidator validator(registry.constraints(), constructionWorld_);
			openingValidation_ = validator.validateOpening(openingSnap_.segment, openingSnap_.t, type->name, type->material);
		} else {
			openingValidation_ = {};
		}
	}

	bool DrawingSystem::handleOpeningClick(Foundation::Vec2 world) {
		const auto* type = activeOpeningType();
		if (type == nullptr) {
			if (callbacks_.onToast) {
				callbacks_.onToast("Unknown opening", activeOpeningType_ + " is not a known type");
			}
			return true;
		}

		// Refresh the snap/validity at the exact click position so a click commits the
		// same placement the preview showed.
		handleOpeningMove(world);

		if (!openingSnap_.valid) {
			if (callbacks_.onToast) {
				callbacks_.onToast("No wall", "openings must sit on a built wall");
			}
			return true;
		}
		if (!openingValidation_.ok()) {
			if (callbacks_.onToast) {
				callbacks_.onToast("Invalid opening", ec::validationReason(openingValidation_.code));
			}
			return true;
		}

		const ec::OpeningId id = constructionWorld_.addOpening(openingSnap_.segment, openingSnap_.t, type->name, type->material);
		if (id == ec::kInvalidOpening) {
			if (callbacks_.onToast) {
				callbacks_.onToast("Commit failed", "opening rejected");
			}
			return true;
		}

		spawnOpeningBlueprintEntity(id, /*built=*/false);

		if (callbacks_.onToast) {
			callbacks_.onToast("Opening placed", type->name + " blueprint");
		}

		// Stay in the tool, ready for the next placement.
		return true;
	}

	ec::OpeningId DrawingSystem::devCommitOpening(ec::SegmentId segment, float t, const std::string& openingType, bool built) {
		const auto* type = ConstructionRegistry::Get().getOpeningType(openingType);
		if (type == nullptr || constructionWorld_.getSegment(segment) == nullptr) {
			return ec::kInvalidOpening;
		}

		const ec::OpeningId id = constructionWorld_.addOpening(segment, t, type->name, type->material);
		if (id == ec::kInvalidOpening) {
			return ec::kInvalidOpening;
		}
		if (built) {
			constructionWorld_.setOpeningState(id, ec::FoundationState::Built);
		}
		spawnOpeningBlueprintEntity(id, built);
		return id;
	}

	ecs::EntityID DrawingSystem::spawnOpeningBlueprintEntity(ec::OpeningId openingId, bool built) {
		if (ecsWorld_ == nullptr) {
			return ecs::EntityID{0};
		}

		const ec::Opening* opening = constructionWorld_.getOpening(openingId);
		if (opening == nullptr || opening->entity != ecs::kInvalidEntity) {
			return opening != nullptr ? opening->entity : ecs::EntityID{0};
		}
		const ec::WallSegment* segment = constructionWorld_.getSegment(opening->segment);
		if (segment == nullptr) {
			return ecs::EntityID{0};
		}
		const ec::Vertex* vert0 = constructionWorld_.getVertex(segment->v0);
		const ec::Vertex* vert1 = constructionWorld_.getVertex(segment->v1);
		if (vert0 == nullptr || vert1 == nullptr) {
			return ecs::EntityID{0};
		}

		const auto* type = ConstructionRegistry::Get().getOpeningType(opening->type);
		if (type == nullptr) {
			return ecs::EntityID{0};
		}

		// Centerline world point at parameter t (lerp v0 -> v1, dequantized). This is
		// the opening's gameplay position (haul destination / build site).
		const Foundation::Vec2 a = geometry::dequantize(vert0->pos);
		const Foundation::Vec2 b = geometry::dequantize(vert1->pos);
		const float			   t = opening->t;
		const Foundation::Vec2 center{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};

		auto entity = ecsWorld_->createEntity();
		ecsWorld_->addComponent<ecs::Position>(entity, ecs::Position{{center.x, center.y}});
		ecsWorld_->addComponent<ecs::Structure>(entity, ecs::Structure{ecs::StructureKind::Opening, openingId});

		// Opening cost/work are CONSTANTS per type (not area-derived): costItems and
		// workUnits straight from the type def. Manifest defName is the type's material.
		const auto requiredQty = static_cast<uint32_t>(std::ceil(static_cast<double>(type->costItems)));

		ecs::StructureBlueprint blueprint;
		blueprint.workTotal = type->workUnits;
		if (built) {
			// Mirror spawnWallSegmentEntity's built branch: a complete opening (full
			// work, manifest already delivered) so the construction loop does not
			// re-haul / re-build it.
			blueprint.phase = ecs::StructureBlueprint::BuildPhase::Complete;
			blueprint.workDone = blueprint.workTotal;
			if (requiredQty > 0) {
				blueprint.required.emplace_back(type->material, requiredQty);
				blueprint.delivered.emplace_back(type->material, requiredQty);
			}
		} else {
			// Openings sit on a built wall: no clearing phase (like walls). Start
			// AwaitingMaterials; ConstructionSystem gates them until the host wall is
			// Built, then haul + build proceed.
			blueprint.phase = ecs::StructureBlueprint::BuildPhase::AwaitingMaterials;
			if (requiredQty > 0) {
				blueprint.required.emplace_back(type->material, requiredQty);
			}
		}
		ecsWorld_->addComponent<ecs::StructureBlueprint>(entity, std::move(blueprint));

		ecs::Inventory deliveryInv;
		deliveryInv.maxCapacity = 8;
		deliveryInv.maxStackSize = 100000;
		ecsWorld_->addComponent<ecs::Inventory>(entity, std::move(deliveryInv));

		// Openings are not area-derived, so HP is a small fixed value scaled by the
		// material's per-m^2 hp (a door/window is roughly a square meter of structure).
		const auto* material = ConstructionRegistry::Get().getMaterial(type->material);
		const float maxHp = material != nullptr ? material->hp : 1.0F;
		ecsWorld_->addComponent<ecs::StructureHealth>(entity, ecs::StructureHealth{maxHp, maxHp});

		constructionWorld_.setOpeningEntity(openingId, entity);

		LOG_INFO(
			Game,
			"Opening #%llu spawned: %s (%s) on segment #%llu at t=%.2f, %u materials, %.0f work, entity %u, %s",
			static_cast<unsigned long long>(openingId),
			type->name.c_str(),
			type->material.c_str(),
			static_cast<unsigned long long>(opening->segment),
			static_cast<double>(t),
			requiredQty,
			static_cast<double>(type->workUnits),
			static_cast<uint32_t>(entity),
			built ? "built" : "blueprint"
		);
		return entity;
	}

	DrawingStatus DrawingSystem::status() const {
		DrawingStatus s;
		s.active = (state_ == DrawingState::Drawing);
		s.wall = (activeTool_ == ToolKind::Wall);
		s.opening = (activeTool_ == ToolKind::Opening);
		s.pointCount = static_cast<int>(points_.size());
		s.material = activeMaterial_;

		if (activeTool_ == ToolKind::Opening) {
			s.openingType = activeOpeningType_;
			if (const auto* type = activeOpeningType()) {
				s.openingWidthMeters = type->widthMeters;
				s.material = type->material;
			}
			// Validity reflects the live snap + validateOpening. No built wall in range
			// reads as "no wall to place on"; an in-range wall surfaces the validator's
			// reason (margin / overlap / too short) when it rejects.
			if (!openingSnap_.valid) {
				s.valid = false;
				s.message = "no wall in range";
			} else {
				s.valid = openingValidation_.ok();
				s.message = ec::validationReason(openingValidation_.code);
			}
			return s;
		}

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

		const auto& style = ConstructionRegistry::Get().rendering();
		const auto& fs = style.foundation;
		const auto& ps = style.preview;

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

			// Material color (palette front) drives the progress fill; fall back to the
			// configured fallback color.
			const auto*		  mat = ConstructionRegistry::Get().getMaterial(f.material);
			Foundation::Color matColor = toColor(fs.fallbackColor);
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
				.color = toColor(fs.blueprintFill),
				.id = "committed_foundation_base",
				.zIndex = 50,
			});

			// Layer 2: progress fill, alpha proportional to workDone/workTotal. Ramps from a
			// barely-there tint at 0% to a solid floor at 100% / Built.
			if (progress > 0.0F) {
				const float fillAlpha =
					built ? fs.progressAlphaMax : (fs.progressAlphaMin + (fs.progressAlphaMax - fs.progressAlphaMin) * progress);
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

			// Layer 3: outline. A Built foundation wears a darker shade of its own
			// material; a blueprint keeps the cool outline, firming up toward Built.
			const float				outlineAlpha = fs.outlineAlphaMin + (fs.outlineAlphaMax - fs.outlineAlphaMin) * progress;
			const Foundation::Color outline = built ? darken(matColor, fs.builtEdgeDarken) : toColor(fs.outlineColor, outlineAlpha);
			for (std::size_t i = 0; i < n; ++i) {
				Renderer::Primitives::drawLine({
					.start = screen[i],
					.end = screen[(i + 1) % n],
					.style = {.color = outline, .width = built ? fs.outlineWidthBuilt : fs.outlineWidthBlueprint},
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

		// Committed openings fill the gaps the wall render leaves (above the bands
		// at z 60-62, below the in-progress preview at 900+).
		renderCommittedOpenings(viewportW, viewportH);

		// --- In-progress preview ----------------------------------------------
		if (state_ != DrawingState::Drawing) {
			return;
		}

		// Wall chain preview is a centerline + thickness band, not a closed polygon.
		if (activeTool_ == ToolKind::Wall) {
			renderWallChainPreview(viewportW, viewportH);
			return;
		}

		// Opening tool: a validity-colorized ghost at the snapped position.
		if (activeTool_ == ToolKind::Opening) {
			renderOpeningGhost(viewportW, viewportH);
			return;
		}

		const bool				valid = lastValidation_.ok();
		const Foundation::Color okColor = UI::status_ok;	 // green
		const Foundation::Color badColor = UI::status_crit; // red
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
			Foundation::Color fillColor = valid ? Foundation::Color{okColor.r, okColor.g, okColor.b, ps.fillPreviewAlpha}
												: Foundation::Color{badColor.r, badColor.g, badColor.b, ps.fillPreviewAlpha};
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
				.style = {.color = okColor, .width = ps.lineWidth},
				.id = "drawing_edge",
				.zIndex = 901,
			});
		}

		// Rubber-band from the last placed point to the snapped cursor.
		if (!points_.empty()) {
			Renderer::Primitives::drawLine({
				.start = screen.back(),
				.end = toScreen(cursor_),
				.style = {.color = lineColor, .width = ps.lineWidth},
				.id = "drawing_rubberband",
				.zIndex = 902,
			});
		}

		// Angle-snap guide line (faint) when the cursor is angle-snapped.
		if (lastSnap_.kind == engine::construction::SnapKind::Angle) {
			Renderer::Primitives::drawLine({
				.start = toScreen(lastSnap_.guideFrom),
				.end = toScreen(lastSnap_.guideTo),
				.style = {.color = toColor(ps.guideColor), .width = ps.guideWidth},
				.id = "drawing_guide",
				.zIndex = 899,
			});
		}

		// Vertex dots.
		for (const auto& sp : screen) {
			Renderer::Primitives::drawCircle({
				.center = sp,
				.radius = ps.vertexRadiusPx,
				.style = {.fill = okColor},
				.id = "drawing_vertex",
				.zIndex = 903,
			});
		}

		// Origin halo when the shape can close.
		if (points_.size() >= 3) {
			const float originRadius = ConstructionRegistry::Get().snapping().originCloseRadiusMeters * scale;
			// Closing reflects whatever the next click will do: the explicit origin snap
			// or the near-start "snap not block" rescue.
			const bool	closing = willClose_;
			Renderer::Primitives::drawCircle({
				.center = screen.front(),
				.radius = std::max(ps.originHaloMinRadiusPx, originRadius),
				.style =
					{.fill = {0.0F, 0.0F, 0.0F, 0.0F},
					 .border =
						 Foundation::BorderStyle{.color = closing ? okColor : toColor(ps.originHaloColor), .width = closing ? 3.0F : 1.5F}},
				.id = "drawing_origin_halo",
				.zIndex = 904,
			});
		}

		// Red highlight on the offending edge/vertex when invalid -- but not when the
		// click will close (willClose_: the explicit origin snap, or the near-start
		// rescue of a would-be-blocked vertex). A pending close reads as "closing", not
		// "error".
		if (!valid && !willClose_ && !screen.empty()) {
			const std::size_t vi = std::min(lastValidation_.vertexIndex, screen.size() - 1);
			Renderer::Primitives::drawCircle({
				.center = screen[vi],
				.radius = ps.invalidVertexRadiusPx,
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
			std::span<const Foundation::Vec2> screen,
			Foundation::Color				  fill,
			Foundation::Color				  outline,
			float							  outlineWidth,
			const char*						  fillId,
			const char*						  edgeId,
			int								  zFill,
			int								  zEdge
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

		const auto& ws = ConstructionRegistry::Get().rendering().wall;

		const auto& segs = constructionWorld_.segments();
		if (segs.empty()) {
			return;
		}

		// Palette color for a wall material (drives both the band fill and the built
		// junction fill). Falls back to the configured color when there is no palette.
		auto wallMatColor = [&](const std::string& name) -> Foundation::Color {
			const auto* m = ConstructionRegistry::Get().getMaterial(name);
			if (m != nullptr && !m->pattern.palette.empty()) {
				const auto& c = m->pattern.palette.front();
				return {c.r / 255.0F, c.g / 255.0F, c.b / 255.0F, 1.0F};
			}
			return toColor(ws.fallbackColor);
		};

		auto toScreen = [&](geometry::Vec2i64 mm) -> Foundation::Vec2 {
			const auto w = geometry::dequantize(mm);
			return camera_->worldToScreen(w.x, w.y, viewportW, viewportH, kPixelsPerMeter);
		};

		// Build the offsetter input for the WHOLE graph at once: shared integer
		// vertices mean resolveWallBands derives every junction by exact-endpoint
		// grouping. A malformed segment (missing vertex or unresolved thickness
		// preset) is SKIPPED rather than fed as a zero-length placeholder, because
		// resolveWallBands rejects any zero-length segment up front and would fail
		// the whole graph. offsetToSeg maps each offsetter index (and the segment
		// indices reported back in bands/junctionSegments) to its original segs index.
		std::vector<geometry::WallSegment> offsetSegs;
		std::vector<std::size_t>		   offsetToSeg;
		offsetSegs.reserve(segs.size());
		offsetToSeg.reserve(segs.size());
		for (std::size_t i = 0; i < segs.size(); ++i) {
			const ec::WallSegment& wseg = segs[i];
			const ec::Vertex*	   v0 = constructionWorld_.getVertex(wseg.v0);
			const ec::Vertex*	   v1 = constructionWorld_.getVertex(wseg.v1);
			std::int64_t		   halfThick = 0;
			if (const auto* preset = ConstructionRegistry::Get().getThicknessPreset(wseg.material, wseg.thicknessPreset)) {
				halfThick = preset->halfThicknessMm;
			}
			if (v0 == nullptr || v1 == nullptr || halfThick <= 0) {
				continue;
			}
			offsetSegs.push_back({v0->pos, v1->pos, halfThick});
			offsetToSeg.push_back(i);
		}
		if (offsetSegs.empty()) {
			return;
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
					.style = {.color = toColor(ws.outlineColor, 0.8F), .width = ws.outlineWidthBuilt},
					.id = "committed_wall_fallback",
					.zIndex = 60,
				});
			}
			return;
		}

		// Per-segment bands, styled like the foundation render (z 60-62, above
		// foundations at 50-52, below the in-progress preview at 900+). bands[i]
		// corresponds to offsetSegs[i], i.e. segs[offsetToSeg[i]].
		for (std::size_t i = 0; i < bands.bands.size() && i < offsetToSeg.size(); ++i) {
			const ec::WallSegment& wseg = segs[offsetToSeg[i]];
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

			const Foundation::Color matColor = wallMatColor(wseg.material);

			const float fillAlpha =
				built ? ws.progressAlphaMax : (ws.progressAlphaMin + (ws.progressAlphaMax - ws.progressAlphaMin) * progress);
			// Built walls wear a darker shade of their own material; blueprints keep the
			// cool outline so blue stays the "planned" signal.
			const Foundation::Color outline =
				built ? darken(matColor, ws.builtEdgeDarken)
					  : toColor(ws.outlineColor, ws.outlineAlphaMin + (ws.outlineAlphaMax - ws.outlineAlphaMin) * progress);

			auto drawWallRing = [&](const std::vector<Foundation::Vec2>& screen) {
				// Blueprint base always; progress fill ramps with workDone/workTotal.
				fillRing(screen, toColor(ws.blueprintFill), toColor(ws.outlineColor, 0.0F), 0.0F, "committed_wall_base", "", 60, 60);
				fillRing(
					screen,
					{matColor.r, matColor.g, matColor.b, fillAlpha},
					outline,
					built ? ws.outlineWidthBuilt : ws.outlineWidthBlueprint,
					"committed_wall_progress",
					"committed_wall_edge",
					61,
					62
				);
			};

			// A segment hosting openings can't use the single resolved band: it must
			// show a gap where each opening sits. Replace the band with solid sub-bands
			// over the centerline runs between the gaps, computed from the segment's own
			// v0->v1 centerline. This drops the whole-graph junction trim on this one
			// segment (square cuts at the gap edges, an accepted interim approximation);
			// the junction polygons still fill the corners. Segments with no openings
			// keep the trimmed band unchanged.
			const auto intervals = openingIntervalsForSegment(constructionWorld_, wseg.id);
			if (intervals.empty()) {
				std::vector<Foundation::Vec2> screen;
				screen.reserve(ring.size());
				for (const auto& v : ring) {
					screen.push_back(toScreen(v));
				}
				drawWallRing(screen);
				continue;
			}

			const ec::Vertex* v0 = constructionWorld_.getVertex(wseg.v0);
			const ec::Vertex* v1 = constructionWorld_.getVertex(wseg.v1);
			std::int64_t	  halfThick = 0;
			if (const auto* preset = ConstructionRegistry::Get().getThicknessPreset(wseg.material, wseg.thicknessPreset)) {
				halfThick = preset->halfThicknessMm;
			}
			if (v0 == nullptr || v1 == nullptr || halfThick <= 0) {
				continue;
			}

			// Walk the centerline cutting out each [t0,t1] gap; render a sub-band over
			// each solid run that has positive length.
			auto lerpMm = [&](float t) -> geometry::Vec2i64 {
				const double ax = static_cast<double>(v0->pos.x);
				const double ay = static_cast<double>(v0->pos.y);
				const double bx = static_cast<double>(v1->pos.x);
				const double by = static_cast<double>(v1->pos.y);
				return {
					static_cast<std::int64_t>(std::llround(ax + (bx - ax) * t)),
					static_cast<std::int64_t>(std::llround(ay + (by - ay) * t)),
				};
			};
			float runStart = 0.0F;
			auto  emitRun = [&](float a, float b) {
				 if (b - a <= 1e-4F) {
					 return;
				 }
				 const geometry::Ring sub = geometry::band(lerpMm(a), lerpMm(b), halfThick);
				 if (sub.size() < 3) {
					 return;
				 }
				 std::vector<Foundation::Vec2> screen;
				 screen.reserve(sub.size());
				 for (const auto& v : sub) {
					 screen.push_back(toScreen(v));
				 }
				 drawWallRing(screen);
			};
			for (const auto& iv : intervals) {
				emitRun(runStart, iv.first);
				runStart = iv.second;
			}
			emitRun(runStart, 1.0F);
		}

		// Junction polygons fill the corner gaps the trimmed bands leave. Styling is
		// per junction, from its own incident segments: a junction reads as built only
		// when it has incident segments and every one is built, in which case it takes
		// the wall material color so corners read as continuous material; otherwise it
		// keeps the cool blueprint tint. So a blueprint's corners stay blue even while a
		// finished structure elsewhere shows wood corners. junctionSegments holds
		// offsetter indices, mapped back to segs via offsetToSeg.
		for (std::size_t j = 0; j < bands.junctions.size(); ++j) {
			const geometry::Ring& ring = bands.junctions[j];
			if (ring.size() < 3) {
				continue;
			}

			// Default to blueprint styling; only a fully-built junction with a known
			// incident set flips to built, so a missing/out-of-sync segment list can't
			// masquerade as built.
			bool		junctionBuilt = (j < bands.junctionSegments.size() && !bands.junctionSegments[j].empty());
			std::string junctionMaterial;
			if (j < bands.junctionSegments.size()) {
				for (const std::size_t offIdx : bands.junctionSegments[j]) {
					if (offIdx >= offsetToSeg.size()) {
						continue;
					}
					const ec::WallSegment& iseg = segs[offsetToSeg[offIdx]];
					if (junctionMaterial.empty()) {
						junctionMaterial = iseg.material;
					}
					if (iseg.state != ec::FoundationState::Built) {
						junctionBuilt = false;
					}
				}
			}
			const Foundation::Color jMat = wallMatColor(junctionMaterial);
			const Foundation::Color junctionFill = junctionBuilt ? Foundation::Color{jMat.r, jMat.g, jMat.b, ws.junctionAlphaBuilt}
																 : toColor(ws.junctionColor, ws.junctionAlphaBlueprint);

			std::vector<Foundation::Vec2> screen;
			screen.reserve(ring.size());
			for (const auto& v : ring) {
				screen.push_back(toScreen(v));
			}
			fillRing(screen, junctionFill, toColor(ws.outlineColor, 0.0F), 0.0F, "committed_wall_junction", "", 61, 61);
		}
	}

	namespace {

		// Material color (palette front) or a fallback. Doors fall back to a warm
		// wood-brown, windows to the same so the frame reads as the material; the
		// pane tint is derived in the draw below.
		Foundation::Color openingMaterialColor(const std::string& materialName) {
			const auto* mat = ConstructionRegistry::Get().getMaterial(materialName);
			if (mat != nullptr && !mat->pattern.palette.empty()) {
				const auto& c = mat->pattern.palette.front();
				return {c.r / 255.0F, c.g / 255.0F, c.b / 255.0F, 1.0F};
			}
			return toColor(ConstructionRegistry::Get().rendering().opening.doorFallbackColor);
		}

		// The opening's footprint width in world meters: the long edge of the oriented
		// footprint rectangle. Derived from the actual geometry (not the type's nominal
		// width), so width-based styling like the mullion count matches what's drawn and
		// stays correct even if the footprint were ever clamped near a wall end.
		float footprintWidthMeters(const geometry::Ring& f) {
			if (f.size() < 4) {
				return 0.0F;
			}
			auto edgeLen = [](geometry::Vec2i64 a, geometry::Vec2i64 b) {
				const double dx = static_cast<double>(b.x - a.x);
				const double dy = static_cast<double>(b.y - a.y);
				return std::sqrt(dx * dx + dy * dy);
			};
			return static_cast<float>(std::max(edgeLen(f[0], f[1]), edgeLen(f[0], f[3])) / geometry::kMillimetersPerMeter);
		}

		// Draw a procedural door/window over the opening footprint (the 4 CCW screen-
		// space corners of the oriented wall-thickness rectangle). `alpha` scales the
		// whole fill (build progress / ghost dimming).
		//
		// A parametric local frame makes feature placement orientation-independent:
		// the two rectangle edges from corner 0 are edgeA/edgeB; the longer is the
		// WIDTH axis (along the wall, the opening's clear width), the shorter is the
		// THICKNESS axis (across the wall). pt(u,v) maps u in [0,1] along the width and
		// v in [0,1] across the thickness, so sub-quads/lines are laid out in unit
		// coordinates regardless of how the wall is rotated on screen.
		//
		// Door: a solid material leaf with darkened jamb caps at both width-ends and a
		// center panel seam. Window: a material frame with an inset translucent glass
		// pane crossed by mullion bar(s).
		//
		// `frameColor` is the material color; the green/red ghost reuses this with a
		// validity tint. z: fill at 63, detail (jambs/glass) just above, lines at 64.
		void drawOpeningFill(
			const std::vector<Foundation::Vec2>& screen,
			Foundation::Color					 frameColor,
			bool								 window,
			float								 openingWidthMeters,
			float								 alpha,
			float								 outlineWidth
		) {
			if (screen.size() < 4) {
				return;
			}

			// Local frame from corner 0. edgeA, edgeB are the two perpendicular sides.
			const Foundation::Vec2 corner = screen[0];
			const Foundation::Vec2 edgeA = screen[1] - screen[0];
			const Foundation::Vec2 edgeB = screen[3] - screen[0];

			// Width axis = longer edge (along the wall); thickness axis = shorter. Anchor
			// the origin so pt(0,0) sits at a corner and pt(1,1) at the diagonal one.
			const bool			   aIsWidth = glm::dot(edgeA, edgeA) >= glm::dot(edgeB, edgeB);
			const Foundation::Vec2 widthVec = aIsWidth ? edgeA : edgeB;
			const Foundation::Vec2 thickVec = aIsWidth ? edgeB : edgeA;
			auto				   pt = [&](float u, float v) -> Foundation::Vec2 {
				  return corner + widthVec * u + thickVec * v;
			};
			auto quad = [&](float u0, float u1, float v0, float v1) -> std::array<Foundation::Vec2, 4> {
				return {pt(u0, v0), pt(u1, v0), pt(u1, v1), pt(u0, v1)};
			};

			const auto&				os = ConstructionRegistry::Get().rendering().opening;
			const Foundation::Color outline = darken(frameColor, os.outlineDarken);

			if (window) {
				// Frame: the whole footprint in material color, darkened outline.
				fillRing(
					screen,
					{frameColor.r, frameColor.g, frameColor.b, os.fillAlpha * alpha},
					{outline.r, outline.g, outline.b, os.outlineAlpha * alpha},
					outlineWidth,
					"committed_opening_frame",
					"committed_opening_frame_edge",
					63,
					64
				);

				// Glass: inset pane, translucent cyan-blue glazing. Inset along the width
				// by the jamb width, across the thickness by kGlassInset.
				const float				kJamb = os.jambWidth;
				const float				kGlassInset = os.glassInset;
				const Foundation::Color glass{os.glassColor.r, os.glassColor.g, os.glassColor.b, os.glassColor.a * alpha};
				fillRing(
					quad(kJamb, 1.0F - kJamb, kGlassInset, 1.0F - kGlassInset),
					glass,
					{glass.r, glass.g, glass.b, 0.0F},
					0.0F,
					"committed_opening_glass",
					"",
					64,
					64
				);

				// Mullion(s): thin material bars across the glass along the thickness axis.
				// Count scales with the window's real width in METERS (not screen pixels),
				// so the pane count is stable across zoom: roughly one mullion per 0.7 m.
				const int				mullions = std::max(1, static_cast<int>(openingWidthMeters / os.mullionSpacingMeters + 0.5F));
				const Foundation::Color mullionColor{frameColor.r, frameColor.g, frameColor.b, os.mullionAlpha * alpha};
				for (int i = 1; i <= mullions; ++i) {
					const float u = kJamb + (1.0F - 2.0F * kJamb) * static_cast<float>(i) / static_cast<float>(mullions + 1);
					Renderer::Primitives::drawLine({
						.start = pt(u, kGlassInset),
						.end = pt(u, 1.0F - kGlassInset),
						.style = {.color = mullionColor, .width = std::max(1.0F, outlineWidth)},
						.id = "committed_opening_mullion",
						.zIndex = 64,
					});
				}
			} else {
				// Leaf: the whole footprint, opaque material color (the door shares the
				// wall material, so jambs + seam are what distinguish it).
				fillRing(
					screen,
					{frameColor.r, frameColor.g, frameColor.b, os.fillAlpha * alpha},
					{outline.r, outline.g, outline.b, os.outlineAlpha * alpha},
					outlineWidth,
					"committed_opening_leaf",
					"committed_opening_leaf_edge",
					63,
					64
				);

				// Jamb caps: a darkened sub-quad at each width-end, full thickness. These
				// frame the leaf and read as the door's hinge/strike jambs.
				const float				kJamb = os.jambWidth;
				const Foundation::Color jambBase = darken(frameColor, os.jambDarken);
				const Foundation::Color jamb{jambBase.r, jambBase.g, jambBase.b, os.jambAlpha * alpha};
				fillRing(quad(0.0F, kJamb, 0.0F, 1.0F), jamb, {jamb.r, jamb.g, jamb.b, 0.0F}, 0.0F, "committed_opening_jamb", "", 64, 64);
				fillRing(
					quad(1.0F - kJamb, 1.0F, 0.0F, 1.0F), jamb, {jamb.r, jamb.g, jamb.b, 0.0F}, 0.0F, "committed_opening_jamb", "", 64, 64
				);

				// Center seam: one darkened line across the thickness at u=0.5, the door
				// panel split.
				const Foundation::Color seam{jamb.r, jamb.g, jamb.b, os.jambAlpha * alpha};
				Renderer::Primitives::drawLine({
					.start = pt(0.5F, 0.0F),
					.end = pt(0.5F, 1.0F),
					.style = {.color = seam, .width = std::max(1.0F, outlineWidth)},
					.id = "committed_opening_seam",
					.zIndex = 64,
				});
			}
		}

	} // namespace

	void DrawingSystem::renderCommittedOpenings(int viewportW, int viewportH) {
		const auto& ops = constructionWorld_.openings();
		if (ops.empty()) {
			return;
		}

		const auto& os = ConstructionRegistry::Get().rendering().opening;

		auto toScreen = [&](geometry::Vec2i64 mm) -> Foundation::Vec2 {
			const auto w = geometry::dequantize(mm);
			return camera_->worldToScreen(w.x, w.y, viewportW, viewportH, kPixelsPerMeter);
		};

		for (const ec::Opening& op : ops) {
			const geometry::Ring footprint = openingFootprint(constructionWorld_, op);
			if (footprint.size() < 3) {
				continue;
			}

			const auto* type = ConstructionRegistry::Get().getOpeningType(op.type);
			if (type == nullptr) {
				continue;
			}
			const bool window = !type->pathable; // windows are not pathable; doors are

			// Progress styling mirrors walls: a Built opening renders solid, a blueprint
			// dims with workDone/workTotal from its ECS mirror (falling back to the
			// topology state when there's no entity yet).
			const bool built = (op.state == ec::FoundationState::Built);
			float	   progress = built ? 1.0F : 0.0F;
			if (!built && ecsWorld_ != nullptr && op.entity != ecs::kInvalidEntity) {
				if (const auto* bp = ecsWorld_->getComponent<ecs::StructureBlueprint>(op.entity)) {
					progress = bp->progress();
				}
			}
			const float alpha = built ? 1.0F : (os.progressAlphaMin + (os.progressAlphaMax - os.progressAlphaMin) * progress);

			std::vector<Foundation::Vec2> screen;
			screen.reserve(footprint.size());
			for (const auto& v : footprint) {
				screen.push_back(toScreen(v));
			}
			drawOpeningFill(
				screen,
				openingMaterialColor(type->material),
				window,
				footprintWidthMeters(footprint),
				alpha,
				built ? os.outlineWidthBuilt : os.outlineWidthBlueprint
			);
		}
	}

	void DrawingSystem::renderOpeningGhost(int viewportW, int viewportH) {
		const auto* type = activeOpeningType();
		if (type == nullptr || !openingSnap_.valid) {
			return; // no wall in range: show nothing
		}

		auto toScreen = [&](geometry::Vec2i64 mm) -> Foundation::Vec2 {
			const auto w = geometry::dequantize(mm);
			return camera_->worldToScreen(w.x, w.y, viewportW, viewportH, kPixelsPerMeter);
		};

		// Build a transient Opening at the snapped segment/t to reuse the shared
		// footprint geometry; it is never committed.
		ec::Opening ghost;
		ghost.segment = openingSnap_.segment;
		ghost.t = openingSnap_.t;
		ghost.type = type->name;
		ghost.material = type->material;
		const geometry::Ring footprint = openingFootprint(constructionWorld_, ghost);
		if (footprint.size() < 3) {
			return;
		}

		const bool				valid = openingValidation_.ok();
		const Foundation::Color okColor = UI::status_ok;	 // green
		const Foundation::Color badColor = UI::status_crit; // red
		const Foundation::Color tint = valid ? okColor : badColor;

		std::vector<Foundation::Vec2> screen;
		screen.reserve(footprint.size());
		for (const auto& v : footprint) {
			screen.push_back(toScreen(v));
		}
		// Validity-colorized ghost: the same procedural door/window shape as the
		// committed render, drawn with the green/red validity tint in place of the
		// material color. Half alpha so it reads as a translucent preview over the wall
		// and ground rather than an opaque structure.
		const bool	window = !type->pathable;
		const auto& os = ConstructionRegistry::Get().rendering().opening;
		drawOpeningFill(screen, tint, window, footprintWidthMeters(footprint), os.ghostAlpha, os.outlineWidthBuilt);
	}

	void DrawingSystem::renderWallChainPreview(int viewportW, int viewportH) {
		const auto& ps = ConstructionRegistry::Get().rendering().preview;

		const float scale = kPixelsPerMeter * camera_->zoom();

		auto toScreen = [&](Foundation::Vec2 w) -> Foundation::Vec2 {
			return camera_->worldToScreen(w.x, w.y, viewportW, viewportH, kPixelsPerMeter);
		};

		const bool				valid = lastValidation_.ok();
		const Foundation::Color okColor = UI::status_ok;	 // green
		const Foundation::Color badColor = UI::status_crit; // red

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

			const Foundation::Color bandColor{okColor.r, okColor.g, okColor.b, ps.bandPreviewAlpha};
			for (std::size_t i = 0; i + 1 < points_.size(); ++i) {
				drawBand(points_[i], points_[i + 1], bandColor);
			}
			if (!points_.empty()) {
				const Foundation::Color rb = valid ? Foundation::Color{okColor.r, okColor.g, okColor.b, ps.bandPreviewAlpha}
												   : Foundation::Color{badColor.r, badColor.g, badColor.b, ps.bandPreviewAlpha};
				drawBand(points_.back(), cursor_, rb);
			}
		}

		// Centerlines between committed points.
		for (std::size_t i = 0; i + 1 < screen.size(); ++i) {
			Renderer::Primitives::drawLine({
				.start = screen[i],
				.end = screen[i + 1],
				.style = {.color = okColor, .width = ps.lineWidth},
				.id = "wall_centerline",
				.zIndex = 907,
			});
		}

		// Rubber-band centerline from the last point to the snapped cursor.
		if (!points_.empty()) {
			Renderer::Primitives::drawLine({
				.start = screen.back(),
				.end = toScreen(cursor_),
				.style = {.color = valid ? okColor : badColor, .width = ps.lineWidth},
				.id = "wall_rubberband",
				.zIndex = 908,
			});
		}

		// Angle-snap guide line (faint) when the cursor is angle-snapped.
		if (lastSnap_.kind == engine::construction::SnapKind::Angle) {
			Renderer::Primitives::drawLine({
				.start = toScreen(lastSnap_.guideFrom),
				.end = toScreen(lastSnap_.guideTo),
				.style = {.color = toColor(ps.guideColor), .width = ps.guideWidth},
				.id = "wall_guide",
				.zIndex = 905,
			});
		}

		// Vertex dots, plus a snap-target ring colored by the snap kind so the player
		// sees endpoint / T-junction / foundation-vertex snaps.
		for (const auto& sp : screen) {
			Renderer::Primitives::drawCircle({
				.center = sp,
				.radius = ps.vertexRadiusPx,
				.style = {.fill = okColor},
				.id = "wall_vertex",
				.zIndex = 909,
			});
		}

		using engine::construction::SnapKind;
		if (lastSnap_.kind == SnapKind::WallEndpoint || lastSnap_.kind == SnapKind::WallSegment || lastSnap_.kind == SnapKind::Vertex ||
			lastSnap_.kind == SnapKind::Edge) {
			const Foundation::Color snapColor = (lastSnap_.kind == SnapKind::WallEndpoint || lastSnap_.kind == SnapKind::Vertex)
													? toColor(ps.snapVertexColor) // vertex snap: amber
													: toColor(ps.snapEdgeColor);  // edge / T-junction: cyan
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

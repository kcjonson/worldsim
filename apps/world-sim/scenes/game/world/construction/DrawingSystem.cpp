#include "DrawingSystem.h"

#include "UserSettings.h"

#include <assets/ConstructionRegistry.h>
#include <construction/OpeningGeometry.h>
#include <construction/RectangleSnap.h>
#include <ecs/components/Structure.h>
#include <ecs/components/StructureBlueprint.h>
#include <ecs/components/StructureHealth.h>
#include <ecs/components/Transform.h>
#include <ecs/systems/ConstructionSystem.h>
#include <ecs/systems/NavigationSystem.h>
#include <ecs/systems/PlayerVisibilitySystem.h>
#include <offset/WallOffset.h>
#include <predicates/Predicates.h>
#include <primitives/Primitives.h>
#include <utils/Log.h>
#include <vector/Tessellator.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>

namespace world_sim {

	namespace {

		using assets::ConstructionRegistry;
		using assets::StyleColor;
		namespace ec = construction;

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

		std::vector<Foundation::Vec2> dequantizeRing(const geometry::Ring& ring) {
			std::vector<Foundation::Vec2> pts;
			pts.reserve(ring.size());
			for (const auto& v : ring) {
				pts.push_back(geometry::dequantize(v));
			}
			return pts;
		}

		// A foundation entity's Position (world meters): the build, haul, and
		// deconstruct goals all target it, so it must lie on the footprint, which the
		// area centroid of a concave outline (a U's notch) does not.
		Foundation::Vec2 sitePosition(const geometry::Ring& ring) {
			const geometry::Vec2d mm = geometry::interiorPoint(ring);
			const double		  mmPerM = static_cast<double>(geometry::kMillimetersPerMeter);
			return {static_cast<float>(mm.x / mmPerM), static_cast<float>(mm.y / mmPerM)};
		}

		geometry::Ring toRingMm(const std::vector<Foundation::Vec2>& pts) {
			geometry::Ring ring;
			ring.reserve(pts.size());
			for (const auto& p : pts) {
				ring.push_back(geometry::quantize(p));
			}
			return ring;
		}

		// Area-driven foundation sizing, the one formula for spawn and in-place
		// resize. Invariant: the manifest defName IS the material name; construction
		// config keys materials by name and the haul chain resolves items by that
		// same name, so the two stay aligned by design.
		struct FoundationSizing {
			uint32_t requiredQty = 0;
			float	 workTotal = 0.0F;
			float	 maxHp = 0.0F;
		};

		FoundationSizing sizeFoundation(float areaSquareMeters, const std::string& materialName) {
			FoundationSizing sizing;
			const auto*		 material = ConstructionRegistry::Get().getMaterial(materialName);
			if (material == nullptr) {
				return sizing;
			}
			sizing.requiredQty = static_cast<uint32_t>(
				std::ceil(static_cast<double>(areaSquareMeters) * static_cast<double>(material->costRatePerSquareMeter))
			);
			sizing.workTotal = areaSquareMeters * material->workRatePerSquareMeter;
			sizing.maxHp = areaSquareMeters * material->hp;
			return sizing;
		}

		// Fill a screen-space polygon of any simple shape (concave included) through
		// the renderer tessellator, the same triangulation the committed foundation
		// cache and the rooms overlay use.
		void fillPolygon(std::span<const Foundation::Vec2> screen, Foundation::Color color, const char* id, int zIndex) {
			// Scratch mesh reused across calls (several per visible ring per frame);
			// drawTriangles copies before returning.
			static thread_local renderer::TessellatedMesh mesh;
			renderer::Tessellator						  tessellator;
			if (!tessellator.Tessellate(renderer::VectorPath{std::vector<Foundation::Vec2>(screen.begin(), screen.end()), true}, mesh) ||
				mesh.indices.size() < 3) {
				return;
			}
			Renderer::Primitives::drawTriangles({
				.vertices = mesh.vertices.data(),
				.indices = mesh.indices.data(),
				.vertexCount = mesh.vertices.size(),
				.indexCount = mesh.indices.size(),
				.color = color,
				.id = id,
				.key = zIndex,
			});
		}

		// Chains and openings are checked as a strip this wide along each segment, and a segment shorter
		// than kChainSeenMinLengthMeters is its end points alone.
		constexpr float kChainSeenHalfWidthMeters = 0.05F;
		constexpr float kChainSeenMinLengthMeters = 0.001F;

		// A closed foundation, or an Add / Subtract region, needs this many points.
		constexpr std::size_t kMinClosingShapePoints = 3;

		// A rejected edit's reason: its text scale, its top-left off the snapped point, and
		// its shadow's offset. The box the labels keep off covers the shadow too.
		constexpr float			   kEditReasonScale = 0.8F;
		constexpr Foundation::Vec2 kEditReasonOffset{14.0F, -22.0F};
		constexpr float			   kEditReasonShadowPx = 1.0F;

		// Add `from`'s quantities into `into`, per defName.
		void addManifest(std::vector<std::pair<std::string, uint32_t>>& into, const std::vector<std::pair<std::string, uint32_t>>& from) {
			for (const auto& [defName, qty] : from) {
				auto it = std::find_if(into.begin(), into.end(), [&defName](const auto& entry) { return entry.first == defName; });
				if (it != into.end()) {
					it->second += qty;
				} else {
					into.emplace_back(defName, qty);
				}
			}
		}

	} // namespace

	DrawingSystem::DrawingSystem(const Args& args)
		: ecsWorld_(args.world),
		  validColor_(args.validColor),
		  invalidColor_(args.invalidColor),
		  accentColor_(args.accentColor),
		  camera_(args.camera),
		  navigation_(args.navigation),
		  callbacks_(args.callbacks) {}

	const ecs::PlayerVisibilitySystem* DrawingSystem::visibilityGate() const {
		return ecsWorld_ != nullptr ? ecs::PlayerVisibilitySystem::gate(*ecsWorld_) : nullptr;
	}

	bool DrawingSystem::pointSeen(Foundation::Vec2 p) const {
		const ecs::PlayerVisibilitySystem* visibility = visibilityGate();
		return visibility == nullptr || visibility->isSeen({p.x, p.y});
	}

	bool DrawingSystem::areaSeen(const std::vector<Foundation::Vec2>& pts) const {
		const ecs::PlayerVisibilitySystem* visibility = visibilityGate();
		return visibility == nullptr || visibility->covers(Foundation::toGlmVec2(pts));
	}

	bool DrawingSystem::chainSeen(const std::vector<Foundation::Vec2>& pts) const {
		const ecs::PlayerVisibilitySystem* visibility = visibilityGate();
		if (visibility == nullptr) {
			return true;
		}
		for (const Foundation::Vec2 point : pts) {
			if (!visibility->isSeen({point.x, point.y})) {
				return false;
			}
		}
		for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
			const glm::vec2 from{pts[i].x, pts[i].y};
			const glm::vec2 to{pts[i + 1].x, pts[i + 1].y};
			const float		length = glm::distance(from, to);
			if (length < kChainSeenMinLengthMeters) {
				continue;
			}
			const glm::vec2 side = glm::vec2{from.y - to.y, to.x - from.x} / length * kChainSeenHalfWidthMeters;
			if (!visibility->covers({from + side, to + side, to - side, from - side})) {
				return false;
			}
		}
		return true;
	}

	bool DrawingSystem::openingSeen(float widthMeters) const {
		const auto* segment = constructionWorld_.getSegment(openingSnap_.segment);
		const auto* start = segment != nullptr ? constructionWorld_.getVertex(segment->v0) : nullptr;
		const auto* end = segment != nullptr ? constructionWorld_.getVertex(segment->v1) : nullptr;
		if (start == nullptr || end == nullptr) {
			return true;
		}
		const Foundation::Vec2 from = geometry::dequantize(start->pos);
		const Foundation::Vec2 to = geometry::dequantize(end->pos);
		const float			   length = std::hypot(to.x - from.x, to.y - from.y);
		if (length < kChainSeenMinLengthMeters) {
			return pointSeen(cursor_);
		}
		const float alongX = (to.x - from.x) / length * widthMeters * 0.5F;
		const float alongY = (to.y - from.y) / length * widthMeters * 0.5F;
		return chainSeen({{cursor_.x - alongX, cursor_.y - alongY}, {cursor_.x + alongX, cursor_.y + alongY}});
	}

	std::string DrawingSystem::cursorGroundReason() const {
		return pointSeen(cursor_) ? std::string("not on buildable ground") : std::string("the area is unexplored");
	}

	bool DrawingSystem::pointOnBuildableGround(Foundation::Vec2 p) const {
		if (!pointSeen(p)) {
			return false;
		}
		// Per-vertex / cursor placeability for the ACTIVE tool. No nav system wired (headless/test) ->
		// validity is owned elsewhere; mirror the dev verbs' permissive fallback. A FOUNDATION vertex
		// may sit over a clearable entity (tree/rock) -- those become clear tasks -- so it uses the
		// terrain-only buildability check, agreeing with the commit gate (isAreaBuildableOnceCleared). A WALL
		// vertex needs clear on-mesh ground out of any ford (no wall footprint-clearing yet).
		if (navigation_ == nullptr) {
			return true;
		}
		// A Subtract region is a cutter: it may reach over water, it never builds there.
		// An Add vertex on or inside the target builds nothing new, matching the
		// commit gate, which checks only the ground the Add adds (buildsOn).
		if (activeTool_ == ToolKind::FoundationEdit) {
			if (editMode_ == ec::FoundationEditMode::Subtract) {
				return true;
			}
			const auto* target = constructionWorld_.get(editTarget_);
			if (target != nullptr && geometry::pointInPolygon(geometry::quantize(p), target->ring) != geometry::PointInPolygon::Outside) {
				return true;
			}
		}
		const bool foundationTool = activeTool_ == ToolKind::Foundation || activeTool_ == ToolKind::FoundationRectangle ||
									activeTool_ == ToolKind::FoundationEdit;
		return foundationTool ? navigation_->isPointBuildableOnceCleared({p.x, p.y}) : navigation_->isPointBuildableNow({p.x, p.y});
	}

	bool DrawingSystem::footprintBuildable(const std::vector<Foundation::Vec2>& pts) const {
		return navigation_ == nullptr || navigation_->isAreaBuildableOnceCleared(Foundation::toGlmVec2(pts));
	}

	std::string DrawingSystem::placementRefusal(const std::vector<Foundation::Vec2>& footprint) const {
		// Placement gate (the SAME check the /api/dev verbs use). A FOUNDATION validates against
		// BUILDABILITY (terrain + geometry, no flora). Walls have no footprint-clearing yet, so they
		// still require clear on-mesh ground along the chain centerline. One blocked part refuses the
		// whole placement (commit nothing).
		const bool wall = activeTool_ == ToolKind::Wall;
		if (!(wall ? chainSeen(footprint) : areaSeen(footprint))) {
			return "the area is unexplored";
		}
		if (navigation_ != nullptr &&
			!(wall ? navigation_->isLineBuildableNow(Foundation::toGlmVec2(footprint)) : footprintBuildable(footprint))) {
			return "not on buildable ground";
		}
		return {};
	}

	bool DrawingSystem::requirePlaceable(const std::vector<Foundation::Vec2>& footprint, const char* what) {
		const std::string refusal = placementRefusal(footprint);
		if (refusal.empty()) {
			return true;
		}
		if (callbacks_.onToast) {
			callbacks_.onToast("Can't build here", std::string(what) + ": " + refusal);
		}
		return false;
	}

	void DrawingSystem::startTool(ToolKind tool) {
		state_ = DrawingState::Drawing;
		activeTool_ = tool;
		points_.clear();
		lastSnap_ = {};
		lastValidation_ = {};
		willClose_ = false;
		pendingClose_ = {};
		cursorOffBuildableGround = false;
		wallHost_ = ec::kInvalidFoundation;
		clearRectangle();
		rectangleSnapKind_ = construction::RectangleSnapKind::None;
		if (callbacks_.onToolActive) {
			callbacks_.onToolActive(true);
		}
	}

	void DrawingSystem::activateFoundationTool() {
		startTool(ToolKind::Foundation);
		lastFoundationTool_ = FoundationTool::Freeform;
		LOG_INFO(Game, "Freeform foundation tool activated (material=%s)", activeMaterial_.c_str());
	}

	void DrawingSystem::activateRectangleTool() {
		startTool(ToolKind::FoundationRectangle);
		lastFoundationTool_ = FoundationTool::Rectangle;
		LOG_INFO(Game, "Rectangle foundation tool activated (material=%s)", activeMaterial_.c_str());
	}

	void DrawingSystem::pickFoundationTool(FoundationTool tool) {
		if (activeFoundationTool() == tool) {
			return;
		}
		if (tool == FoundationTool::Rectangle) {
			activateRectangleTool();
		} else {
			activateFoundationTool();
		}
	}

	std::optional<FoundationTool> DrawingSystem::activeFoundationTool() const {
		if (!isActive()) {
			return std::nullopt;
		}
		switch (activeTool_) {
			case ToolKind::FoundationRectangle:
				return FoundationTool::Rectangle;
			case ToolKind::Foundation:
				return FoundationTool::Freeform;
			case ToolKind::FoundationEdit:
			case ToolKind::Wall:
			case ToolKind::Opening:
				return std::nullopt;
		}
		return std::nullopt;
	}

	void DrawingSystem::activateWallTool() {
		startTool(ToolKind::Wall);
		LOG_INFO(Game, "Wall tool activated (material=%s, preset=%s)", activeMaterial_.c_str(), activeThicknessPreset_.c_str());
	}

	void DrawingSystem::activateOpeningTool(const std::string& openingType) {
		activeOpeningType_ = openingType;
		openingSnap_ = {};
		openingValidation_ = {};
		startTool(ToolKind::Opening);
		LOG_INFO(Game, "Opening tool activated (type=%s)", activeOpeningType_.c_str());
	}

	void DrawingSystem::activateFoundationEditTool(ec::FoundationId target, ec::FoundationEditMode mode) {
		if (constructionWorld_.get(target) == nullptr) {
			return;
		}
		editTarget_ = target;
		editMode_ = mode;
		editPreview_ = {};
		editPreviewActive_ = false;
		startTool(ToolKind::FoundationEdit);
		LOG_INFO(
			Game,
			"Foundation %s tool activated on #%llu",
			mode == ec::FoundationEditMode::Add ? "Add" : "Subtract",
			static_cast<unsigned long long>(target)
		);
	}

	void DrawingSystem::deactivate() {
		state_ = DrawingState::Idle;
		points_.clear();
		lastSnap_ = {};
		lastValidation_ = {};
		willClose_ = false;
		pendingClose_ = {};
		clearRectangle();
		cursorOffBuildableGround = false;
		wallHost_ = ec::kInvalidFoundation;
		openingSnap_ = {};
		openingValidation_ = {};
		editTarget_ = ec::kInvalidFoundation;
		editPreview_ = {};
		editPreviewActive_ = false;
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
		if (activeTool_ == ToolKind::FoundationRectangle) {
			handleRectangleMove({world.x, world.y}, freeform);
			return;
		}
		const bool editing = activeTool_ == ToolKind::FoundationEdit;
		if (editing && constructionWorld_.get(editTarget_) == nullptr) {
			deactivate(); // the target was demolished or merged away under the tool
			return;
		}

		auto&			  registry = ConstructionRegistry::Get();
		const SnapMemoKey key = snapMemoKey({world.x, world.y}, freeform, nullptr);
		if (!reuseSnapMemo(key)) {
			const ec::SnapEngine snap(registry.snapping(), constructionWorld_);
			snapMemoResult_ = snap.snap(
				points_,
				key.cursorWorld,
				ec::SnapInput{.aids = key.aids, .freeform = freeform, .originCloseRadiusMeters = effectiveOriginCloseRadiusMeters()}
			);
			const ec::ConstructionValidator validator(registry.constraints(), constructionWorld_);
			snapMemoValidation_ = editing ? validator.validateEditPoint(points_, snapMemoResult_.point)
										  : validator.validatePoint(points_, snapMemoResult_.point);
		}
		lastSnap_ = snapMemoResult_;
		cursor_ = lastSnap_.point;
		lastValidation_ = snapMemoValidation_;

		// Live nav feedback: is the snapped cursor itself off buildable ground? Drives the
		// red preview (see render). The commit gate re-checks the whole footprint regardless.
		cursorOffBuildableGround = !pointOnBuildableGround(cursor_);

		// "Snap not block": with a closeable shape, a click near the start that would
		// otherwise be rejected closes instead of erroring -- but only when the closed
		// shape is itself valid (never rescue-close into a bad foundation). The explicit
		// origin-close is the SnapKind::Origin case and needs no validity check. A
		// foundation's closed ring is the check kept with the snap; an edit's outline reads
		// the target's blueprint, so it is validated on each move, once: the edit preview
		// takes this result rather than validating the same polygon again.
		willClose_ = false;
		std::optional<construction::FoundationEditResult> closedEdit;
		if (points_.size() >= kMinClosingShapePoints) {
			if (lastSnap_.closesShape()) {
				willClose_ = true;
			} else if (!lastValidation_.ok() && nearStartVertex(cursor_, closeRescueRadiusMeters())) {
				if (editing) {
					closedEdit = validateDrawnEdit(points_);
					willClose_ = closedEdit->ok();
				} else {
					willClose_ = closedShapeCheck().accepted;
				}
			}
		}
		if (editing) {
			refreshEditPreview(std::move(closedEdit));
		}
		pendingClose_ = willClose_ ? closeVerdict() : CloseVerdict{};
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
		if (activeTool_ == ToolKind::FoundationRectangle) {
			if (!rectanglePress_) {
				return true; // its press landed on the interface: the release does nothing
			}
			const Foundation::Vec2 press = *rectanglePress_;
			const bool			   pressPlacedCorner = rectanglePressPlacedCorner_;
			rectanglePress_.reset();
			const float dragPx = std::hypot(screenX - press.x, screenY - press.y);
			if (pressPlacedCorner && dragPx <= ConstructionRegistry::Get().snapping().doubleClickDistancePx) {
				return true; // a click, not a drag: the first corner waits for the second click
			}
			if (!rectangleCorners_.empty()) {
				commitShape();
			}
			return true;
		}

		// Close the shape (>= 3 points) when the click resolves to a close: the explicit
		// origin-close, or the near-start "snap not block" rescue. willClose_ was set by
		// handleMouseMove (called above) and already requires a valid closed ring for the
		// rescue case, so this never closes into an invalid foundation.
		if (points_.size() >= kMinClosingShapePoints && willClose_) {
			if (activeTool_ == ToolKind::FoundationEdit) {
				commitFoundationEdit();
			} else {
				commitShape();
			}
			return true;
		}

		if (!clickVerdict().proposed) {
			return true;
		}

		// Reject the point if it would violate a hard per-vertex constraint.
		if (!lastValidation_.ok()) {
			if (callbacks_.onToast) {
				callbacks_.onToast("Invalid point", ec::validationReason(lastValidation_.code));
			}
			return true; // consumed: the click was a deliberate (rejected) action
		}

		// Nav gate: refuse a vertex that lands on genuinely unbuildable ground (water or off-mesh).
		// A vertex over a clearable entity (tree/rock) is allowed -- pointOnBuildableGround routes foundations
		// through the terrain-only buildability check. The whole-footprint re-check on commit
		// (requirePlaceable) still catches an edge or interior that crosses water between two vertices.
		if (cursorOffBuildableGround) {
			if (callbacks_.onToast) {
				callbacks_.onToast("Can't build here", "foundation: " + cursorGroundReason());
			}
			return true;
		}

		points_.push_back(cursor_);
		lastPlacement_ = PointPlacement{.pointer = snapMemoKey_.cursorWorld, .pointCount = points_.size()};
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
			willClose_ = false;
			pendingClose_ = {};
			editPreviewActive_ = false;
			clearRectangle();
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
		willClose_ = false;		// recomputed on the next move; don't leave a stale closing halo
		cursorOffBuildableGround = false; // recomputed on the next move
		editPreviewActive_ = false;
		pendingClose_ = {};
		if (points_.empty()) {
			// The chain's host is determined by its first point; once empty, the next
			// first click re-picks it. A rectangle's first corner was its only point.
			wallHost_ = ec::kInvalidFoundation;
			clearRectangle();
		}
		return true;
	}

	void DrawingSystem::clearRectangle() {
		rectangleCorners_.clear();
		rectangleLabel_.clear();
		rectanglePress_.reset();
		rectanglePressPlacedCorner_ = false;
	}

	bool DrawingSystem::handlePress(float screenX, float screenY, int viewportW, int viewportH, bool freeform) {
		if (!isRectangleTool() || camera_ == nullptr) {
			return false;
		}
		handleMouseMove(screenX, screenY, viewportW, viewportH, freeform);
		rectanglePress_ = Foundation::Vec2{screenX, screenY};
		rectanglePressPlacedCorner_ = false;
		if (!points_.empty()) {
			return true; // a corner is waiting: this press's release places the opposite one
		}
		if (cursorOffBuildableGround) {
			rectanglePress_.reset();
			if (callbacks_.onToast) {
				callbacks_.onToast("Can't build here", "foundation: " + cursorGroundReason());
			}
			return true;
		}
		points_.push_back(cursor_);
		rectanglePressPlacedCorner_ = true;
		handleMouseMove(screenX, screenY, viewportW, viewportH, freeform);
		return true;
	}

	void DrawingSystem::cancelPress() {
		rectanglePress_.reset();
		rectanglePressPlacedCorner_ = false;
	}

	void DrawingSystem::handleRectangleMove(Foundation::Vec2 world, bool freeform) {
		auto&			  registry = ConstructionRegistry::Get();
		const SnapMemoKey key = snapMemoKey(world, freeform, nullptr);
		if (!reuseSnapMemo(key)) {
			const construction::RectangleCornerSnap corner =
				construction::snapRectangleCorner(constructionWorld_, registry.snapping(), key.aids, world, freeform);
			snapMemoResult_ = construction::SnapResult{.point = corner.point};
			snapMemoRectangleSnapKind_ = corner.kind;
			snapMemoValidation_ = {};
			snapMemoRectangleCorners_.clear();
			snapMemoRectangleLabel_.clear();
			if (!points_.empty()) {
				if (const std::optional<construction::RectangleShape> rectangle = construction::rectangleFromCorners(points_.front(), corner.point)) {
					snapMemoRectangleCorners_ = dequantizeRing(rectangle->ring);
					snapMemoRectangleLabel_ = rectangle->label;
				}
			}
		}
		lastSnap_ = snapMemoResult_;
		rectangleSnapKind_ = snapMemoRectangleSnapKind_;
		cursor_ = lastSnap_.point;
		lastValidation_ = snapMemoValidation_;
		cursorOffBuildableGround = !pointOnBuildableGround(cursor_);

		rectangleCorners_ = snapMemoRectangleCorners_;
		rectangleLabel_ = snapMemoRectangleLabel_;
		willClose_ = !rectangleCorners_.empty();
		pendingClose_ = willClose_ ? closeVerdict() : CloseVerdict{};
	}

	float DrawingSystem::pixelsPerWorldMeter() const {
		const float zoom = (camera_ != nullptr) ? camera_->zoom() : 1.0F;
		return kPixelsPerMeter * (zoom > 0.0F ? zoom : 1.0F);
	}

	float DrawingSystem::effectiveOriginCloseRadiusMeters() const {
		auto&		registry = ConstructionRegistry::Get();
		const float configMeters = registry.snapping().originCloseRadiusMeters;
		// Floor to a screen-px minimum so the catch radius stays reachable at every zoom.
		// The dotted close ring is drawn at this radius, so it shows the catch exactly;
		// the closing halo has its own floor (DrawingFeedback).
		const float floorPx = registry.rendering().preview.originHaloMinRadiusPx;
		const float scale = pixelsPerWorldMeter();
		const float floorMeters = (scale > 0.0F) ? floorPx / scale : 0.0F;
		return std::max(configMeters, floorMeters);
	}

	DrawingSystem::SnapMemoKey
	DrawingSystem::snapMemoKey(Foundation::Vec2 cursorWorld, bool freeform, const assets::ThicknessPreset* preset) const {
		return SnapMemoKey{
			.tool = activeTool_,
			.cursorWorld = cursorWorld,
			.aids = UserSettings::Get().drawingAids(),
			.worldVersion = constructionWorld_.version(),
			.freeform = freeform,
			.zoom = camera_ != nullptr ? camera_->zoom() : 0.0F,
			.preset = preset,
			.wallHost = wallHost_,
			.editTarget = editTarget_,
		};
	}

	bool DrawingSystem::reuseSnapMemo(const SnapMemoKey& key) {
		if (snapMemoValid_ && key == snapMemoKey_ && points_ == snapMemoPoints_) {
			return true;
		}
		snapMemoValid_ = true;
		snapMemoKey_ = key;
		snapMemoPoints_ = points_;
		snapMemoClosedShape_.reset();
		++snapEvaluationCount_;
		return false;
	}

	DrawingFeedbackInput DrawingSystem::feedbackInput(
		const assets::PreviewStyle& style, int viewportWidth, int viewportHeight, float uiScale, const MeasureTextSize& measure
	) const {
		const PreviewVerdict verdict = previewVerdict();
		// Labels keep off the rejected edit's reason.
		std::optional<FeedbackBox> keepClear;
		if (const std::optional<EditReason> reason = editReason(viewportWidth, viewportHeight, uiScale, measure)) {
			keepClear = reason->box;
		}
		return DrawingFeedbackInput{
			.tool = activeTool_,
			.snap = lastSnap_,
			.points = points_,
			.cursor = cursor_,
			.freeform = snapMemoKey_.freeform,
			.display = UserSettings::Get().drawing,
			.style = style,
			.camera = *camera_,
			.viewportWidth = viewportWidth,
			.viewportHeight = viewportHeight,
			.uiScale = uiScale,
			.pixelsPerMeter = kPixelsPerMeter,
			.closeRadiusMeters = effectiveOriginCloseRadiusMeters(),
			.canClose = points_.size() >= (activeTool_ == ToolKind::Wall ? construction::kMinClosingWallChainPoints : kMinClosingShapePoints),
			.willClose = verdict.closeHalo,
			.snappedPointInvalid = verdict.markerInvalid,
			.invalidColor = invalidColor_,
			.accentColor = accentColor_,
			.commonLengthMergeMeters = ConstructionRegistry::Get().snapping().commonLengthMergeMeters,
			.keepClear = keepClear,
			.rectangleSnap = activeTool_ == ToolKind::FoundationRectangle ? rectangleSnapKind_ : construction::RectangleSnapKind::None,
			.sizeLabel = rectangleLabel_,
		};
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

	DrawingSystem::CloseVerdict DrawingSystem::refusedClose(std::string title, std::string reason) {
		return {.accepted = false, .title = std::move(title), .reason = std::move(reason)};
	}

	DrawingSystem::CloseVerdict DrawingSystem::closeVerdict() {
		// The ground checks (seen, buildable) run on every call, since they read player
		// visibility and the navigation mesh, which the snap memo's key does not cover. It is
		// the cost the placement cursor's own ground check already pays each frame.
		if (activeTool_ == ToolKind::Wall) {
			// The click adds the closing point, then commits the chain: its ground first
			// (commitWallChain's requirePlaceable), then each segment.
			std::vector<Foundation::Vec2> chain = points_;
			chain.push_back(cursor_);
			if (const std::string refusal = placementRefusal(chain); !refusal.empty()) {
				return refusedClose("Can't build here", "wall: " + refusal);
			}
			return closedShapeCheck();
		}
		if (activeTool_ == ToolKind::FoundationEdit) {
			if (!areaSeen(points_)) {
				return refusedClose("Can't edit foundation", "the area is unexplored");
			}
			// While the click closes, the move validated the placed points alone into
			// editPreview_; it reads the target's blueprint, so it is not kept with the snap.
			if (const std::string refusal = editRefusal(editPreview_, editMode_); !refusal.empty()) {
				return refusedClose("Can't edit foundation", refusal);
			}
			return {};
		}
		if (const CloseVerdict& ring = closedShapeCheck(); !ring.accepted) {
			return ring;
		}
		// The whole footprint must sit on walkable ground (interior + edges, not just the
		// vertices). Refuse a foundation that spans water or clips an off-mesh hole.
		if (const std::string refusal = placementRefusal(closingShape()); !refusal.empty()) {
			return refusedClose("Can't build here", "foundation: " + refusal);
		}
		return {};
	}

	const DrawingSystem::CloseVerdict& DrawingSystem::closedShapeCheck() {
		if (snapMemoClosedShape_) {
			return *snapMemoClosedShape_;
		}
		if (activeTool_ == ToolKind::Wall) {
			// Commit the chain into a copy of the world, as commitWallChain commits it into
			// the real one, so later segments are checked against the earlier ones too.
			snapMemoClosedShape_ = CloseVerdict{};
			if (const assets::ThicknessPreset* preset = activePreset()) {
				std::vector<Foundation::Vec2> chain = points_;
				chain.push_back(cursor_);
				construction::ConstructionWorld trial = constructionWorld_;
				const WallChainCommit			attempt = commitWallSegments(trial, chain, *preset);
				if (attempt.rejected > 0) {
					snapMemoClosedShape_ = refusedClose("Wall rejected", "wall: " + attempt.firstRefusal);
				}
			}
			return *snapMemoClosedShape_;
		}
		const char* title = activeTool_ == ToolKind::FoundationRectangle ? "Cannot place" : "Cannot close";
		const construction::ConstructionValidator validator(ConstructionRegistry::Get().constraints(), constructionWorld_);
		const construction::ValidationResult	  ring = validator.validateRing(closingShape());
		snapMemoClosedShape_ = ring.ok() ? CloseVerdict{} : refusedClose(title, construction::validationReason(ring.code));
		return *snapMemoClosedShape_;
	}

	DrawingSystem::ClickVerdict DrawingSystem::clickVerdict() const {
		if (willClose_) {
			return {.valid = pendingClose_.accepted, .reason = pendingClose_.reason};
		}
		// A pointer still where it placed the last point, whose snapped point is refused as too close
		// to it, proposes nothing: no red the player did not cause by moving. A snapped point that is
		// valid is placed, and any other refusal shows as it would anywhere.
		if (restsOnLastPoint() && lastValidation_.code == construction::ValidationCode::VerticesTooClose) {
			return {.proposed = false};
		}
		// Off-mesh cursor (on water) reads as invalid just like a geometry violation, so the
		// band and fill turn red before the player commits. A point the next click would refuse
		// outranks the outline an edit would produce; then an edit reads red while that outline
		// is invalid.
		if (cursorOffBuildableGround) {
			return {.valid = false, .reason = cursorGroundReason()};
		}
		if (!lastValidation_.ok()) {
			return {.valid = false, .reason = construction::validationReason(lastValidation_.code)};
		}
		if (activeTool_ == ToolKind::FoundationEdit && editPreviewActive_ && !editPreview_.ok()) {
			return {.valid = false, .reason = construction::validationReason(editPreview_.validation.code)};
		}
		return {};
	}

	bool DrawingSystem::restsOnLastPoint() const {
		return activeTool_ == ToolKind::Foundation && lastPlacement_ && lastPlacement_->pointCount == points_.size() &&
			   snapMemoKey_.cursorWorld == lastPlacement_->pointer;
	}

	DrawingSystem::PreviewVerdict DrawingSystem::previewVerdict() const {
		const bool		   editing = activeTool_ == ToolKind::FoundationEdit && editPreviewActive_;
		const ClickVerdict click = clickVerdict();
		PreviewVerdict	   verdict{.valid = click.valid};
		if (willClose_) {
			// An edit's outline can be valid and its close still refused, for the ground it
			// would build on: the outline and the reason take the close's verdict. A rectangle
			// does not close onto a first point, so it draws no halo.
			verdict.closeHalo = pendingClose_.accepted && activeTool_ != ToolKind::FoundationRectangle;
			verdict.markerInvalid = !pendingClose_.accepted;
			verdict.editOutlineValid = pendingClose_.accepted;
			if (editing && !pendingClose_.accepted) {
				verdict.editReason = pendingClose_.reason;
			}
			return verdict;
		}
		verdict.markerInvalid = cursorOffBuildableGround;
		if (editing && !editPreview_.ok()) {
			verdict.editReason = construction::validationReason(editPreview_.validation.code);
		}
		return verdict;
	}

	void DrawingSystem::commitShape() {
		if (const CloseVerdict verdict = closeVerdict(); !verdict.accepted) {
			if (callbacks_.onToast) {
				callbacks_.onToast(verdict.title, verdict.reason);
			}
			return;
		}

		const auto commit = constructionWorld_.commitFoundation(closingShape(), activeMaterial_);
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
		pendingClose_ = {};
		clearRectangle();
	}

	const std::vector<Foundation::Vec2>& DrawingSystem::closingShape() const {
		return activeTool_ == ToolKind::FoundationRectangle ? rectangleCorners_ : points_;
	}

	ecs::EntityID DrawingSystem::spawnBlueprintEntity(ec::FoundationId id) {
		if (ecsWorld_ == nullptr) {
			return ecs::kInvalidEntity;
		}

		const auto* foundation = constructionWorld_.get(id);
		if (foundation == nullptr) {
			return ecs::kInvalidEntity;
		}

		const float			   area = constructionWorld_.areaSquareMeters(id);
		const FoundationSizing sizing = sizeFoundation(area, foundation->material);

		auto entity = ecsWorld_->createEntity();

		const Foundation::Vec2 site = sitePosition(foundation->ring);
		ecsWorld_->addComponent<ecs::Position>(entity, ecs::Position{{site.x, site.y}});

		ecsWorld_->addComponent<ecs::Structure>(entity, ecs::Structure{ecs::StructureKind::Foundation, id});

		ecs::StructureBlueprint blueprint;
		blueprint.phase = ecs::StructureBlueprint::BuildPhase::Clearing;
		if (sizing.requiredQty > 0) {
			blueprint.required.emplace_back(foundation->material, sizing.requiredQty);
		}
		blueprint.workTotal = sizing.workTotal;
		ecsWorld_->addComponent<ecs::StructureBlueprint>(entity, std::move(blueprint));

		// HP scales with area; full HP is only meaningful once built but the
		// component exists from creation (architecture: avoid a later migration).
		ecsWorld_->addComponent<ecs::StructureHealth>(entity, ecs::StructureHealth{sizing.maxHp, sizing.maxHp});

		LOG_INFO(
			Game,
			"Foundation #%llu spawned: %.1f m^2, %s, %u materials, %.0f work, entity %u",
			static_cast<unsigned long long>(id),
			static_cast<double>(area),
			foundation->material.c_str(),
			sizing.requiredQty,
			static_cast<double>(sizing.workTotal),
			static_cast<uint32_t>(entity)
		);
		constructionWorld_.setEntity(id, entity);
		return entity;
	}

	void DrawingSystem::resizeBlueprintEntity(ec::FoundationId id) {
		const auto* foundation = constructionWorld_.get(id);
		if (ecsWorld_ == nullptr || foundation == nullptr || !ecsWorld_->isAlive(foundation->entity)) {
			return;
		}
		auto* blueprint = ecsWorld_->getComponent<ecs::StructureBlueprint>(foundation->entity);
		if (blueprint == nullptr) {
			return;
		}

		const FoundationSizing sizing = sizeFoundation(constructionWorld_.areaSquareMeters(id), foundation->material);
		blueprint->required.clear();
		if (sizing.requiredQty > 0) {
			blueprint->required.emplace_back(foundation->material, sizing.requiredQty);
		}
		blueprint->workTotal = sizing.workTotal;
		blueprint->phase = ecs::StructureBlueprint::BuildPhase::Clearing;

		if (auto* health = ecsWorld_->getComponent<ecs::StructureHealth>(foundation->entity)) {
			*health = ecs::StructureHealth{sizing.maxHp, sizing.maxHp};
		}
		if (auto* position = ecsWorld_->getComponent<ecs::Position>(foundation->entity)) {
			const Foundation::Vec2 site = sitePosition(foundation->ring);
			position->value = {site.x, site.y};
		}

		// The goal tree was sized and placed for the old footprint (and a clear goal
		// already in it would keep a new one from emitting); the next construction
		// tick rebuilds it from the new one.
		ecsWorld_->getSystem<ecs::ConstructionSystem>().resetBlueprintGoals(foundation->entity);
	}

	const ecs::StructureBlueprint* DrawingSystem::foundationBlueprint(ec::FoundationId id) const {
		const auto* foundation = constructionWorld_.get(id);
		if (ecsWorld_ == nullptr || foundation == nullptr || !ecsWorld_->isAlive(foundation->entity)) {
			return nullptr;
		}
		return ecsWorld_->getComponent<ecs::StructureBlueprint>(foundation->entity);
	}

	DrawingSystem::FoundationEditOutcome
	DrawingSystem::applyFoundationEdit(ec::FoundationId target, const std::vector<Foundation::Vec2>& drawn, ec::FoundationEditMode mode) {
		const construction::ConstructionValidator validator(ConstructionRegistry::Get().constraints(), constructionWorld_);
		return applyValidatedFoundationEdit(target, drawn, mode, validator.validateFoundationEdit(target, drawn, mode, foundationBlueprint(target)));
	}

	DrawingSystem::FoundationEditOutcome DrawingSystem::applyValidatedFoundationEdit(
		construction::FoundationId				  target,
		const std::vector<Foundation::Vec2>&	  drawn,
		construction::FoundationEditMode		  mode,
		const construction::FoundationEditResult& result
	) {
		FoundationEditOutcome outcome;
		outcome.target = target;
		if (std::string refusal = editRefusal(result, mode); !refusal.empty()) {
			outcome.reason = std::move(refusal);
			return outcome;
		}

		if (!result.extension.empty()) {
			const ec::CommitResult extension = constructionWorld_.commitExtension(target, result.extension);
			if (!extension.ok()) {
				outcome.reason = "extension rejected";
				return outcome;
			}
			outcome.extension = extension.id;
			outcome.entity = spawnBlueprintEntity(extension.id);
		} else {
			geometry::Ring cutter = toRingMm(drawn);
			geometry::ensureCounterClockwise(cutter);
			const ec::CommitStatus status = mode == ec::FoundationEditMode::Add ? constructionWorld_.addToFoundation(target, cutter)
																				: constructionWorld_.subtractFromFoundation(target, cutter);
			if (status != ec::CommitStatus::Ok) {
				outcome.reason = "edit rejected";
				return outcome;
			}
			resizeBlueprintEntity(target);
			outcome.entity = constructionWorld_.get(target)->entity;
		}

		outcome.ok = true;
		outcome.areaSquareMeters = static_cast<float>(std::abs(geometry::signedAreaSquareMeters(result.outline)));
		LOG_INFO(
			Game,
			"Foundation #%llu %s: outline %.1f m^2%s",
			static_cast<unsigned long long>(target),
			mode == ec::FoundationEditMode::Add ? "add" : "subtract",
			static_cast<double>(outcome.areaSquareMeters),
			outcome.extension != ec::kInvalidFoundation ? " (extension blueprint)" : ""
		);
		return outcome;
	}

	ecs::EntityID DrawingSystem::mergeExtension(ec::FoundationId extensionId) {
		const auto* extension = constructionWorld_.get(extensionId);
		if (extension == nullptr) {
			return ecs::kInvalidEntity;
		}
		const ec::FoundationId targetId = extension->mergeTarget;
		const ecs::EntityID	   extensionEntity = extension->entity;
		const auto*			   target = constructionWorld_.get(targetId);
		if (target == nullptr) {
			return ecs::kInvalidEntity;
		}
		const ecs::EntityID targetEntity = target->entity;

		const ec::CommitStatus status = constructionWorld_.mergeExtension(extensionId);
		if (status != ec::CommitStatus::Ok) {
			LOG_ERROR(
				Game,
				"Extension #%llu failed to merge into #%llu (status %d)",
				static_cast<unsigned long long>(extensionId),
				static_cast<unsigned long long>(targetId),
				static_cast<int>(status)
			);
			return ecs::kInvalidEntity;
		}

		// The merged foundation owns what both halves cost and did, so demolishing it
		// refunds and un-builds the whole thing.
		if (ecsWorld_ != nullptr && ecsWorld_->isAlive(targetEntity) && ecsWorld_->isAlive(extensionEntity)) {
			auto*		targetBp = ecsWorld_->getComponent<ecs::StructureBlueprint>(targetEntity);
			const auto* extensionBp = ecsWorld_->getComponent<ecs::StructureBlueprint>(extensionEntity);
			if (targetBp != nullptr && extensionBp != nullptr) {
				addManifest(targetBp->required, extensionBp->required);
				addManifest(targetBp->delivered, extensionBp->delivered);
				targetBp->workTotal += extensionBp->workTotal;
				targetBp->workDone += extensionBp->workDone;
			}
			auto*		targetHp = ecsWorld_->getComponent<ecs::StructureHealth>(targetEntity);
			const auto* extensionHp = ecsWorld_->getComponent<ecs::StructureHealth>(extensionEntity);
			if (targetHp != nullptr && extensionHp != nullptr) {
				targetHp->hp += extensionHp->hp;
				targetHp->maxHp += extensionHp->maxHp;
			}
			if (auto* position = ecsWorld_->getComponent<ecs::Position>(targetEntity)) {
				const Foundation::Vec2 site = sitePosition(constructionWorld_.get(targetId)->ring);
				position->value = {site.x, site.y};
			}
		}

		LOG_INFO(
			Game,
			"Extension #%llu merged into foundation #%llu: %.1f m^2",
			static_cast<unsigned long long>(extensionId),
			static_cast<unsigned long long>(targetId),
			static_cast<double>(constructionWorld_.areaSquareMeters(targetId))
		);
		return extensionEntity;
	}

	construction::FoundationEditResult DrawingSystem::validateDrawnEdit(const std::vector<Foundation::Vec2>& drawn) const {
		const construction::ConstructionValidator validator(ConstructionRegistry::Get().constraints(), constructionWorld_);
		return validator.validateFoundationEdit(editTarget_, drawn, editMode_, foundationBlueprint(editTarget_));
	}

	void DrawingSystem::refreshEditPreview(std::optional<construction::FoundationEditResult> closedEdit) {
		std::vector<Foundation::Vec2> drawn = points_;
		if (!willClose_) {
			drawn.push_back(cursor_);
		}
		editPreviewActive_ = drawn.size() >= kMinClosingShapePoints;
		if (!editPreviewActive_) {
			editPreview_ = {};
			return;
		}
		editPreview_ = willClose_ && closedEdit ? std::move(*closedEdit) : validateDrawnEdit(drawn);
	}

	std::string DrawingSystem::editRefusal(const construction::FoundationEditResult& result, construction::FoundationEditMode mode) const {
		if (!result.ok()) {
			return construction::validationReason(result.validation.code);
		}
		if (mode == construction::FoundationEditMode::Add && !footprintBuildable(dequantizeRing(result.buildsOn))) {
			return "not on buildable ground";
		}
		return {};
	}

	void DrawingSystem::commitFoundationEdit() {
		if (const CloseVerdict verdict = closeVerdict(); !verdict.accepted) {
			if (callbacks_.onToast) {
				callbacks_.onToast(verdict.title, verdict.reason);
			}
			return;
		}
		// The verdict passed editPreview_, the placed points' validation from this click's move.
		const FoundationEditOutcome outcome = applyValidatedFoundationEdit(editTarget_, points_, editMode_, editPreview_);
		if (!outcome.ok) {
			if (callbacks_.onToast) {
				callbacks_.onToast("Can't edit foundation", outcome.reason);
			}
			return;
		}
		if (callbacks_.onToast) {
			if (outcome.extension != ec::kInvalidFoundation) {
				callbacks_.onToast("Extension placed", "merges into the foundation once built");
			} else {
				callbacks_.onToast("Foundation reshaped", editMode_ == ec::FoundationEditMode::Add ? "area added" : "area removed");
			}
		}

		// Stay in the tool, ready for the next region (zone-style).
		points_.clear();
		lastValidation_ = {};
		willClose_ = false;
		editPreviewActive_ = false;
	}

	// =========================================================================
	// Wall tool. Mirrors the foundation path: snapWall + validateWallPoint live,
	// per-segment validateWallSegment + commitSegment on finish, one blueprint
	// entity per committed segment. The chain is hosted by one foundation, and a
	// point on its first point closes and commits it.
	// =========================================================================

	const assets::ThicknessPreset* DrawingSystem::activePreset() const {
		return ConstructionRegistry::Get().getThicknessPreset(activeMaterial_, activeThicknessPreset_);
	}

	void DrawingSystem::handleWallMove(Foundation::Vec2 world, bool freeform) {
		auto& registry = ConstructionRegistry::Get();

		// The snap insets foundation-edge/corner hits by the wall's half-thickness for
		// outer-face-flush alignment, so resolve the preset first. Without a preset (a
		// material with no wall config) there is no thickness to measure: snap with no
		// inset and skip validation below.
		const auto*		  preset = activePreset();
		const SnapMemoKey key = snapMemoKey(world, freeform, preset);
		if (!reuseSnapMemo(key)) {
			const ec::SnapEngine snap(registry.snapping(), constructionWorld_);
			snapMemoResult_ = snap.snapWall(
				points_,
				world,
				ec::SnapInput{
					.aids = key.aids,
					.freeform = freeform,
					.originCloseRadiusMeters = effectiveOriginCloseRadiusMeters(),
					.wallHalfThicknessMm = preset != nullptr ? preset->halfThicknessMm : 0,
					.maxPoints = registry.constraints().maxPoints,
				}
			);
			snapMemoValidation_ = {};
			if (preset != nullptr) {
				// The host is the foundation the FIRST point landed on. For the live preview
				// before the first click, probe the cursor's foundation so the colorize and
				// readouts reflect where the chain would start.
				ec::FoundationId host = wallHost_;
				if (host == ec::kInvalidFoundation && points_.empty()) {
					host = constructionWorld_.foundationAt(geometry::quantize(snapMemoResult_.point));
				}
				const ec::ConstructionValidator validator(registry.constraints(), constructionWorld_);
				snapMemoValidation_ = validator.validateWallPoint(points_, snapMemoResult_.point, *preset, host);
			}
		}
		lastSnap_ = snapMemoResult_;
		cursor_ = lastSnap_.point;
		lastValidation_ = snapMemoValidation_;

		// Live nav feedback for the wall rubber-band: off-mesh cursor reads red (see
		// renderWallChainPreview). The commit gate re-checks the whole chain regardless.
		cursorOffBuildableGround = !pointOnBuildableGround(cursor_);

		willClose_ = preset != nullptr && lastSnap_.closesShape() && lastValidation_.ok() && !cursorOffBuildableGround;
		pendingClose_ = willClose_ ? closeVerdict() : CloseVerdict{};
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

		// Nav gate: refuse a point that lands off buildable ground (water or a ford) before it
		// enters the chain, with clear feedback. The whole-chain re-check on commit still
		// catches a segment that bridges water between two on-mesh points.
		if (cursorOffBuildableGround) {
			if (callbacks_.onToast) {
				callbacks_.onToast("Can't build here", "wall: " + cursorGroundReason());
			}
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

		const bool closes = ec::closesWallChain(points_, cursor_);
		points_.push_back(cursor_);
		if (closes) {
			commitWallChain();
		}
		return true;
	}

	void DrawingSystem::commitWallChain() {
		const auto* preset = activePreset();
		if (preset == nullptr || points_.size() < 2) {
			points_.clear();
			wallHost_ = ec::kInvalidFoundation;
			lastValidation_ = {};
			willClose_ = false;
			pendingClose_ = {};
			return;
		}

		// The whole chain centerline must sit on buildable ground. One off-mesh segment
		// (e.g. one that bridges water) refuses the WHOLE chain so nothing is stamped.
		if (!requirePlaceable(points_, "wall")) {
			points_.clear();
			wallHost_ = ec::kInvalidFoundation;
			lastValidation_ = {};
			willClose_ = false;
			pendingClose_ = {};
			return;
		}

		const WallChainCommit commit = commitWallSegments(constructionWorld_, points_, *preset);

		// One reconcile after the whole chain: spawns a correctly-sized entity for
		// every segment created/split above, and destroys any orphaned by a split.
		reconcileSegmentEntities();

		if (callbacks_.onToast) {
			if (commit.committed > 0) {
				char buf[48];
				std::snprintf(buf, sizeof(buf), "%d segment%s", commit.committed, commit.committed == 1 ? "" : "s");
				callbacks_.onToast("Wall placed", std::string(buf) + " (" + activeMaterial_ + ")");
			} else if (commit.rejected > 0) {
				callbacks_.onToast("Wall rejected", "no valid segments");
			}
		}

		// Stay in the tool, ready for the next chain.
		points_.clear();
		wallHost_ = ec::kInvalidFoundation;
		lastValidation_ = {};
		willClose_ = false;
		pendingClose_ = {};
	}

	DrawingSystem::WallChainCommit DrawingSystem::commitWallSegments(
		construction::ConstructionWorld&	 into,
		const std::vector<Foundation::Vec2>& chain,
		const assets::ThicknessPreset&		 preset
	) const {
		// The validator reads `into`, so each pair is checked against the ones committed before it.
		const construction::ConstructionValidator validator(ConstructionRegistry::Get().constraints(), into);
		WallChainCommit							  commit;
		const auto								  skip = [&commit](std::string reason) {
			 ++commit.rejected;
			 if (commit.firstRefusal.empty()) {
				 commit.firstRefusal = std::move(reason);
			 }
		};
		// Read once, from the world committed into: every segment of the chain shares it, so the
		// chain is one owner (D12). A trial on a copy reads the same value the real commit will.
		const std::uint64_t placementOrder = into.version();
		for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
			const Foundation::Vec2 start = chain[i];
			const Foundation::Vec2 end = chain[i + 1];

			// Reject-don't-repair: skip the bad segment, keep the rest.
			if (const construction::ValidationResult check = validator.validateWallSegment(start, end, preset, wallHost_); !check.ok()) {
				skip(construction::validationReason(check.code));
				continue;
			}
			const construction::SegmentCommitResult result = into.commitSegment(
				geometry::quantize(start), geometry::quantize(end), activeMaterial_, activeThicknessPreset_, wallHost_, placementOrder
			);
			if (!result.ok()) {
				skip("segment rejected");
				continue;
			}
			// A single pair can create several segments (drawn through a junction,
			// or a T-split of an existing wall); count them all as committed.
			commit.committed += static_cast<int>(result.createdSegments.size());
		}
		return commit;
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
		const std::uint64_t placementOrder = constructionWorld_.version();
		int					committed = 0;
		for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
			const geometry::Vec2i64 qa = geometry::quantize(pts[i]);
			const geometry::Vec2i64 qb = geometry::quantize(pts[i + 1]);
			const auto				result = constructionWorld_.commitSegment(qa, qb, material, thicknessPreset, host, placementOrder);
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

		// Edge fill bypasses the chain-commit gate, so apply the same whole-centerline nav
		// check here: the inset corners must lie on buildable ground (one shared predicate).
		if (!requirePlaceable({bestA, bestB}, "wall")) {
			return false;
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
			geometry::quantize(bestA),
			geometry::quantize(bestB),
			activeMaterial_,
			activeThicknessPreset_,
			host,
			constructionWorld_.version()
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

	const assets::OpeningTypeDef* DrawingSystem::activeOpeningType() const {
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
		openingUnexplored_ = openingSnap_.valid && !openingSeen(type->widthMeters);

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
		if (openingUnexplored_) {
			if (callbacks_.onToast) {
				callbacks_.onToast("Can't build here", "opening: the area is unexplored");
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
				s.valid = openingValidation_.ok() && !openingUnexplored_;
				s.message = openingUnexplored_ ? "the area is unexplored" : ec::validationReason(openingValidation_.code);
			}
			return s;
		}

		// The strip reads the verdict the preview reads, so the two cannot disagree.
		const ClickVerdict click = clickVerdict();
		s.valid = click.valid;
		s.message = click.reason;

		if (activeTool_ == ToolKind::Wall) {
			s.thicknessPreset = activeThicknessPreset_;

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

		if (activeTool_ == ToolKind::FoundationEdit) {
			// Material is locked to the target's (Add requires the same material).
			// Readouts track the outline the edit would produce once there is a polygon.
			s.foundationEdit = true;
			s.editLabel = editMode_ == ec::FoundationEditMode::Add ? "Add to foundation" : "Subtract from foundation";
			if (const auto* target = constructionWorld_.get(editTarget_)) {
				s.material = target->material;
			}
			const float current = constructionWorld_.areaSquareMeters(editTarget_);
			s.areaSquareMeters = current;
			if (editPreviewActive_ && editPreview_.ok()) {
				s.areaSquareMeters = static_cast<float>(std::abs(geometry::signedAreaSquareMeters(editPreview_.outline)));
				s.areaDeltaSquareMeters = s.areaSquareMeters - current;
			}
			return s;
		}

		// The foundation tools: the area tracks the shape whatever its validity, once there is
		// a closed one (three placed points, or a rectangle's two corners).
		s.foundationTool = activeFoundationTool();
		if (closingShape().size() >= kMinClosingShapePoints) {
			s.areaSquareMeters = static_cast<float>(std::abs(geometry::signedAreaSquareMeters(toRingMm(closingShape()))));
		}
		return s;
	}

	// =========================================================================
	// INTERIM rendering. Committed-foundation rendering here is a placeholder;
	// C6 replaces it with the baked element-emitter + build-progress prefix.
	// =========================================================================

	float DrawingSystem::blueprintProgress(bool built, ecs::EntityID entity) const {
		if (built) {
			return 1.0F;
		}
		// isAlive checks the generation word; ComponentPool::get matches by index
		// only, so a stale cached handle whose index was recycled would otherwise
		// read a DIFFERENT entity's blueprint progress.
		if (ecsWorld_ != nullptr && entity != ecs::kInvalidEntity && ecsWorld_->isAlive(entity)) {
			if (const auto* bp = ecsWorld_->getComponent<ecs::StructureBlueprint>(entity)) {
				return bp->progress();
			}
		}
		return 0.0F;
	}

	void DrawingSystem::renderCommitted(int viewportW, int viewportH) {
		if (camera_ == nullptr) {
			return;
		}

		// Geometry + state-derived styling come from the version-keyed cache;
		// only build progress (ECS blueprint workDone/workTotal) is read per frame.
		committedCache_.refresh(constructionWorld_);

		// Cull to the viewport. The margin covers outline widths and the band
		// half-thickness so edge geometry doesn't pop at the screen border.
		constexpr float	 kCullMarginMeters = 2.0F;
		Foundation::Rect visibleWorld = camera_->getVisibleRect(viewportW, viewportH, kPixelsPerMeter);
		visibleWorld.x -= kCullMarginMeters;
		visibleWorld.y -= kCullMarginMeters;
		visibleWorld.width += 2.0F * kCullMarginMeters;
		visibleWorld.height += 2.0F * kCullMarginMeters;

		const auto& fs = ConstructionRegistry::Get().rendering().foundation;

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
		std::vector<Foundation::Vec2> screen;
		std::vector<Foundation::Vec2> fill;
		for (const auto& g : committedCache_.foundations()) {
			if (!g.aabb.intersects(visibleWorld)) {
				continue;
			}
			const float progress = blueprintProgress(g.built, g.entity);

			screen.clear();
			screen.reserve(g.ring.size());
			for (const auto& v : g.ring) {
				screen.push_back(toScreen(v));
			}
			fill.clear();
			fill.reserve(g.fillVertices.size());
			for (const auto& v : g.fillVertices) {
				fill.push_back(toScreen(v));
			}

			// Layer 1: faint blueprint base (always present, reads as the planned footprint).
			Renderer::Primitives::drawTriangles({
				.vertices = fill.data(),
				.indices = g.fillIndices.data(),
				.vertexCount = fill.size(),
				.indexCount = g.fillIndices.size(),
				.color = toColor(fs.blueprintFill),
				.id = "committed_foundation_base",
				.key = 50,
			});

			// Layer 2: progress fill, alpha proportional to workDone/workTotal. Ramps from a
			// barely-there tint at 0% to a solid floor at 100% / Built.
			if (progress > 0.0F) {
				const float fillAlpha =
					g.built ? fs.progressAlphaMax : (fs.progressAlphaMin + (fs.progressAlphaMax - fs.progressAlphaMin) * progress);
				Renderer::Primitives::drawTriangles({
					.vertices = fill.data(),
					.indices = g.fillIndices.data(),
					.vertexCount = fill.size(),
					.indexCount = g.fillIndices.size(),
					.color = {g.matColor.r, g.matColor.g, g.matColor.b, fillAlpha},
					.id = "committed_foundation_progress",
					.key = 51,
				});
			}

			// Layer 3: outline. A Built foundation wears a darker shade of its own
			// material; a blueprint keeps the cool outline, firming up toward Built.
			const float				outlineAlpha = fs.outlineAlphaMin + (fs.outlineAlphaMax - fs.outlineAlphaMin) * progress;
			const Foundation::Color outline = g.built ? darken(g.matColor, fs.builtEdgeDarken) : toColor(fs.outlineColor, outlineAlpha);
			for (std::size_t i = 0; i < screen.size(); ++i) {
				Renderer::Primitives::drawLine({
					.start = screen[i],
					.end = screen[(i + 1) % screen.size()],
					.style = {.color = outline, .width = g.built ? fs.outlineWidthBuilt : fs.outlineWidthBlueprint},
					.id = "committed_foundation_edge",
					.key = 52,
				});
			}
		}

		// Walls (trimmed bands + junction polygons) and the opening fills in their
		// gaps, all from the same cache. The z values (50-64) only order committed
		// construction against itself: the caller flushes this pass before the
		// entity pass, so they never compete with entity, preview, or UI order.
		renderCommittedWalls(visibleWorld, viewportW, viewportH);
		renderCommittedOpenings(visibleWorld, viewportW, viewportH);
	}

	void DrawingSystem::renderPreview(int viewportWidth, int viewportHeight, float uiScale) {
		if (camera_ == nullptr || state_ != DrawingState::Drawing) {
			return;
		}

		const auto& previewStyle = ConstructionRegistry::Get().rendering().preview;

		auto toScreen = [&](Foundation::Vec2 world) -> Foundation::Vec2 {
			return camera_->worldToScreen(world.x, world.y, viewportWidth, viewportHeight, kPixelsPerMeter);
		};

		// Wall chain preview is a centerline + thickness band, not a closed polygon.
		if (activeTool_ == ToolKind::Wall) {
			renderWallChainPreview(viewportWidth, viewportHeight, uiScale);
			return;
		}

		// Opening tool: a validity-colorized ghost at the snapped position.
		if (activeTool_ == ToolKind::Opening) {
			renderOpeningGhost(viewportWidth, viewportHeight);
			return;
		}

		if (activeTool_ == ToolKind::FoundationRectangle) {
			renderRectanglePreview(viewportWidth, viewportHeight, uiScale);
			return;
		}

		// Add / Subtract: the resulting outline under the drawn polygon's preview.
		if (activeTool_ == ToolKind::FoundationEdit) {
			renderEditOutlinePreview(viewportWidth, viewportHeight, uiScale);
		}

		// The band and fill read what the click would do: while it closes, whether the close
		// commits (the halo draws only then); otherwise whether the point may be placed.
		const bool				valid = previewVerdict().valid;
		const Foundation::Color okColor = validColor_;	  // green
		const Foundation::Color badColor = invalidColor_; // red
		const Foundation::Color lineColor = valid ? okColor : badColor;

		std::vector<Foundation::Vec2> screen;
		screen.reserve(points_.size());
		for (const auto& point : points_) {
			screen.push_back(toScreen(point));
		}

		// Faint fill once >= 3 points (the implied closed polygon).
		if (points_.size() >= 3) {
			const Foundation::Color fillColor = valid ? Foundation::Color{okColor.r, okColor.g, okColor.b, previewStyle.fillPreviewAlpha}
													  : Foundation::Color{badColor.r, badColor.g, badColor.b, previewStyle.fillPreviewAlpha};
			fillPolygon(screen, fillColor, "drawing_fill_preview", 900);
		}

		// Committed edges between placed points.
		for (std::size_t i = 0; i + 1 < screen.size(); ++i) {
			Renderer::Primitives::drawLine({
				.start = screen[i],
				.end = screen[i + 1],
				.style = {.color = okColor, .width = previewStyle.lineWidth},
				.id = "drawing_edge",
				.key = 901,
			});
		}

		// Rubber-band from the last placed point to the snapped cursor.
		if (!points_.empty()) {
			Renderer::Primitives::drawLine({
				.start = screen.back(),
				.end = toScreen(cursor_),
				.style = {.color = lineColor, .width = previewStyle.lineWidth},
				.id = "drawing_rubberband",
				.key = 902,
			});
		}

		// Above the edges, the rubber band, and the vertex dots (its keys start at 904).
		drawSnapFeedback(feedbackInput(previewStyle, viewportWidth, viewportHeight, uiScale, measureFeedbackText));

		// Vertex dots.
		for (const auto& vertex : screen) {
			Renderer::Primitives::drawCircle({
				.center = vertex,
				.radius = previewStyle.vertexRadiusPx,
				.style = {.fill = okColor},
				.id = "drawing_vertex",
				.key = 903,
			});
		}

		// Red highlight on the offending PLACED vertex/edge when a geometry constraint is
		// violated -- but not when the click will close (willClose_: the explicit origin
		// snap, or the near-start rescue): the point's verdict doesn't apply to a close.
		// Gated on the geometry result (not `valid`) because lastValidation_.vertexIndex
		// only indexes a placed vertex; off buildable ground, the snapped-point marker
		// turns red instead (drawSnapFeedback). A pointer resting on the point just placed, its
		// snapped point refused as too close, proposes nothing, so nothing is offending.
		if (!lastValidation_.ok() && !willClose_ && clickVerdict().proposed && !screen.empty()) {
			const std::size_t offending = std::min(lastValidation_.vertexIndex, screen.size() - 1);
			Renderer::Primitives::drawCircle({
				.center = screen[offending],
				.radius = previewStyle.invalidVertexRadiusPx,
				.style = {.fill = {badColor.r, badColor.g, badColor.b, 0.5F}},
				.id = "drawing_invalid_vertex",
				.key = 905,
			});
		}
	}

	void DrawingSystem::renderRectanglePreview(int viewportWidth, int viewportHeight, float uiScale) {
		const auto& previewStyle = ConstructionRegistry::Get().rendering().preview;
		const auto	toScreen = [&](Foundation::Vec2 world) -> Foundation::Vec2 {
			 return camera_->worldToScreen(world.x, world.y, viewportWidth, viewportHeight, kPixelsPerMeter);
		};

		// The prototype's order: the grid cross (the feedback's alignment layer), then the first
		// corner, the fill, and the outline, then the rings, the size label, and the marker over
		// them. The rectangle draws at the rings' key before the feedback, so the rings, drawn
		// later at the same key, go over it.
		const std::int32_t rectangleKey = feedbackLayerKey(ToolKind::FoundationRectangle, FeedbackLayer::Marker);
		if (!points_.empty()) {
			Renderer::Primitives::drawCircle({
				.center = toScreen(points_.front()),
				.radius = previewStyle.vertexRadiusPx,
				.style = {.fill = validColor_},
				.id = "drawing_vertex",
				.key = rectangleKey,
			});
		}
		// The fill and outline read what the release would do, as the freeform band does.
		const Foundation::Color lineColor = previewVerdict().valid ? validColor_ : invalidColor_;
		if (!rectangleCorners_.empty()) {
			std::vector<Foundation::Vec2> screen;
			screen.reserve(rectangleCorners_.size());
			for (const Foundation::Vec2 corner : rectangleCorners_) {
				screen.push_back(toScreen(corner));
			}
			fillPolygon(screen, {lineColor.r, lineColor.g, lineColor.b, previewStyle.fillPreviewAlpha}, "drawing_fill_preview", rectangleKey);
			for (std::size_t index = 0; index < screen.size(); ++index) {
				Renderer::Primitives::drawLine({
					.start = screen[index],
					.end = screen[(index + 1) % screen.size()],
					.style = {.color = lineColor, .width = previewStyle.lineWidth},
					.id = "drawing_rubberband",
					.key = rectangleKey,
				});
			}
		}
		drawSnapFeedback(feedbackInput(previewStyle, viewportWidth, viewportHeight, uiScale, measureFeedbackText));
	}

	namespace {

		// Filled polygon plus an outline via Primitives. Screen-space points.
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
			fillPolygon(screen, fill, fillId, zFill);
			for (std::size_t i = 0; i < n; ++i) {
				Renderer::Primitives::drawLine({
					.start = screen[i],
					.end = screen[(i + 1) % n],
					.style = {.color = outline, .width = outlineWidth},
					.id = edgeId,
					.key = zEdge,
				});
			}
		}

	} // namespace

	void DrawingSystem::renderCommittedWalls(const Foundation::Rect& visibleWorld, int viewportW, int viewportH) {
		const auto& ws = ConstructionRegistry::Get().rendering().wall;

		auto toScreen = [&](Foundation::Vec2 w) -> Foundation::Vec2 {
			return camera_->worldToScreen(w.x, w.y, viewportW, viewportH, kPixelsPerMeter);
		};

		// Reject-don't-repair: resolveWallBands refused these graphs at rebuild time;
		// bare centerlines still show the walls exist, never garbage bands.
		for (const auto& line : committedCache_.fallbackCenterlines()) {
			Renderer::Primitives::drawLine({
				.start = toScreen(line[0]),
				.end = toScreen(line[1]),
				.style = {.color = toColor(ws.outlineColor, 0.8F), .width = ws.outlineWidthBuilt},
				.id = "committed_wall_fallback",
				.key = 60,
			});
		}

		// Per-segment pieces and the junctions each owns, styled like the foundation
		// render (z 60-62, above foundations at 50-52). The rings tile, so only the
		// cached outline is stroked: no seam where a band meets its corner. A wall's
		// bounds cover its outline edges as well as its rings.
		std::vector<Foundation::Vec2> screen;
		for (const auto& wall : committedCache_.walls()) {
			if (!wall.aabb.intersects(visibleWorld)) {
				continue;
			}
			const float progress = blueprintProgress(wall.built, wall.entity);

			const float fillAlpha =
				wall.built ? ws.progressAlphaMax : (ws.progressAlphaMin + (ws.progressAlphaMax - ws.progressAlphaMin) * progress);
			// Built walls wear a darker shade of their own material; blueprints keep the
			// cool outline so blue stays the "planned" signal.
			const Foundation::Color outline =
				wall.built ? darken(wall.matColor, ws.builtEdgeDarken)
						   : toColor(ws.outlineColor, ws.outlineAlphaMin + (ws.outlineAlphaMax - ws.outlineAlphaMin) * progress);

			for (const auto& ring : wall.rings) {
				screen.clear();
				screen.reserve(ring.size());
				for (const auto& v : ring) {
					screen.push_back(toScreen(v));
				}
				// Blueprint base always; progress fill ramps with workDone/workTotal.
				fillPolygon(screen, toColor(ws.blueprintFill), "committed_wall_base", 60);
				fillPolygon(screen, {wall.matColor.r, wall.matColor.g, wall.matColor.b, fillAlpha}, "committed_wall_progress", 61);
			}
			for (const auto& edge : wall.outline) {
				Renderer::Primitives::drawLine({
					.start = toScreen(edge[0]),
					.end = toScreen(edge[1]),
					.style = {.color = outline, .width = wall.built ? ws.outlineWidthBuilt : ws.outlineWidthBlueprint},
					.id = "committed_wall_edge",
					.key = 62,
				});
			}
		}
	}

	namespace {

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
						.key = 64,
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
					.key = 64,
				});
			}
		}

	} // namespace

	void DrawingSystem::renderCommittedOpenings(const Foundation::Rect& visibleWorld, int viewportW, int viewportH) {
		const auto& os = ConstructionRegistry::Get().rendering().opening;

		auto toScreen = [&](Foundation::Vec2 w) -> Foundation::Vec2 {
			return camera_->worldToScreen(w.x, w.y, viewportW, viewportH, kPixelsPerMeter);
		};

		std::vector<Foundation::Vec2> screen;
		for (const auto& g : committedCache_.openings()) {
			if (!g.aabb.intersects(visibleWorld)) {
				continue;
			}
			// Progress styling mirrors walls: a Built opening renders solid, a blueprint
			// dims with workDone/workTotal from its ECS mirror.
			const float progress = blueprintProgress(g.built, g.entity);
			const float alpha = g.built ? 1.0F : (os.progressAlphaMin + (os.progressAlphaMax - os.progressAlphaMin) * progress);

			screen.clear();
			screen.reserve(g.footprint.size());
			for (const auto& v : g.footprint) {
				screen.push_back(toScreen(v));
			}
			drawOpeningFill(screen, g.matColor, g.window, g.widthMeters, alpha, g.built ? os.outlineWidthBuilt : os.outlineWidthBlueprint);
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

		const bool				valid = openingValidation_.ok() && !openingUnexplored_;
		const Foundation::Color okColor = validColor_;	  // green
		const Foundation::Color badColor = invalidColor_; // red
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

	std::optional<DrawingSystem::EditReason>
	DrawingSystem::editReason(int viewportWidth, int viewportHeight, float uiScale, const MeasureTextSize& measure) const {
		if (camera_ == nullptr) {
			return std::nullopt;
		}
		std::string text = previewVerdict().editReason;
		if (text.empty()) {
			return std::nullopt;
		}
		const Foundation::Vec2 position = camera_->worldToScreen(cursor_.x, cursor_.y, viewportWidth, viewportHeight, kPixelsPerMeter) +
										  Foundation::Vec2{kEditReasonOffset.x * uiScale, kEditReasonOffset.y * uiScale};
		const float			   textScale = kEditReasonScale * uiScale;
		const Foundation::Vec2 size = measure(text, textScale * kRendererFontBaseSizePx, FeedbackTextWeight::Regular);
		return EditReason{
			.text = std::move(text),
			.position = position,
			.textScale = textScale,
			.box = {position.x, position.y, position.x + size.x + kEditReasonShadowPx, position.y + size.y + kEditReasonShadowPx},
		};
	}

	void DrawingSystem::renderEditOutlinePreview(int viewportWidth, int viewportHeight, float uiScale) {
		if (!editPreviewActive_) {
			return;
		}
		const auto& previewStyle = ConstructionRegistry::Get().rendering().preview;

		auto toScreen = [&](Foundation::Vec2 world) -> Foundation::Vec2 {
			return camera_->worldToScreen(world.x, world.y, viewportWidth, viewportHeight, kPixelsPerMeter);
		};

		// Outline only: the committed foundation already shows under it, and the drawn
		// region's own fill sits on top. While the click closes it takes the close's color.
		if (editPreview_.ok()) {
			const Foundation::Color color = previewVerdict().editOutlineValid ? validColor_ : invalidColor_;
			const geometry::Ring&	outline = editPreview_.outline;
			for (std::size_t i = 0; i < outline.size(); ++i) {
				Renderer::Primitives::drawLine({
					.start = toScreen(geometry::dequantize(outline[i])),
					.end = toScreen(geometry::dequantize(outline[(i + 1) % outline.size()])),
					.style = {.color = color, .width = previewStyle.lineWidth * 1.5F},
					.id = "drawing_edit_outline",
					.key = 898,
				});
			}
		}

		if (const std::optional<EditReason> reason = editReason(viewportWidth, viewportHeight, uiScale, measureFeedbackText)) {
			Renderer::Primitives::drawText({
				.text = reason->text,
				.position = reason->position,
				.scale = reason->textScale,
				.color = invalidColor_,
				.shadow = Foundation::BoxShadow{
					.color = {0.0F, 0.0F, 0.0F, 0.8F}, .blur = 0.0F, .offset = {kEditReasonShadowPx, kEditReasonShadowPx}
				},
				.id = "drawing_edit_reason",
				.key = 906,
			});
		}
	}

	void DrawingSystem::renderWallChainPreview(int viewportWidth, int viewportHeight, float uiScale) {
		const auto& previewStyle = ConstructionRegistry::Get().rendering().preview;

		auto toScreen = [&](Foundation::Vec2 world) -> Foundation::Vec2 {
			return camera_->worldToScreen(world.x, world.y, viewportWidth, viewportHeight, kPixelsPerMeter);
		};

		// The band and centerline read what the click would do: while it closes the chain,
		// whether the close commits (the halo draws only then); otherwise whether the point
		// may be placed (off buildable ground reads red like a geometry violation).
		const bool				valid = previewVerdict().valid;
		const Foundation::Color okColor = validColor_;	  // green
		const Foundation::Color badColor = invalidColor_; // red

		std::vector<Foundation::Vec2> screen;
		screen.reserve(points_.size());
		for (const auto& point : points_) {
			screen.push_back(toScreen(point));
		}

		// Thickness band preview for the committed chain + the rubber-band segment,
		// so the player sees the real footprint, not just a centerline.
		const auto* preset = activePreset();
		if (preset != nullptr) {
			const std::int64_t halfThick = preset->halfThicknessMm;

			auto drawBand = [&](Foundation::Vec2 start, Foundation::Vec2 end, Foundation::Color color) {
				const geometry::Ring band = geometry::band(geometry::quantize(start), geometry::quantize(end), halfThick);
				if (band.size() < 3) {
					return;
				}
				std::vector<Foundation::Vec2> bandScreen;
				bandScreen.reserve(band.size());
				for (const auto& corner : band) {
					bandScreen.push_back(toScreen(geometry::dequantize(corner)));
				}
				fillRing(bandScreen, color, {color.r, color.g, color.b, 0.0F}, 0.0F, "wall_preview_band", "", 906, 906);
			};

			const Foundation::Color bandColor{okColor.r, okColor.g, okColor.b, previewStyle.bandPreviewAlpha};
			for (std::size_t i = 0; i + 1 < points_.size(); ++i) {
				drawBand(points_[i], points_[i + 1], bandColor);
			}
			if (!points_.empty()) {
				const Foundation::Color rubberBand = valid ? Foundation::Color{okColor.r, okColor.g, okColor.b, previewStyle.bandPreviewAlpha}
												   : Foundation::Color{badColor.r, badColor.g, badColor.b, previewStyle.bandPreviewAlpha};
				drawBand(points_.back(), cursor_, rubberBand);
			}
		}

		// Centerlines between committed points.
		for (std::size_t i = 0; i + 1 < screen.size(); ++i) {
			Renderer::Primitives::drawLine({
				.start = screen[i],
				.end = screen[i + 1],
				.style = {.color = okColor, .width = previewStyle.lineWidth},
				.id = "wall_centerline",
				.key = 907,
			});
		}

		// Rubber-band centerline from the last point to the snapped cursor.
		if (!points_.empty()) {
			Renderer::Primitives::drawLine({
				.start = screen.back(),
				.end = toScreen(cursor_),
				.style = {.color = valid ? okColor : badColor, .width = previewStyle.lineWidth},
				.id = "wall_rubberband",
				.key = 908,
			});
		}

		// Above the band, the centerlines, and the vertex dots (its keys start at 911).
		drawSnapFeedback(feedbackInput(previewStyle, viewportWidth, viewportHeight, uiScale, measureFeedbackText));

		// Vertex dots. The snap-target ring (endpoint / T-junction / foundation-vertex snaps)
		// is drawSnapFeedback's, over the snapped-point marker.
		for (const auto& vertex : screen) {
			Renderer::Primitives::drawCircle({
				.center = vertex,
				.radius = previewStyle.vertexRadiusPx,
				.style = {.fill = okColor},
				.id = "wall_vertex",
				.key = 909,
			});
		}
	}

} // namespace world_sim

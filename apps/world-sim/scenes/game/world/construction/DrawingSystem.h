#pragma once

// DrawingSystem - foundation AND wall drawing tools for the construction system.
//
// Sibling of PlacementSystem (building-construction D11). Owns the structure
// tools' state machines, the SnapEngine and ConstructionValidator (both engine
// pure-logic), and the app-level ConstructionWorld topology store. Click-by-
// click polygon drawing in world meters; on close it quantizes the ring,
// commits to ConstructionWorld, and spawns the ECS blueprint entity.
//
// Two tools share the one state machine, selected by activeTool_:
//   Foundation: a CLOSED polygon. snap() / validatePoint / validateRing /
//     commitFoundation. Origin-close commits.
//   Wall:       an OPEN polyline chain on a single host foundation
//     (building-construction Walls). snapWall() / validateWallPoint /
//     validateWallSegment / commitSegment per consecutive pair. No origin-close;
//     the chain ends on right-click / Esc / Enter. The host foundation is the
//     one the FIRST point lands on; later points that leave it are rejected.
//
// Input order in GameScene is GameUI -> DrawingSystem -> PlacementSystem ->
// SelectionSystem: while drawing, this consumes clicks before placement/
// selection get them (GameUI still gets first dibs).
//
// Rendering here is INTERIM: it draws the in-progress preview AND committed
// foundations + wall bands via Primitives so the tools are verifiable end to
// end. C6 replaces committed-structure rendering with the baked element-emitter
// + progress prefix; this whole render path goes away then.

#include <construction/ConstructionValidator.h>
#include <construction/ConstructionWorld.h>
#include <construction/SnapEngine.h>

#include <ecs/EntityID.h>
#include <ecs/World.h>
#include <math/Types.h>
#include <world/camera/WorldCamera.h>

#include <functional>
#include <string>
#include <vector>

namespace engine::assets {
	struct ThicknessPreset;
	struct OpeningTypeDef;
}

namespace ecs {
	class NavigationSystem;
}

namespace world_sim {

	/// Drawing tool state. Idle when no tool is on; Drawing while collecting
	/// points. There is no separate "armed but no points" state: activation enters
	/// Drawing with an empty point list, the first click places the origin.
	enum class DrawingState {
		Idle,
		Drawing,
	};

	/// Which structure tool is active while Drawing.
	enum class ToolKind {
		Foundation,
		Wall,
		Opening,
	};

	/// Live status of the in-progress shape, surfaced to the config strip.
	struct DrawingStatus {
		bool		active = false;	 // tool is on (config strip visible)
		bool		wall = false;	 // true while the wall tool is active (config strip shows presets)
		bool		opening = false; // true while the opening tool is active (config strip shows the opening type)
		int			pointCount = 0;
		float		areaSquareMeters = 0.0F; // closed-polygon area preview (0 until >= 3 pts)
		bool		valid = true;			 // current cursor placement / shape validity
		std::string message;				 // validity reason (empty when valid)
		std::string material;				 // active material name

		// Wall readouts (meaningful only when wall == true).
		std::string thicknessPreset;			// active preset name (Light/Standard/Heavy)
		float		segmentLengthMeters = 0.0F; // length of the rubber-band segment
		float		totalLengthMeters = 0.0F;	// summed length of the committed chain so far
		float		wallCost = 0.0F;			// material cost of the chain so far
		float		wallWork = 0.0F;			// work units of the chain so far

		// Opening readouts (meaningful only when opening == true).
		std::string openingType;			   // active opening type name (Door/Window)
		float		openingWidthMeters = 0.0F; // clear width of the active opening type
	};

	class DrawingSystem {
	  public:
		struct Callbacks {
			/// Show/hide the config strip (tool active state).
			std::function<void(bool)> onToolActive;
			/// Push a toast (title, message) for commit/reject feedback.
			std::function<void(const std::string&, const std::string&)> onToast;
		};

		struct Args {
			ecs::World*					world;
			engine::world::WorldCamera* camera;
			ecs::NavigationSystem*		navigation; // world-position validity (isValidPosition); same predicate as the dev verbs
			Callbacks					callbacks;
		};

		DrawingSystem() = default;
		explicit DrawingSystem(const Args& args);

		// --- Tool lifecycle ---

		/// Activate the foundation tool (from the Build menu). Enters Drawing.
		void activateFoundationTool();

		/// Activate the wall tool (from the Build menu). Enters Drawing in wall mode.
		void activateWallTool();

		/// Activate the opening tool (from the Build menu). `openingType` is a config
		/// name ("Door"/"Window"); enters Drawing in opening mode. The material is the
		/// type's own material (v1: no separate material selector for openings).
		void activateOpeningTool(const std::string& openingType);

		/// Deactivate and discard any in-progress shape.
		void deactivate();

		[[nodiscard]] bool isActive() const { return state_ == DrawingState::Drawing; }
		[[nodiscard]] bool isWallTool() const { return activeTool_ == ToolKind::Wall; }
		[[nodiscard]] bool isOpeningTool() const { return activeTool_ == ToolKind::Opening; }

		// --- Material selection (from the config strip) ---

		void							 setActiveMaterial(const std::string& material);
		[[nodiscard]] const std::string& activeMaterial() const { return activeMaterial_; }

		/// Active wall thickness preset (Light/Standard/Heavy). The config strip
		/// sets it while the wall tool is active.
		void							 setActiveThicknessPreset(const std::string& preset) { activeThicknessPreset_ = preset; }
		[[nodiscard]] const std::string& activeThicknessPreset() const { return activeThicknessPreset_; }

		// --- Input ---

		/// Update the snapped cursor + live validity from a mouse move.
		void handleMouseMove(float screenX, float screenY, int viewportW, int viewportH, bool freeform);

		/// Left click: add a snapped point, or commit if it closes the shape
		/// (foundation) / extends the chain (wall). Ctrl held on the wall tool over a
		/// foundation edge triggers edge fill. @return true if the click was consumed.
		bool handleClick(float screenX, float screenY, int viewportW, int viewportH, bool freeform, bool ctrl = false);

		/// Esc / right-click: cancel the in-progress shape (foundation) or finish the
		/// chain (wall, committing the segments). When nothing is in progress, exits
		/// the tool. @return true if it consumed the action.
		bool cancel();

		/// Enter / double-click: finish a wall chain (commit its segments). No-op for
		/// the foundation tool (it closes on origin-click instead). @return true if
		/// it consumed the action.
		bool finishChain();

		/// Backspace: remove the last placed point. @return true if a point was removed.
		bool removeLastPoint();

		// --- Rendering (INTERIM, see file header) ---

		void render(int viewportW, int viewportH);

	  private:
		/// Render committed wall segments as trimmed bands + junction polygons
		/// (resolveWallBands), styled per segment by build progress. A segment that
		/// hosts openings is drawn as solid sub-bands around each opening's gap
		/// instead of one continuous band. INTERIM.
		void renderCommittedWalls(int viewportW, int viewportH);

		/// Render each committed opening as a procedural door/window fill in its
		/// wall-band gap, styled by build progress. INTERIM.
		void renderCommittedOpenings(int viewportW, int viewportH);

		/// Render the opening tool's ghost at the snapped position, colorized for
		/// validity. INTERIM.
		void renderOpeningGhost(int viewportW, int viewportH);

		/// Render the in-progress wall chain: centerline, thickness band preview,
		/// snap guides, and validity colorize. INTERIM.
		void renderWallChainPreview(int viewportW, int viewportH);

	  public:
		// --- Status for the config strip ---

		[[nodiscard]] DrawingStatus status() const;

		// --- Topology access for consumers (selection, ConstructionSystem) ---

		[[nodiscard]] engine::construction::ConstructionWorld&		 world() { return constructionWorld_; }
		[[nodiscard]] const engine::construction::ConstructionWorld& world() const { return constructionWorld_; }

		// --- Dev/test hook ---

		/// Commit a wall chain straight to the topology, bypassing the draw tool and
		/// the soft validator (the wall analogue of the /api/dev/foundation path).
		/// Each consecutive (pts[i], pts[i+1]) becomes a segment on `host` (0 ==
		/// freestanding); when `built`, the created segments flip to Built so they
		/// enclose rooms immediately. Reconciles segment entities once. Returns the
		/// number of segments created.
		/// Precondition: callers must nav-validate the chain first (the dev verb does
		/// via requireWalkableChain); this bypasses the on-mesh check.
		int devCommitWalls(
			const std::vector<Foundation::Vec2>& pts,
			const std::string&					 material,
			const std::string&					 thicknessPreset,
			engine::construction::FoundationId	 host,
			bool								 built
		);

		/// Commit an opening straight to the topology and spawn its mirror entity,
		/// bypassing the draw tool and the soft validator (the opening analogue of the
		/// /api/dev/foundation path). Places `openingType` on `segment` at parameter t
		/// (clamped in addOpening). When `built`, the spawned entity mirrors a complete
		/// opening (full work, manifest delivered) and the topology flips to Built.
		/// Returns the created opening id, or kInvalidOpening on failure (unknown
		/// segment / type).
		engine::construction::OpeningId
		devCommitOpening(engine::construction::SegmentId segment, float t, const std::string& openingType, bool built);

	  private:
		// --- Nav-mesh placement validity (shared by foundation + wall + opening) ---

		/// Whether `p` (world meters) is a placeable point for the ACTIVE tool: a FOUNDATION vertex is
		/// placeable over clearable entities too (NavigationSystem::isPointBuildable, terrain-only), a
		/// WALL vertex needs clear on-mesh ground (isValidPosition). Same split the /api/dev foundation
		/// vs walls verbs now use; validity stays owned by a nav mesh (no world/terrain/water reads).
		/// When no nav system is wired (headless/test) validity is owned elsewhere; mirror the dev
		/// verbs' permissive fallback and treat the point as placeable.
		[[nodiscard]] bool pointOnMesh(Foundation::Vec2 p) const;

		/// True when the WHOLE footprint of `pts` may be placed. A FOUNDATION validates its polygon
		/// area against NavigationSystem::isAreaBuildable -- a terrain-only mesh, so geography (water)
		/// and built walls block but clearable entities (trees/rocks) do NOT: placing over them spawns
		/// clear tasks and the build waits for the footprint to clear. A WALL validates its chain
		/// centerline against isPolylineWalkable (no wall footprint-clearing yet, so it still needs
		/// clear ground). The SAME shared predicates the /api/dev verbs use. On failure toasts
		/// "Can't build here" and returns false so the caller commits NOTHING -- no partial structure.
		[[nodiscard]] bool requirePlaceable(const std::vector<Foundation::Vec2>& pts, const char* what);

		// --- Foundation tool ---

		/// Quantize, commit, and spawn the foundation blueprint entity. Surfaces
		/// failures as a toast. Returns to an empty Drawing state on success.
		void commitShape();

		/// Screen pixels per world meter at the current zoom (mirrors the render path:
		/// kPixelsPerMeter * camera zoom). 1.0 fallback if no camera.
		[[nodiscard]] float pixelsPerWorldMeter() const;

		/// Origin-close catch radius in world meters, floored so it never shrinks below
		/// the on-screen origin halo: what you can click to close matches what you see,
		/// at any zoom. Drives both the snap() override and (x scale) the halo radius.
		[[nodiscard]] float effectiveOriginCloseRadiusMeters() const;

		/// Radius (world meters) around the start vertex within which a click that would
		/// otherwise be rejected closes the shape instead (the "snap not block" rescue).
		/// Covers the non-adjacent edge-clearance dead-zone; never below the close radius.
		[[nodiscard]] float closeRescueRadiusMeters() const;

		/// Whether `p` (world meters) is within `radiusMeters` of the first placed point.
		/// False when no points are placed.
		[[nodiscard]] bool nearStartVertex(Foundation::Vec2 p, float radiusMeters) const;

		/// Build the ECS blueprint entity for a committed foundation.
		ecs::EntityID spawnBlueprintEntity(engine::construction::FoundationId id);

		// --- Wall tool ---

		/// Mouse-move / click handling specialized for the wall chain (snapWall +
		/// validateWallPoint). Returns whether the click was consumed.
		void handleWallMove(Foundation::Vec2 world, bool freeform);
		bool handleWallClick(Foundation::Vec2 world, bool freeform, bool ctrl);

		/// Resolve the active thickness preset for the active material, or nullptr if
		/// the material has no wall presets (the wall tool can't commit then).
		[[nodiscard]] const engine::assets::ThicknessPreset* activePreset() const;

		/// Commit every consecutive pair of the in-progress chain as a wall segment
		/// (validateWallSegment then commitSegment) and spawn a blueprint entity per
		/// segment. Clears the chain. Host is wallHost_.
		void commitWallChain();

		/// Ctrl+click edge fill: instantly commit a wall blueprint along the entire
		/// foundation edge nearest `world`. Returns true if it placed a segment.
		bool tryEdgeFill(Foundation::Vec2 world);

		/// Spawn the ECS mirror entity for a single wall segment, sized to THAT
		/// segment's own length (its v0/v1 positions) x its thickness preset x its
		/// material rate. Reads material / preset / state from the segment record,
		/// not the active tool settings, so split halves of a differently-materialed
		/// wall are priced correctly. A Built segment spawns Complete (full work,
		/// delivered == required); a Blueprint spawns AwaitingMaterials. Sets the
		/// segment's entity handle. No-op if the id is unknown or already has an
		/// entity.
		ecs::EntityID spawnWallSegmentEntity(engine::construction::SegmentId segmentId);

		/// Idempotently make the ECS mirror match the wall topology: spawn an entity
		/// for every segment that lacks one (sized to itself), and destroy any wall
		/// entity whose segment id no longer exists (orphaned by a T-junction split).
		/// Called after every wall commit / edge fill so multi-segment commits and
		/// splits leave no segment entity-less and no entity leaked.
		void reconcileSegmentEntities();

		// --- Opening tool ---

		/// Mouse-move handling for the opening tool: snapOpening onto the nearest built
		/// wall, store the snap, and (when the snap is valid) run validateOpening for the
		/// live validity readout.
		void handleOpeningMove(Foundation::Vec2 world);

		/// Click handling for the opening tool: commit the opening at the current snap if
		/// valid, spawn its blueprint entity, and stay in the tool. Returns whether the
		/// click was consumed (always true while the tool is active).
		bool handleOpeningClick(Foundation::Vec2 world);

		/// Resolve the active opening type's config def, or nullptr if the type name is
		/// not a known opening type (the opening tool can't snap/commit then).
		[[nodiscard]] const engine::assets::OpeningTypeDef* activeOpeningType() const;

		/// Spawn the ECS mirror entity for a single opening (Position at the centerline
		/// point at t, Structure{Opening, openingId}, StructureBlueprint with the type's
		/// constant manifest/work, Inventory, StructureHealth). A `built` opening spawns
		/// Complete (full work, delivered == required); otherwise AwaitingMaterials
		/// (openings have no clearing phase, like walls). Sets the opening's entity
		/// handle. No-op if the id is unknown or already has an entity.
		ecs::EntityID spawnOpeningBlueprintEntity(engine::construction::OpeningId openingId, bool built);

		ecs::World*					ecsWorld_ = nullptr;
		engine::world::WorldCamera* camera_ = nullptr;
		ecs::NavigationSystem*		navigation_ = nullptr;
		Callbacks					callbacks_;

		engine::construction::ConstructionWorld constructionWorld_;

		DrawingState						   state_ = DrawingState::Idle;
		ToolKind							   activeTool_ = ToolKind::Foundation;
		std::vector<Foundation::Vec2>		   points_;				// placed vertices, world meters
		Foundation::Vec2					   cursor_{0.0F, 0.0F}; // snapped cursor
		engine::construction::SnapResult	   lastSnap_;
		engine::construction::ValidationResult lastValidation_;

		// Foundation tool: true when the next click will CLOSE the shape rather than
		// place a vertex -- either the explicit origin-close (lastSnap_ Origin) or the
		// near-start "snap not block" rescue (a would-be-rejected vertex near the start
		// whose closed ring is valid). Recomputed every move; drives the closing-halo
		// feedback and suppresses the invalid highlight.
		bool willClose_ = false;

		// True when the snapped cursor is at a NON-PLACEABLE point for the active tool (foundation:
		// unbuildable -- water/wall; wall: off the walkable mesh; or no active mesh). Recomputed every
		// move for the foundation + wall tools; drives the red invalid preview so the player sees they
		// can't place there BEFORE committing. The commit gate re-checks every vertex regardless, so
		// this is feedback only.
		bool cursorOffMesh_ = false;

		// Wall chain host: the foundation the first chain point landed on. Every
		// segment of the chain hosts here; a later point leaving it is rejected.
		engine::construction::FoundationId wallHost_ = engine::construction::kInvalidFoundation;

		// Opening tool: the active opening type (config name) plus the latest snap +
		// validity, refreshed on every move and read on click / by status().
		std::string							   activeOpeningType_ = "Door";
		engine::construction::OpeningSnap	   openingSnap_;
		engine::construction::ValidationResult openingValidation_;

		std::string activeMaterial_ = "Wood";
		std::string activeThicknessPreset_ = "Standard";

		static constexpr float kPixelsPerMeter = 8.0F; // match GameScene
	};

} // namespace world_sim

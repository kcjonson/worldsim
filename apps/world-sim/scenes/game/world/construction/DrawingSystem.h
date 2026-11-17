#pragma once

// DrawingSystem - foundation AND wall drawing tools for the construction system.
//
// Sibling of PlacementSystem (building-construction D11). Owns the structure
// tools' state machines, the SnapEngine and ConstructionValidator (both engine
// pure-logic), and the app-level ConstructionWorld topology store. Click-by-
// click polygon drawing in world meters; on close it quantizes the ring,
// commits to ConstructionWorld, and spawns the ECS blueprint entity.
//
// The tools share the one state machine, selected by activeTool_:
//   Foundation: a CLOSED polygon. snap() / validatePoint / validateRing /
//     commitFoundation. Origin-close commits.
//   FoundationRectangle: two opposite corners, by two clicks or a press-drag-release.
//     snapRectangleCorner / rectangleFromCorners, then the polygon's close checks and
//     commit on the rectangle's ring.
//   FoundationEdit: Add / Subtract on one existing foundation (Epic G3). The
//     same closed-polygon flow; validateEditPoint per point, and the close applies
//     the edit with the validateFoundationEdit result the move already holds.
//   Wall:       a polyline chain on a single host foundation
//     (building-construction Walls). snapWall() / validateWallPoint /
//     validateWallSegment / commitSegment per consecutive pair. The chain ends
//     on right-click / Esc / Enter, or on a point that lands on its first point
//     (closesWallChain), which closes the loop. The host foundation is the
//     one the FIRST point lands on; later points that leave it are rejected.
//
// Input order in GameScene is GameUI -> DrawingSystem -> PlacementSystem ->
// SelectionSystem: while drawing, this consumes clicks before placement/
// selection get them (GameUI still gets first dibs).
//
// Rendering here is INTERIM: it draws the in-progress preview AND committed
// foundations + wall bands via Primitives so the tools are verifiable end to
// end. C6 replaces committed-structure rendering with the baked element-emitter
// + progress prefix; this whole render path goes away then. The two halves
// draw in different world layers (see world-depth-sorting.md): renderCommitted
// is the flat ground sub-layer between terrain and the entity pass, so every
// entity paints over a building; renderPreview rides above the entities with
// the other cursor ghosts.

#include "CommittedGeometryCache.h"
#include "DrawingFeedback.h"

#include <construction/ConstructionValidator.h>
#include <construction/ConstructionWorld.h>
#include <construction/SnapEngine.h>

#include <ecs/EntityID.h>
#include <ecs/World.h>
#include <graphics/Color.h>
#include <math/Types.h>
#include <world/camera/WorldCamera.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace assets {
	struct ThicknessPreset;
	struct OpeningTypeDef;
	struct PreviewStyle;
} // namespace assets

namespace ecs {
	class NavigationSystem;
	class PlayerVisibilitySystem;
	struct StructureBlueprint;
} // namespace ecs

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
		Foundation,			 // the Freeform tool: a polygon, point by point
		FoundationRectangle, // the Rectangle tool: two opposite corners on the grid (D13)
		FoundationEdit, // Add / Subtract on an existing foundation (Epic G3)
		Wall,
		Opening,
	};

	/// The two foundation tools, between which the Build menu's Foundation entry and the
	/// config strip's tool picker choose.
	enum class FoundationTool {
		Rectangle,
		Freeform,
	};

	/// Live status of the in-progress shape, surfaced to the config strip.
	struct DrawingStatus {
		bool						  active = false;		  // tool is on (config strip visible)
		bool						  wall = false;			  // true while the wall tool is active (config strip shows presets)
		bool						  opening = false;		  // true while the opening tool is active (config strip shows the opening type)
		bool						  foundationEdit = false; // true while Add / Subtract is active (material locked to the target's)
		std::optional<FoundationTool> foundationTool;		  // the Rectangle or Freeform tool, while one is active (the strip's tool picker)
		std::string					  editLabel;			  // "Add to foundation" / "Subtract from foundation" (foundationEdit only)
		int							  pointCount = 0;
		float		areaSquareMeters = 0.0F;	  // the shape's area: the closed polygon (0 until >= 3 pts), the pending rectangle, an edit's outline
		float		areaDeltaSquareMeters = 0.0F; // edit: resulting area minus the target's current area
		bool		valid = true;				  // current cursor placement / shape validity
		std::string message;					  // validity reason (empty when valid)
		std::string material;					  // active material name

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

		/// Result of a foundation Add / Subtract, from the tool or the dev verb.
		/// `extension` is the new extension blueprint's id for an Add onto a Built
		/// foundation (0 otherwise); `entity` is the blueprint entity that now carries
		/// the work (the resized target, or the extension).
		struct FoundationEditOutcome {
			bool							   ok = false;
			std::string						   reason; // empty when ok
			construction::FoundationId		   target = construction::kInvalidFoundation;
			construction::FoundationId		   extension = construction::kInvalidFoundation;
			ecs::EntityID					   entity = ecs::kInvalidEntity;
			float							   areaSquareMeters = 0.0F; // the target's outline once the edit lands
		};

		struct Args {
			ecs::World*					world;
			world::WorldCamera*			camera;
			ecs::NavigationSystem*		navigation; // the ground gates (BuildableOnceCleared for foundations, BuildableNow for walls); the dev verbs' too
			// The preview's colors where a shape can and can't go, and a matched length's
			// label, from the scene's theme.
			Foundation::Color validColor{};
			Foundation::Color invalidColor{};
			Foundation::Color accentColor{};
			Callbacks		  callbacks;
		};

		DrawingSystem() = default;
		explicit DrawingSystem(const Args& args);

		// --- Tool lifecycle ---

		/// Activate the Freeform foundation tool, the polygon point by point. Enters Drawing
		/// and makes it the session's foundation pick.
		void activateFoundationTool();

		/// Activate the Rectangle foundation tool. Enters Drawing and makes it the session's
		/// foundation pick.
		void activateRectangleTool();

		/// The foundation tool last activated this session, Rectangle before either was:
		/// what the Build menu's Foundation entry opens.
		[[nodiscard]] FoundationTool lastFoundationTool() const { return lastFoundationTool_; }

		/// The Build menu's Foundation entry and the config strip's tool picker: activate
		/// `tool`, dropping any other tool's shape in progress. With `tool` already on, nothing
		/// changes and its shape in progress stays.
		void pickFoundationTool(FoundationTool tool);

		/// Activate the wall tool (from the Build menu). Enters Drawing in wall mode.
		void activateWallTool();

		/// Activate the opening tool (from the Build menu). `openingType` is a config
		/// name ("Door"/"Window"); enters Drawing in opening mode. The material is the
		/// type's own material (v1: no separate material selector for openings).
		void activateOpeningTool(const std::string& openingType);

		/// Activate foundation Add / Subtract on `target` (from its info panel). The
		/// same click-by-click polygon flow as the foundation tool; closing the drawn
		/// polygon applies the edit and the tool stays on for the next one.
		void activateFoundationEditTool(construction::FoundationId target, construction::FoundationEditMode mode);

		/// Deactivate and discard any in-progress shape.
		void deactivate();

		/// How many times a mouse move has run the snap and the point validation rather
		/// than reusing the last frame's results. Tests read it to check the memo.
		[[nodiscard]] std::uint64_t snapEvaluationCount() const { return snapEvaluationCount_; }

		[[nodiscard]] bool isActive() const { return state_ == DrawingState::Drawing; }
		/// The tool on, while one is.
		[[nodiscard]] std::optional<ToolKind> activeTool() const { return isActive() ? std::optional<ToolKind>(activeTool_) : std::nullopt; }
		[[nodiscard]] bool isWallTool() const { return activeTool_ == ToolKind::Wall; }
		[[nodiscard]] bool isRectangleTool() const { return isActive() && activeTool_ == ToolKind::FoundationRectangle; }
		[[nodiscard]] bool isOpeningTool() const { return activeTool_ == ToolKind::Opening; }

		// --- Material selection (from the config strip) ---

		void							 setActiveMaterial(const std::string& material);
		[[nodiscard]] const std::string& activeMaterial() const { return activeMaterial_; }

		/// Active wall thickness preset (Light/Standard/Heavy). The config strip
		/// sets it while the wall tool is active.
		void							 setActiveThicknessPreset(const std::string& preset) { activeThicknessPreset_ = preset; }
		[[nodiscard]] const std::string& activeThicknessPreset() const { return activeThicknessPreset_; }

		// --- Input ---

		/// Update the snapped cursor + live validity from a mouse move. Resumes a paused tool.
		void handleMouseMove(float screenX, float screenY, int viewportW, int viewportH, bool freeform);

		/// The pointer is not with the tool (outside the window, or the window without the
		/// focus): forget everything it proposed, so nothing follows it and the status strip
		/// refuses nothing, until the next handleMouseMove. Placed points, a waiting first
		/// corner, and a pending press stay. A tool starts paused, until its first move.
		void pause();

		/// Left click, on its release: add a snapped point, or commit if it closes the shape
		/// (foundation) / extends or closes the chain (wall). Ctrl held on the wall tool over a
		/// foundation edge triggers edge fill. In the Rectangle tool it ends the press
		/// handlePress began: a release more than doubleClickDistancePx from that press, or
		/// any release once a first corner was waiting before it, commits the rectangle when
		/// the two corners make one; a release that ends no press the tool saw does nothing.
		/// @return true if the click was consumed.
		bool handleClick(float screenX, float screenY, int viewportW, int viewportH, bool freeform, bool ctrl = false);

		/// Rectangle tool, left press on the world: places the first corner when none is
		/// waiting, and remembers where the press was for the release. @return true if the
		/// press was consumed (the Rectangle tool is active).
		bool handlePress(float screenX, float screenY, int viewportW, int viewportH, bool freeform);

		/// Rectangle tool: a press the release of which will never reach the world (released
		/// over the interface or outside the window, or lost with the window's focus). Forgets
		/// the press and commits nothing; a first corner, the press's own included, waits for
		/// a later click.
		void cancelPress();
		[[nodiscard]] bool hasPendingPress() const { return rectanglePress_.has_value(); }

		/// Esc / right-click: cancel the in-progress shape (foundation) or finish the
		/// chain (wall, committing the segments). When nothing is in progress, exits
		/// the tool. @return true if it consumed the action.
		bool cancel();

		/// Enter / double-click: finish a wall chain (commit its segments); with one point or
		/// none, the chain is dropped and nothing is committed or toasted. No-op for the
		/// foundation tool (it closes on origin-click instead). @return true if it consumed
		/// the action.
		bool finishChain();

		/// Backspace: remove the last placed point. @return true if a point was removed.
		bool removeLastPoint();

		// --- Rendering (INTERIM, see file header) ---

		/// Committed construction (foundations, walls, openings). Called between
		/// the terrain and entity passes: the flat ground sub-layer, below
		/// groundcover and every Y-sorted upright. Geometry comes from the
		/// version-keyed CommittedGeometryCache and is culled to the viewport, so
		/// per-frame cost scales with what is VISIBLE, not with the whole world.
		void renderCommitted(int viewportW, int viewportH);

		/// In-progress drawing feedback (preview polygon/chain, snap guides,
		/// opening ghost). Called after the entity pass, alongside the placement
		/// ghost, below UI. Its text is sized in ui px, `uiScale` logical px each.
		void renderPreview(int viewportWidth, int viewportHeight, float uiScale);

	  private:
		/// Draw the cached wall bands + junction polygons that intersect
		/// `visibleWorld`, styled per segment by build progress. INTERIM.
		void renderCommittedWalls(const Foundation::Rect& visibleWorld, int viewportW, int viewportH);

		/// Draw each cached opening footprint that intersects `visibleWorld` as a
		/// procedural door/window fill, styled by build progress. INTERIM.
		void renderCommittedOpenings(const Foundation::Rect& visibleWorld, int viewportW, int viewportH);

		/// Build progress in [0,1] for a committed record: Built renders full, a
		/// blueprint reads workDone/workTotal off its ECS mirror (0 when unspawned).
		[[nodiscard]] float blueprintProgress(bool built, ecs::EntityID entity) const;

		/// Render the opening tool's ghost at the snapped position, colorized for
		/// validity. INTERIM.
		void renderOpeningGhost(int viewportW, int viewportH);

		/// Render the in-progress wall chain: centerline, thickness band preview,
		/// snap guides, and validity colorize. INTERIM.
		void renderWallChainPreview(int viewportWidth, int viewportHeight, float uiScale);

		/// Foundation Add / Subtract: the outline the edit would produce (merged or
		/// remainder), colorized for validity, with the reason next to the cursor.
		/// Drawn under the regular polygon preview. INTERIM.
		void renderEditOutlinePreview(int viewportWidth, int viewportHeight, float uiScale);

		/// The snap feedback (drawSnapFeedback), while the tool follows the pointer.
		void drawPointerFeedback(int viewportWidth, int viewportHeight, float uiScale) const;

	  public:
		/// A rejected edit's reason, drawn beside the cursor, where, and how large.
		struct EditReason {
			std::string		 text;
			Foundation::Vec2 position;	// the text's top-left, screen pixels
			float			 textScale; // the renderer's text scale
			FeedbackBox		 box;		// the text and its shadow, screen pixels: the snap labels keep off it
		};

		/// The reason text renderEditOutlinePreview draws, while the outline the edit
		/// would produce is invalid, sized and placed in ui px of `uiScale` logical px and
		/// measured with `measure`.
		[[nodiscard]] std::optional<EditReason>
		editReason(int viewportWidth, int viewportHeight, float uiScale, const MeasureTextSize& measure) const;

		/// The snap feedback's input for the current frame, its text measured with `measure`.
		[[nodiscard]] DrawingFeedbackInput feedbackInput(
			const assets::PreviewStyle& style, int viewportWidth, int viewportHeight, float uiScale, const MeasureTextSize& measure
		) const;

		// --- Status for the config strip ---

		[[nodiscard]] DrawingStatus status() const;

		/// What the preview shows this frame: whether the band and fill read valid, and
		/// whether the close halo draws. While the next click closes, both come from
		/// closeVerdict() as of the last move: a halo means the checks the click runs before
		/// committing pass (the commit itself can still be refused, with a toast, and the
		/// picture can lag a world change by a frame). Otherwise both come from the point the
		/// click would place. The snapped-point marker's red dot, and an Add / Subtract's
		/// outline color and reason text, follow the same verdict.
		struct PreviewVerdict {
			bool		valid = true;
			bool		closeHalo = false;
			bool		markerInvalid = false;	 // the snapped-point marker's dot takes the invalid color
			bool		editOutlineValid = true; // the outline an Add / Subtract would produce
			std::string editReason;				 // why that edit is refused, drawn beside the pointer; empty when it isn't
		};
		[[nodiscard]] PreviewVerdict previewVerdict() const;

		/// What renderPreview draws that follows the pointer, all of it gone while the tool is
		/// paused: the rubber band (and the wall's band) to the snapped point with the snap
		/// feedback, an Add or Subtract's trial outline of the foundation it would make with its
		/// reason, and the opening tool's ghost on the wall it snapped to.
		struct PointerPreview {
			bool snapFeedback = false;
			bool trialOutline = false;
			bool openingGhost = false;
		};
		[[nodiscard]] PointerPreview pointerPreview() const;

		// --- Topology access for consumers (selection, ConstructionSystem) ---

		[[nodiscard]] construction::ConstructionWorld&		 world() { return constructionWorld_; }
		[[nodiscard]] const construction::ConstructionWorld& world() const { return constructionWorld_; }

		// --- Dev/test hook ---

		/// Commit a wall chain straight to the topology, bypassing the draw tool and
		/// the soft validator (the wall analogue of the /api/dev/foundation path).
		/// Each consecutive (pts[i], pts[i+1]) becomes a segment on `host` (0 ==
		/// freestanding); when `built`, the created segments flip to Built so they
		/// enclose rooms immediately. Reconciles segment entities once. Returns the
		/// number of segments created.
		/// Precondition: callers must nav-validate the chain first (the dev verb does
		/// via wallChainGroundRefusal); this bypasses the on-mesh check.
		int devCommitWalls(
			const std::vector<Foundation::Vec2>& pts,
			const std::string&					 material,
			const std::string&					 thicknessPreset,
			construction::FoundationId			 host,
			bool								 built
		);

		/// Commit an opening straight to the topology and spawn its mirror entity,
		/// bypassing the draw tool and the soft validator (the opening analogue of the
		/// /api/dev/foundation path). Places `openingType` on `segment` at parameter t
		/// (clamped in addOpening). When `built`, the spawned entity mirrors a complete
		/// opening (full work, manifest delivered) and the topology flips to Built.
		/// Returns the created opening id, or kInvalidOpening on failure (unknown
		/// segment / type).
		construction::OpeningId devCommitOpening(construction::SegmentId segment, float t, const std::string& openingType, bool built);

		// --- Foundation lifecycle (shared with GameScene and the dev verbs) ---

		/// Spawn the ECS blueprint entity for committed foundation `id`, sized from its
		/// own ring and material (manifest, work, HP). kInvalidEntity for an unknown id.
		ecs::EntityID spawnBlueprintEntity(construction::FoundationId id);

		/// Validate and apply an Add / Subtract of the closed polygon `drawn` (world
		/// meters) to `target`. The one edit path: the tool's close and
		/// /api/dev/foundation-edit both land here. An editable blueprint is edited in
		/// place (manifest + work resized, back to Clearing); an Add onto a Built
		/// foundation commits an extension blueprint that merges once built.
		FoundationEditOutcome applyFoundationEdit(
			construction::FoundationId			 target,
			const std::vector<Foundation::Vec2>& drawn,
			construction::FoundationEditMode	 mode
		);

		/// Merge a just-Built extension into its target: the topology union, and the
		/// target entity takes over the extension's manifest, work, and HP and
		/// re-centers on the merged outline. Returns the extension's entity for the
		/// caller to despawn (deferred), or kInvalidEntity when the merge is refused.
		ecs::EntityID mergeExtension(construction::FoundationId extension);

		/// Foundation `id`'s ECS mirror blueprint (deliveries, work, demolish order),
		/// or nullptr when it has no live entity. The edit validator's ECS input.
		[[nodiscard]] const ecs::StructureBlueprint* foundationBlueprint(construction::FoundationId id) const;

	  private:
		// --- Nav-mesh placement validity (shared by foundation + wall + opening) ---

		/// Whether `p` (world meters) is a placeable point for the ACTIVE tool: a FOUNDATION vertex is
		/// placeable over clearable entities too (NavigationSystem::isPointBuildableOnceCleared, terrain-only), a
		/// WALL vertex needs clear on-mesh ground out of any ford (isPointBuildableNow). Same split the /api/dev foundation
		/// vs walls verbs now use; validity stays owned by a nav mesh (no world/terrain/water reads).
		/// When no nav system is wired (headless/test) validity is owned elsewhere; mirror the dev
		/// verbs' permissive fallback and treat the point as placeable.
		[[nodiscard]] bool pointOnBuildableGround(Foundation::Vec2 p) const;

		/// True when the WHOLE `footprint` may be placed (placementRefusal is empty). On
		/// failure toasts "Can't build here" and returns false so the caller commits NOTHING --
		/// no partial structure.
		[[nodiscard]] bool requirePlaceable(const std::vector<Foundation::Vec2>& footprint, const char* what);

		/// Why the WHOLE `footprint` may not be placed, or empty when it may. A FOUNDATION
		/// validates its polygon area against footprintBuildable. A WALL validates its chain
		/// centerline against isLineBuildableNow (no wall footprint-clearing yet, so it still needs
		/// clear ground). The SAME shared predicates the /api/dev verbs use.
		[[nodiscard]] std::string placementRefusal(const std::vector<Foundation::Vec2>& footprint) const;

		/// Player visibility (player-visibility.md 3.7): the tools build only on ground the colonists have
		/// seen, and each of these is true when player visibility is off. A point; a closed shape's whole
		/// area; an open chain's points and the strip along each segment; the snapped opening's span.
		[[nodiscard]] bool pointSeen(Foundation::Vec2 p) const;
		[[nodiscard]] bool areaSeen(const std::vector<Foundation::Vec2>& pts) const;
		[[nodiscard]] bool chainSeen(const std::vector<Foundation::Vec2>& pts) const;
		[[nodiscard]] bool openingSeen(float widthMeters) const;
		[[nodiscard]] const ecs::PlayerVisibilitySystem* visibilityGate() const;
		/// "not on buildable ground", or "the area is unexplored" when the cursor is over ground nobody has seen.
		[[nodiscard]] std::string cursorGroundReason() const;

		/// NavigationSystem::isAreaBuildableOnceCleared over the polygon `pts`: a terrain-only mesh, so water and
		/// off-mesh gaps block but clearable entities (trees/rocks) do NOT (placing over them spawns
		/// clear tasks and the build waits for the footprint to clear). Wall conflicts are the
		/// validator's job. True when no nav system is wired (headless/test).
		[[nodiscard]] bool footprintBuildable(const std::vector<Foundation::Vec2>& pts) const;

		// --- Foundation tool ---

		/// Quantize, commit, and spawn the foundation blueprint entity for closingShape().
		/// Surfaces failures as a toast. Returns to an empty Drawing state on success.
		void commitShape();

		/// The closed shape the next closing click commits: the placed points, or in the
		/// Rectangle tool the pending rectangle's corners (empty when the two corners make none).
		[[nodiscard]] const std::vector<Foundation::Vec2>& closingShape() const;

		/// Enter Drawing with `tool` and nothing placed, and show the config strip.
		void startTool(ToolKind tool);

		/// The Rectangle or Freeform tool while one is on.
		[[nodiscard]] std::optional<FoundationTool> activeFoundationTool() const;

		// --- Rectangle tool ---

		/// The corner snap (snapRectangleCorner) and, with a first corner placed, the rectangle the
		/// two corners span, judged as the closing click would judge it. Level corners span none.
		void handleRectangleMove(Foundation::Vec2 world, bool freeform);

		/// The pending rectangle's fill and outline, the first corner, and planSnapFeedback's plan
		/// (the size label, the grid cross, the existing corner's or edge's ring, and the
		/// snapped-point marker). INTERIM.
		void renderRectanglePreview(int viewportWidth, int viewportHeight, float uiScale);

		/// Forget the pending rectangle and any press (the first corner is points_, cleared by the caller).
		void clearRectangle();

		/// What the next click would do, as the preview, the status strip, and the click read
		/// it: while it closes (or in the Rectangle tool commits), the close's verdict;
		/// otherwise whether the point may be placed, and why not. The Freeform tool's pointer
		/// resting where it placed the last point, with its snapped point refused as too close
		/// to it, proposes nothing: valid, and the click places nothing. A paused tool proposes
		/// nothing either.
		struct ClickVerdict {
			bool		valid = true;
			bool		proposed = true;
			std::string reason;
		};
		[[nodiscard]] ClickVerdict clickVerdict() const;

		/// The Freeform tool's pointer has not moved in the world since the click that placed the
		/// last point. Its snapped point need not be that point: a length that rounds to zero
		/// leaves the raw pointer, a few centimeters off.
		[[nodiscard]] bool restsOnLastPoint() const;

		/// Screen pixels per world meter at the current zoom (mirrors the render path:
		/// kPixelsPerMeter * camera zoom). 1.0 fallback if no camera.
		[[nodiscard]] float pixelsPerWorldMeter() const;

		// --- Snap memo ---

		/// Every input the snap and the point validation read, except the placed points
		/// (compared against snapMemoPoints_ so a frame builds no vector). The config
		/// they also read is loaded once per game and never changes under the tool.
		struct SnapMemoKey {
			ToolKind							 tool = ToolKind::Foundation;
			Foundation::Vec2					 cursorWorld{0.0F, 0.0F};
			construction::DrawingAids			 aids;
			std::uint64_t						 worldVersion = 0;
			bool								 freeform = false;
			float								 zoom = 0.0F; // feeds the origin-close radius
			const assets::ThicknessPreset*		 preset = nullptr; // the wall's thickness, from the material and preset name
			construction::FoundationId			 wallHost = construction::kInvalidFoundation;
			construction::FoundationId			 editTarget = construction::kInvalidFoundation;

			bool operator==(const SnapMemoKey&) const = default;
		};

		[[nodiscard]] SnapMemoKey snapMemoKey(Foundation::Vec2 cursorWorld, bool freeform, const assets::ThicknessPreset* preset) const;

		/// True when the last evaluation was for `key` and the current points, so its
		/// results still hold. Otherwise records `key` and the points as the inputs about
		/// to be evaluated and returns false.
		bool reuseSnapMemo(const SnapMemoKey& key);

		/// Origin-close catch radius in world meters, floored to `originHaloMinRadiusPx` on
		/// screen so it stays reachable at every zoom. Drives the snap() override and the
		/// dotted close ring, drawn at this radius so it shows the catch exactly; the close
		/// halo (DrawingFeedback) has its own floor.
		[[nodiscard]] float effectiveOriginCloseRadiusMeters() const;

		/// Whether a close of the placed points passes, or why not: a toast's title and its
		/// reason. Foundation and Add / Subtract: the checks the closing click runs before it
		/// commits (commitShape, commitFoundationEdit), refused here exactly when the click
		/// refuses. Wall: the chain with the closing point added, accepted only when its ground
		/// passes and every segment passes the check commitWallChain gives it, so a refusal
		/// means not all of the chain would be built; the wall click does not run this, and
		/// still builds the segments that pass. The preview, the marker, the status strip, and
		/// the edit's outline and reason read it while the next click closes. Reads the move
		/// that just ran (its snap, and for an edit editPreview_), so call it after one.
		struct CloseVerdict {
			bool		accepted = true;
			std::string title;
			std::string reason;
		};
		[[nodiscard]] CloseVerdict closeVerdict();
		[[nodiscard]] static CloseVerdict refusedClose(std::string title, std::string reason);

		/// The part of closeVerdict() that reads only what the snap memo's key covers (the
		/// points, the snapped point, the world's version, the wall's preset and host), kept
		/// with the snap while the memo holds. A foundation's ring validation; the wall
		/// chain's segment checks. Edits have none: theirs reads the target's blueprint.
		[[nodiscard]] const CloseVerdict& closedShapeCheck();

		/// The wall chain's segments, committed into `into` the way commitWallChain commits
		/// them: each consecutive pair through validateWallSegment, then commitSegment,
		/// skipping a pair that fails either. commitWallChain runs it on the construction
		/// world; the close verdict runs it on a copy, to learn whether all of it would build.
		struct WallChainCommit {
			int			committed = 0;	 // segments created, a split counting each part
			int			rejected = 0;	 // pairs skipped
			std::string firstRefusal; // why the first skipped pair was, empty when none was
		};
		[[nodiscard]] WallChainCommit
		commitWallSegments(construction::ConstructionWorld& into, const std::vector<Foundation::Vec2>& chain, const assets::ThicknessPreset& preset) const;

		/// Radius (world meters) around the start vertex within which a click that would
		/// otherwise be rejected closes the shape instead (the "snap not block" rescue).
		/// Covers the non-adjacent edge-clearance dead-zone; never below the close radius.
		[[nodiscard]] float closeRescueRadiusMeters() const;

		/// Whether `p` (world meters) is within `radiusMeters` of the first placed point.
		/// False when no points are placed.
		[[nodiscard]] bool nearStartVertex(Foundation::Vec2 p, float radiusMeters) const;

		// --- Foundation Add / Subtract tool ---

		/// validateFoundationEdit of the drawn polygon `drawn` on the active target.
		[[nodiscard]] construction::FoundationEditResult validateDrawnEdit(const std::vector<Foundation::Vec2>& drawn) const;

		/// Refresh editPreview_ from the drawn polygon the next click would produce
		/// (placed points, plus the cursor unless the click closes). `closedEdit`, when the
		/// move already validated the placed points alone, is that result, taken while closing.
		void refreshEditPreview(std::optional<construction::FoundationEditResult> closedEdit);

		/// Apply an edit already validated as `result` (validateFoundationEdit of `drawn`),
		/// refusing it when editRefusal does. applyFoundationEdit validates, then lands here.
		FoundationEditOutcome applyValidatedFoundationEdit(
			construction::FoundationId			 target,
			const std::vector<Foundation::Vec2>& drawn,
			construction::FoundationEditMode	 mode,
			const construction::FoundationEditResult& result
		);

		/// Why the edit `result` (validateFoundationEdit's) would be refused before it
		/// commits, or empty: its validation, then for an Add the ground it builds on.
		[[nodiscard]] std::string editRefusal(const construction::FoundationEditResult& result, construction::FoundationEditMode mode) const;

		/// Apply the edit for the closed polygon points_, toast the outcome, and clear
		/// the points (the tool stays on).
		void commitFoundationEdit();

		/// Re-size an in-place-edited blueprint's mirror entity to its new ring:
		/// manifest, work, HP, and position, back to Clearing (the new footprint may
		/// cover blockers the old one didn't), with its goal tree dropped for the next
		/// construction tick to rebuild from the new footprint.
		void resizeBlueprintEntity(construction::FoundationId id);

		// --- Wall tool ---

		/// Mouse-move / click handling specialized for the wall chain (snapWall +
		/// validateWallPoint). Returns whether the click was consumed.
		void handleWallMove(Foundation::Vec2 world, bool freeform);
		bool handleWallClick(Foundation::Vec2 world, bool freeform, bool ctrl);

		/// Resolve the active thickness preset for the active material, or nullptr if
		/// the material has no wall presets (the wall tool can't commit then).
		[[nodiscard]] const assets::ThicknessPreset* activePreset() const;

		/// Commit every consecutive pair of the in-progress chain as a wall segment
		/// (commitWallSegments) and spawn a blueprint entity per segment. Clears the chain.
		/// Host is wallHost_.
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
		ecs::EntityID spawnWallSegmentEntity(construction::SegmentId segmentId);

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
		[[nodiscard]] const assets::OpeningTypeDef* activeOpeningType() const;

		/// Spawn the ECS mirror entity for a single opening (Position at the centerline
		/// point at t, Structure{Opening, openingId}, StructureBlueprint with the type's
		/// constant manifest/work, Inventory, StructureHealth). A `built` opening spawns
		/// Complete (full work, delivered == required); otherwise AwaitingMaterials
		/// (openings have no clearing phase, like walls). Sets the opening's entity
		/// handle. No-op if the id is unknown or already has an entity.
		ecs::EntityID spawnOpeningBlueprintEntity(construction::OpeningId openingId, bool built);

		ecs::World*					ecsWorld_ = nullptr;
		Foundation::Color			validColor_{};
		Foundation::Color			invalidColor_{};
		Foundation::Color			accentColor_{};
		world::WorldCamera*			camera_ = nullptr;
		ecs::NavigationSystem*		navigation_ = nullptr;
		Callbacks					callbacks_;

		construction::ConstructionWorld constructionWorld_;

		// Committed-construction geometry, rebuilt only when constructionWorld_'s
		// version moves (see CommittedGeometryCache.h).
		CommittedGeometryCache committedCache_;

		DrawingState						   state_ = DrawingState::Idle;
		ToolKind							   activeTool_ = ToolKind::Foundation;
		std::vector<Foundation::Vec2>		   points_;				// placed vertices, world meters
		Foundation::Vec2					   cursor_{0.0F, 0.0F}; // snapped cursor, read only while followingPointer_
		construction::SnapResult			   lastSnap_;
		// A move has proposed something since the tool started or last paused. It holds over the
		// interface, where the preview stays at its last position on the world.
		bool followingPointer_ = false;
		construction::ValidationResult		   lastValidation_;

		// The last evaluated snap and point validation, and what they were evaluated
		// for. Kept apart from lastSnap_ and lastValidation_, which other paths clear.
		bool							snapMemoValid_ = false;
		SnapMemoKey						snapMemoKey_;
		std::vector<Foundation::Vec2>	snapMemoPoints_;
		construction::SnapResult		snapMemoResult_;
		construction::ValidationResult	snapMemoValidation_;
		std::optional<CloseVerdict>		snapMemoClosedShape_; // closedShapeCheck(), filled when first needed
		std::uint64_t					snapEvaluationCount_ = 0;

		// Foundation tool: true when the next click will CLOSE the shape rather than
		// place a vertex -- either the explicit origin-close (lastSnap_ Origin) or the
		// near-start "snap not block" rescue (a would-be-rejected vertex near the start
		// whose closed ring is valid). Recomputed every move; suppresses the invalid highlight.
		// In the Rectangle tool, true while the first corner and the snapped cursor span a
		// rectangle: the next release commits it.
		bool willClose_ = false;

		// While willClose_: closeVerdict() as of the last move. Drives previewVerdict (the
		// color and the close halo), the marker, the status strip, and the edit's reason.
		CloseVerdict pendingClose_;

		// True when the snapped cursor is at a NON-PLACEABLE point for the active tool (foundation:
		// water, a ford, or off-mesh; wall: those or a placed obstacle; or no active mesh). Recomputed
		// every move for the foundation + wall tools; drives the red invalid preview so the player sees
		// they can't place there BEFORE committing. The commit gate re-checks every vertex regardless, so
		// this is feedback only.
		bool cursorOffBuildableGround = false;

		// Rectangle tool: the corners and the size label of the rectangle the first corner
		// (points_) and the snapped cursor span, refreshed every move; empty when they make
		// none. And the press handlePress saw, in screen pixels, until its release; whether it
		// placed the first corner.
		std::vector<Foundation::Vec2>  rectangleCorners_;
		std::string					   rectangleLabel_;
		std::optional<Foundation::Vec2> rectanglePress_;
		bool							rectanglePressPlacedCorner_ = false;
		// What placed the snapped corner (lastSnap_ holds the point), and the memo's copies of
		// it and of the rectangle, built once for each snap the memo holds.
		construction::RectangleSnapKind rectangleSnapKind_ = construction::RectangleSnapKind::None;
		construction::RectangleSnapKind snapMemoRectangleSnapKind_ = construction::RectangleSnapKind::None;
		std::vector<Foundation::Vec2>	snapMemoRectangleCorners_;
		std::string						snapMemoRectangleLabel_;

		// Freeform tool: the pointer, world meters, of the click that placed a point, and how many
		// points that made (restsOnLastPoint).
		struct PointPlacement {
			Foundation::Vec2 pointer{0.0F, 0.0F};
			std::size_t		 pointCount = 0;
		};
		std::optional<PointPlacement> lastPlacement_;

		// The foundation tool the Build menu's Foundation entry opens: the last one activated.
		FoundationTool lastFoundationTool_ = FoundationTool::Rectangle;

		// True when the opening tool's snapped opening is not all on seen ground.
		bool openingUnexplored_ = false;

		// Wall chain host: the foundation the first chain point landed on. Every
		// segment of the chain hosts here; a later point leaving it is rejected.
		construction::FoundationId wallHost_ = construction::kInvalidFoundation;

		// Opening tool: the active opening type (config name) plus the latest snap +
		// validity, refreshed on every move and read on click / by status().
		std::string							   activeOpeningType_ = "Door";
		construction::OpeningSnap			   openingSnap_;
		construction::ValidationResult		   openingValidation_;

		// Foundation Add / Subtract: the foundation being edited, the mode, and the
		// live result for the drawn polygon (outline preview + validity), refreshed
		// on every move once the drawing closes into a polygon.
		construction::FoundationId				   editTarget_ = construction::kInvalidFoundation;
		construction::FoundationEditMode		   editMode_ = construction::FoundationEditMode::Add;
		construction::FoundationEditResult		   editPreview_;
		bool									   editPreviewActive_ = false; // >= 3 drawn points to judge

		std::string activeMaterial_ = "Wood";
		std::string activeThicknessPreset_ = "Standard";

		static constexpr float kPixelsPerMeter = 8.0F; // match GameScene
	};

} // namespace world_sim

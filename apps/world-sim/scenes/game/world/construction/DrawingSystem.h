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
	};

	/// Live status of the in-progress shape, surfaced to the config strip.
	struct DrawingStatus {
		bool		active = false; // tool is on (config strip visible)
		bool		wall = false;	// true while the wall tool is active (config strip shows presets)
		int			pointCount = 0;
		float		areaSquareMeters = 0.0F; // closed-polygon area preview (0 until >= 3 pts)
		bool		valid = true;			 // current cursor placement / shape validity
		std::string message;				 // validity reason (empty when valid)
		std::string material;				 // active material name

		// Wall readouts (meaningful only when wall == true).
		std::string thicknessPreset;		// active preset name (Light/Standard/Heavy)
		float		segmentLengthMeters = 0.0F; // length of the rubber-band segment
		float		totalLengthMeters = 0.0F;	// summed length of the committed chain so far
		float		wallCost = 0.0F;			// material cost of the chain so far
		float		wallWork = 0.0F;			// work units of the chain so far
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
			Callbacks					callbacks;
		};

		DrawingSystem() = default;
		explicit DrawingSystem(const Args& args);

		// --- Tool lifecycle ---

		/// Activate the foundation tool (from the Build menu). Enters Drawing.
		void activateFoundationTool();

		/// Activate the wall tool (from the Build menu). Enters Drawing in wall mode.
		void activateWallTool();

		/// Deactivate and discard any in-progress shape.
		void deactivate();

		[[nodiscard]] bool isActive() const { return state_ == DrawingState::Drawing; }
		[[nodiscard]] bool isWallTool() const { return activeTool_ == ToolKind::Wall; }

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
		/// (resolveWallBands), styled per segment by build progress. INTERIM.
		void renderCommittedWalls(int viewportW, int viewportH);

		/// Render the in-progress wall chain: centerline, thickness band preview,
		/// snap guides, and validity colorize. INTERIM.
		void renderWallChainPreview(int viewportW, int viewportH);

	  public:

		// --- Status for the config strip ---

		[[nodiscard]] DrawingStatus status() const;

		// --- Topology access for consumers (selection, ConstructionSystem) ---

		[[nodiscard]] engine::construction::ConstructionWorld&		 world() { return constructionWorld_; }
		[[nodiscard]] const engine::construction::ConstructionWorld& world() const { return constructionWorld_; }

	  private:
		// --- Foundation tool ---

		/// Quantize, commit, and spawn the foundation blueprint entity. Surfaces
		/// failures as a toast. Returns to an empty Drawing state on success.
		void commitShape();

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

		/// Spawn the ECS blueprint entity for a committed wall segment. lengthMeters
		/// and the preset drive the manifest / work / HP (length x thickness x rate).
		ecs::EntityID spawnWallBlueprintEntity(
			engine::construction::SegmentId		   segmentId,
			Foundation::Vec2					   a,
			Foundation::Vec2					   b,
			const engine::assets::ThicknessPreset& preset
		);

		ecs::World*					ecsWorld_ = nullptr;
		engine::world::WorldCamera* camera_ = nullptr;
		Callbacks					callbacks_;

		engine::construction::ConstructionWorld constructionWorld_;

		DrawingState						   state_ = DrawingState::Idle;
		ToolKind							   activeTool_ = ToolKind::Foundation;
		std::vector<Foundation::Vec2>		   points_;				// placed vertices, world meters
		Foundation::Vec2					   cursor_{0.0F, 0.0F}; // snapped cursor
		engine::construction::SnapResult	   lastSnap_;
		engine::construction::ValidationResult lastValidation_;

		// Wall chain host: the foundation the first chain point landed on. Every
		// segment of the chain hosts here; a later point leaving it is rejected.
		engine::construction::FoundationId wallHost_ = engine::construction::kInvalidFoundation;

		std::string activeMaterial_ = "Wood";
		std::string activeThicknessPreset_ = "Standard";

		static constexpr float kPixelsPerMeter = 8.0F; // match GameScene
	};

} // namespace world_sim

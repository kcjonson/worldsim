#pragma once

// DrawingSystem - foundation drawing tool for the construction system.
//
// Sibling of PlacementSystem (building-construction D11). Owns the foundation
// tool's state machine, the SnapEngine and ConstructionValidator (both engine
// pure-logic), and the app-level ConstructionWorld topology store. Click-by-
// click polygon drawing in world meters; on close it quantizes the ring,
// commits to ConstructionWorld, and spawns the ECS blueprint entity.
//
// Input order in GameScene is GameUI -> DrawingSystem -> PlacementSystem ->
// SelectionSystem: while drawing, this consumes clicks before placement/
// selection get them (GameUI still gets first dibs).
//
// Rendering here is INTERIM: it draws the in-progress preview AND committed
// foundations via Primitives so the tool is verifiable end to end. C6 replaces
// committed-foundation rendering with the baked element-emitter + progress
// prefix; this whole render path goes away then.

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

namespace world_sim {

	/// Foundation tool state. Idle when the tool is off; Drawing while collecting
	/// points. There is no separate "armed but no points" state: activation enters
	/// Drawing with an empty point list, the first click places the origin.
	enum class DrawingState {
		Idle,
		Drawing,
	};

	/// Live status of the in-progress shape, surfaced to the config strip.
	struct DrawingStatus {
		bool		active		= false; // tool is on (config strip visible)
		int			pointCount	= 0;
		float		areaSquareMeters = 0.0F; // closed-polygon area preview (0 until >= 3 pts)
		bool		valid		= true;	 // current cursor placement / shape validity
		std::string message;			 // validity reason (empty when valid)
		std::string material;			 // active material name
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

		/// Deactivate and discard any in-progress shape.
		void deactivate();

		[[nodiscard]] bool isActive() const { return state_ == DrawingState::Drawing; }

		// --- Material selection (from the config strip) ---

		void setActiveMaterial(const std::string& material);
		[[nodiscard]] const std::string& activeMaterial() const { return activeMaterial_; }

		// --- Input ---

		/// Update the snapped cursor + live validity from a mouse move.
		void handleMouseMove(float screenX, float screenY, int viewportW, int viewportH, bool freeform);

		/// Left click: add a snapped point, or commit if it closes the shape.
		/// @return true if the click was consumed.
		bool handleClick(float screenX, float screenY, int viewportW, int viewportH, bool freeform);

		/// Esc / right-click: cancel the in-progress shape (or exit the tool when
		/// nothing is in progress). @return true if it consumed the action.
		bool cancel();

		/// Backspace: remove the last placed point. @return true if a point was removed.
		bool removeLastPoint();

		// --- Rendering (INTERIM, see file header) ---

		void render(int viewportW, int viewportH);

		// --- Status for the config strip ---

		[[nodiscard]] DrawingStatus status() const;

		// --- Topology access for later pieces (C5/C6/C7) ---

		[[nodiscard]] engine::construction::ConstructionWorld&		world() { return constructionWorld_; }
		[[nodiscard]] const engine::construction::ConstructionWorld& world() const { return constructionWorld_; }

	  private:
		/// Quantize, commit, and spawn the blueprint entity. Surfaces failures as
		/// a toast. Returns to an empty Drawing state on success.
		void commitShape();

		/// Build the ECS blueprint entity for a committed foundation.
		ecs::EntityID spawnBlueprintEntity(engine::construction::FoundationId id);

		ecs::World*					ecsWorld_ = nullptr;
		engine::world::WorldCamera* camera_	  = nullptr;
		Callbacks					callbacks_;

		engine::construction::ConstructionWorld constructionWorld_;

		DrawingState				  state_ = DrawingState::Idle;
		std::vector<Foundation::Vec2> points_;	  // placed vertices, world meters
		Foundation::Vec2			  cursor_{0.0F, 0.0F}; // snapped cursor
		engine::construction::SnapResult	  lastSnap_;
		engine::construction::ValidationResult lastValidation_;

		std::string activeMaterial_ = "Wood";

		static constexpr float kPixelsPerMeter = 8.0F; // match GameScene
	};

} // namespace world_sim

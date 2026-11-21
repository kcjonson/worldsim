#pragma once

// Item views (docs/technical/rendering/rotatable-placed-items.md, sections 2, 3, 6, and 7): everything drawn for
// one parts def at one turn and one detail level, built on the CPU, and the strip layout that cuts it into depth
// strips. Pure functions of the def and the view config, safe on any thread while no load or clear runs.

#include "assets/AssetDefinition.h"
#include "assets/MeshSilhouette.h"
#include "assets/PartsAsset.h"
#include "assets/ViewConfig.h"

#include <math/AffineTransform2D.h>
#include <vector/Types.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace assets {

	// ─── Turns (section 3) ────────────────────────────────────────────────────────────────────────────────

	/// A placed item's turn is a whole number of these. Part of the stored format, not tuning.
	inline constexpr double kTurnQuantumDegrees = 0.01;
	/// Quanta in a whole turn: a turn is k in [0, kTurnQuantaPerTurn).
	inline constexpr uint32_t kTurnQuantaPerTurn = 36000;

	/// A turn in degrees clockwise rounded to the nearest quantum and then wrapped into [0, 36000), in that
	/// order, so 359.996 degrees is quantum 0.
	[[nodiscard]] uint32_t turnQuantaFromDegrees(double degrees);

	/// A turn's radians, k times the quantum, computed in double and stored as float.
	[[nodiscard]] float turnRadiansFromQuanta(uint32_t turnQuanta);

	/// The turn an entity's ecs::Rotation stands for: its radians, positive from +X toward +Y (clockwise on the
	/// screen), in degrees, rounded to the quantum and wrapped as turnQuantaFromDegrees does.
	[[nodiscard]] uint32_t turnQuantaFromRadians(float radians);

	// ─── Faces and shading (sections 6.3 and 7) ──────────────────────────────────────────────────────────

	/// A side face that shows at a turn: its outward plan normal points down the screen.
	struct VisibleFace {
		uint32_t	placement = 0;
		uint32_t	face = 0;	   ///< into the part's faces
		glm::vec2	left{0.0F};	   ///< A: the face's left end seen from outside it, on the ground, in the item frame turned
		glm::vec2	right{0.0F};   ///< B
		glm::vec2	normal{0.0F};  ///< outward, turned
		/// Maps a drawing point (u along the face from its left end, v down from the part's top), in metres, onto
		/// the screen relative to the pivot.
		Foundation::AffineTransform2D matrix;
		float						  shade = 1.0F;
		/// False when the projected width is under faceDetailMinWidthMeters (or the face has no drawing): the
		/// face draws its plain rectangle.
		bool drawsArt = false;
	};

	/// Every visible side face at a turn, placement by placement, faces in winding order.
	[[nodiscard]] std::vector<VisibleFace> visibleFaces(const PartsAsset& parts, float turnRadians, const ViewConfig& config);

	/// A visible side face's shade from the one light (section 7); colours on the face are multiplied by it.
	[[nodiscard]] float sideShade(glm::vec2 normal, const ViewConfig& config);

	// ─── Strip layouts (section 6.7) ──────────────────────────────────────────────────────────────────────

	/// A placement's triangles between two screen x values (relative to the pivot), which go to one strip.
	struct StripPiece {
		uint32_t placement = 0;
		float	 minX = 0.0F; ///< -infinity at the first band
		float	 maxX = 0.0F; ///< +infinity at the last band
	};

	/// One depth strip: an interval of the collider within one band, or a merged run of them, or the flat strip.
	struct ItemStrip {
		/// The largest plan y of its interval's southern edge within its bands (the largest of a merged run);
		/// the lowest float for the flat strip.
		float anchorLocalY = 0.0F;
		/// Its screen box relative to the pivot, which holds its triangles at every level.
		float minX = 0.0F;
		float maxX = 0.0F;
		float minY = 0.0F;
		float maxY = 0.0F;
		/// The x range of its bands, where the selection outline is cut (section 15): -infinity and +infinity at the
		/// layout's first and last band, and both for the flat strip.
		float bandMinX = 0.0F;
		float bandMaxX = 0.0F;
		bool  flat = false;
		/// Each placement's triangles in it, by x range, ordered by placement then x.
		std::vector<StripPiece> pieces;
	};

	/// Where an item is cut into depth strips at a turn: the flat strip first (when the item has flat parts on
	/// the ground), then ascending anchorLocalY, ties left to right.
	struct StripLayout {
		uint32_t			   turnQuanta = 0;
		std::vector<ItemStrip> strips;
	};

	/// The strip layout of a parts def at a turn, from its collider and footprints alone: no drawing is read and
	/// nothing is tessellated, so it is cheap enough for the main thread. Empty for a def that isn't parts.
	[[nodiscard]] StripLayout stripLayout(const AssetDefinition& def, uint32_t turnQuanta, const ViewConfig& config);

	/// A strip layout the registry keeps for a template at a turn (AssetRegistry::stripLayout), which an item's
	/// stream records point into (section 8).
	struct ItemLayout {
		uint32_t	templateId = 0;
		uint32_t	turnQuanta = 0;
		/// Its place among the layouts kept since the last clear, from 0.
		uint32_t	id = 0;
		StripLayout layout;
	};

	// ─── Views (section 6) ────────────────────────────────────────────────────────────────────────────────

	/// A view's past this many vertices is an error (16-bit indices).
	inline constexpr uint32_t kMaxViewVertices = 65535;
	/// Validation rule 12: a view at a 15 degree stop at the finest level stays under this.
	inline constexpr uint32_t kMaxStopViewVertices = 49152;

	/// One depth strip of a built view: its triangles are indices [firstIndex, firstIndex + indexCount).
	struct ViewStrip {
		uint32_t firstIndex = 0;
		uint32_t indexCount = 0;
		float	 anchorLocalY = 0.0F;
		float	 minX = 0.0F;
		float	 maxX = 0.0F;
		float	 minY = 0.0F;
		float	 maxY = 0.0F;
		float	 bandMinX = 0.0F;
		float	 bandMaxX = 0.0F;
		bool	 flat = false;
	};

	/// A view's selection silhouette (section 15): its rings and hit region, in pivot millimetres with the turn in
	/// them, and the rings cut at the strips' band edges into open lines, each strip's in its own entry, so the outline
	/// draws at the strips' depths. Within a band of several strips (the two sides of a notch) a line goes to the strip
	/// whose own silhouette it lies on.
	struct ItemSilhouette {
		AssetSilhouette silhouette;
		/// By strip, in the layout's order: open lines in metres relative to the pivot.
		std::vector<std::vector<std::vector<glm::vec2>>> stripLines;
	};

	struct ItemView {
		uint32_t turnQuanta = 0;
		uint8_t	 level = 0;
		/// Triangles with per-vertex colours, in metres relative to the pivot, the turn applied. No MeshParts.
		renderer::TessellatedMesh mesh;
		/// In the layout's order, each a contiguous index range; together they cover every index once.
		std::vector<ViewStrip> strips;
		/// Built from the view's triangles at the reference level (section 15); invalid at any other level.
		ItemSilhouette silhouette;
		/// How far from the pivot anything drawn at any turn reaches (section 6.9).
		float reachMeters = 0.0F;
		/// Why there is no view (past kMaxViewVertices, an unknown level, not a parts def); empty when built.
		std::string error;
		/// How many vertices it would have, also when it is refused for being past kMaxViewVertices.
		uint32_t vertexCount = 0;
	};

	/// Builds a parts def's view at a turn and one of its detail levels (0 for a def without levels).
	[[nodiscard]] ItemView buildItemView(const AssetDefinition& def, uint32_t turnQuanta, uint8_t level, const ViewConfig& config);

	/// buildItemView without the silhouette, the costly part: for the registry's template (whose silhouette it keeps
	/// itself, from the same triangles) and validation's vertex count.
	[[nodiscard]] ItemView buildItemMesh(const AssetDefinition& def, uint32_t turnQuanta, uint8_t level, const ViewConfig& config);

	/// The farthest any drawn point of a parts item reaches from its pivot at any turn: the largest |p| +
	/// top * heightRatio over every placed footprint vertex p, plus the outline's half width.
	[[nodiscard]] float itemReachMeters(const PartsAsset& parts, const ViewConfig& config);

} // namespace assets

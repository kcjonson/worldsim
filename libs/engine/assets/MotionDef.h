#pragma once

// Motion definitions: declarative, per-part animation clips for an asset.
//
// A motion file (`<Motion>` XML, referenced by an AssetDef's <motion>) holds named <clip>s,
// each a list of <drive> rows. A driver is pure data: which part (by SVG id), which node of
// that part is the pivot, which channel, and a waveform with amp/freq/phase. The C++ animator
// evaluates `value = amp * fn(2*pi*(freq*phase + phaseOffset))` each frame and applies it to
// the part's mesh vertex range about the resolved pivot. No scripting.

#include <cmath>
#include <string>
#include <vector>

#include <glm/vec2.hpp>

namespace engine::assets {

	/// A resolved per-part affine for one frame: rotate+scale about `pivot`, then translate.
	/// All values are in the coordinate space of the vertices it will be applied to (the caller
	/// converts pivot/translate into that space; rotation and scale are space-invariant).
	struct PartTransform {
		float	  rotation = 0.0F;		   // radians
		glm::vec2 scale{1.0F, 1.0F};
		glm::vec2 translate{0.0F, 0.0F};
		glm::vec2 pivot{0.0F, 0.0F};

		[[nodiscard]] glm::vec2 apply(glm::vec2 v) const {
			const glm::vec2 p{(v.x - pivot.x) * scale.x, (v.y - pivot.y) * scale.y};
			const float		c = std::cos(rotation);
			const float		s = std::sin(rotation);
			return {pivot.x + (p.x * c - p.y * s) + translate.x, pivot.y + (p.x * s + p.y * c) + translate.y};
		}
	};

	/// Which transform component a driver animates.
	enum class MotionChannel { Rotation, PosX, PosY, ScaleX, ScaleY };

	/// Driver waveform. Phase is normalized [0,1); one period spans the full clip cycle.
	enum class MotionWave { Sine, Triangle };

	/// One animated channel of one part: a waveform plus a pivot for rotation/scale.
	struct MotionDriver {
		std::string	  part;					   // Target part = SVG shape id
		int			  node = 0;				   // Pivot = this authored node index of the part's path
		MotionChannel channel = MotionChannel::Rotation;
		MotionWave	  wave = MotionWave::Sine;
		float		  amp = 0.0F;			   // Degrees (rotation), meters (pos), ratio delta (scale)
		float		  freq = 1.0F;			   // Cycles per clip period
		float		  phaseOffset = 0.0F;	   // [0,1) offset into the cycle

		// Resolved at load against this asset's geometry: pivot in the mesh local-meter frame
		// (the same frame the rendered vertices live in). hasPivot is false for pure translation.
		glm::vec2 pivot{0.0F, 0.0F};
		bool	  hasPivot = false;
	};

	/// A named animation made of independent per-part drivers.
	struct MotionClip {
		std::string				  name;
		float					  stride = 1.0F; // Meters of travel per full cycle (drives phase advance)
		std::vector<MotionDriver> drivers;
	};

	/// All clips for an asset, resolved into its local-meter frame and cached on the template.
	struct MotionDef {
		std::vector<MotionClip> clips;

		[[nodiscard]] bool empty() const { return clips.empty(); }

		[[nodiscard]] const MotionClip* findClip(const std::string& name) const {
			for (const auto& c : clips) {
				if (c.name == name) {
					return &c;
				}
			}
			return nullptr;
		}
	};

} // namespace engine::assets

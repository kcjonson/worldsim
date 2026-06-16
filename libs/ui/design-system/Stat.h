#pragma once

// Salvage design-system primitive: Stat.
//
// A labeled numeric readout: a small stencil label over a large value, with an
// optional small trailing unit. Tone colors only the value, not the label.
// Size sets the value's font size (sm/md/lg).
//
// Static rendering only. All visuals come from UI::DS tokens.
//
// Spec: docs/design/ui/design-system/components.md (Stat section).

#include "design-system/Variants.h"
#include "math/Types.h"
#include <string>

namespace UI::DS {

	class Stat {
	  public:
		struct Args {
			Foundation::Vec2 position{0.0F, 0.0F};
			std::string		 label;
			std::string		 value;
			std::string		 unit; // empty -> no unit
			Tone			 tone = Tone::Default;
			Size			 size = Size::Md;
		};

		explicit Stat(Args args);

		// Draw the label, then the value with its optional trailing unit.
		void render() const;

	  private:
		Args args;
	};

} // namespace UI::DS

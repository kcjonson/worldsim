#pragma once

// StatusTextLine - a task or action line coloured by its status: active green behind "> ", pending
// amber, idle dim, blocked red behind "x ", available in the body colour; the others behind two spaces
// so the text lines up. With `wrap` it breaks to its width. Its style's set fields win over the status
// colour.

#include "scenes/game/ui/Look.h"

#include <ui/shapes/Text.h>

#include <cstdint>
#include <string>

namespace world_sim {

	enum class LineStatus : std::uint8_t {
		Active,	 // running now
		Pending, // waiting to run
		Idle,	 // not active
		Blocked, // can't run
		Available
	};

	class StatusTextLine : public ui::Text {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string		   text;
			LineStatus		   status = LineStatus::Available;
			ui::DimensionToken fontSize = look::t::fontSize::fsXs;
			bool			   wrap = false;
		};

		explicit StatusTextLine(const Args& args);

		void setStatus(LineStatus newStatus, const std::string& newText);

		[[nodiscard]] LineStatus getStatus() const { return status; }

		[[nodiscard]] const char* typeName() const override { return "StatusTextLine"; }

	  protected:
		void onMount() override;

	  private:
		void tint();

		LineStatus		   status;
		ui::DimensionToken fontSize;
		ui::Style		   given;
	};

} // namespace world_sim

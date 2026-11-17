#pragma once

// DrawingKeyGuide - the key guide (building-construction D15): while a drawing tool is on and Show
// key guide is, a small panel above the construction strip with the tool's keys, a legend line for
// each helper that is on, its glyph drawn as the preview draws that mark, and a foot line.
// Collapsed, a "Keys ?" chip, which opens it. It takes the pointer only inside its own box, and the
// HUD gives it no keyboard focus.

#include "UserSettings.h"
#include "scenes/game/world/construction/DrawingFeedback.h"

#include <input/InputTypes.h>
#include <ui/Component.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace world_sim {

	enum class ToolKind;

	/// A run of a key row's keys: a key chip, or the words beside one.
	struct KeyGuidePart {
		std::string text;
		bool		chip = false;

		bool operator==(const KeyGuidePart&) const = default;
	};

	/// A key row: its keys, then what they do.
	struct KeyGuideKeyRow {
		std::vector<KeyGuidePart> keys;
		std::string				  action;

		bool operator==(const KeyGuideKeyRow&) const = default;
	};

	/// A legend row: a helper's mark, then what it means.
	struct KeyGuideLegendRow {
		LegendGlyph glyph;
		std::string text;

		bool operator==(const KeyGuideLegendRow&) const = default;
	};

	struct KeyGuideContent {
		std::vector<KeyGuideKeyRow>	   keyRows;
		std::vector<KeyGuideLegendRow> legendRows;
		std::vector<KeyGuidePart>	   foot;
	};

	/// What the key guide lists for `tool` under `settings`: the design spec's key rows for the tool,
	/// then a legend row for each helper that is on (the Freeform, wall, and Add / Subtract tools
	/// only), then the foot line.
	[[nodiscard]] KeyGuideContent keyGuideContent(ToolKind tool, const DrawingSettings& settings);

	/// Whether `event` collapses or opens the key guide: the `?` character, whatever keys typed it,
	/// or a press of H that is not a repeat.
	[[nodiscard]] bool togglesKeyGuide(const foundation::InputEvent& event);

	class DrawingKeyGuide : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::function<void()> onOpen; // a click on the chip
		};

		explicit DrawingKeyGuide(const Args& args);

		/// Shown while `tool` is on and `settings` shows the key guide: the panel, or the chip while
		/// `collapsed`. The rows are built again only when one of the three changes.
		void show(std::optional<ToolKind> tool, const DrawingSettings& settings, bool collapsed);

		[[nodiscard]] const char* typeName() const override { return "DrawingKeyGuide"; }

	  protected:
		void onMount() override;

	  private:
		void build();

		struct Shown {
			std::optional<ToolKind> tool;
			DrawingSettings			settings;
			bool					collapsed = false;

			bool operator==(const Shown&) const = default;
		};

		std::function<void()>  onOpen;
		std::optional<Shown>   shown;
		ui::Ref<ui::Component> panel;
		ui::Ref<ui::Component> chip;
	};

} // namespace world_sim

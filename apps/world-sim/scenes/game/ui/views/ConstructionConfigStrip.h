#pragma once

// ConstructionConfigStrip - the strip docked above the GameplayBar while a structure tool is active
// (building-construction D11), hidden otherwise. Material cards (click to pick), then in wall mode the
// active material's thickness presets, the tool's readouts, and a validity line in a status colour.
// Foundation Add / Subtract shows no material cards (the edited foundation's material is locked), its
// label instead, and the area the edit would leave with its change. 60% of its parent's width and
// kHeight tall unless its size says otherwise.

#include "scenes/game/world/construction/DrawingSystem.h"

#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace world_sim {

	class ConstructionConfigStrip : public ui::Rectangle {
	  public:
		static constexpr float kHeight = 56.0F;

		struct ThicknessPresetInfo {
			std::string name;
			float		thicknessMeters = 0.0F;
		};

		struct Args {
			UI_COMMON_ARGS
			std::function<void(const std::string&)> onMaterialSelected;
			std::function<void(const std::string&)> onThicknessSelected; // wall mode
		};

		explicit ConstructionConfigStrip(const Args& args);

		// {name, cost per square metre} for each material, from the registry.
		void setMaterials(std::vector<std::pair<std::string, float>> materials);
		// The active material's wall thickness presets.
		void setThicknessPresets(std::vector<ThicknessPresetInfo> presets);
		// The tool's latest status: its readouts, the validity line, the picked cards, and whether the
		// strip shows at all.
		void setStatus(const DrawingStatus& status);

		[[nodiscard]] const char* typeName() const override { return "ConstructionConfigStrip"; }

	  protected:
		void onMount() override;

	  private:
		void showCards();
		void showReadouts();

		std::function<void(const std::string&)>	   onMaterialSelected;
		std::function<void(const std::string&)>	   onThicknessSelected;
		std::vector<std::pair<std::string, float>> materials;
		std::vector<ThicknessPresetInfo>		   presets;
		DrawingStatus							   status;
		ui::ColorToken							   messageColor{};
		ui::Ref<ui::Component>					   materialCards;
		ui::Ref<ui::Component>					   presetCards;
		ui::Ref<ui::Component>					   readouts;
		ui::Ref<ui::Text>						   readoutTop;
		ui::Ref<ui::Text>						   readoutBottom;
		ui::Ref<ui::Text>						   message;
	};

} // namespace world_sim

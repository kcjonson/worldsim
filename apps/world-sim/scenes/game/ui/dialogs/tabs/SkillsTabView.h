#pragma once

// SkillsTabView - the dossier's Skills tab. The game simulates no skills yet, so it is an empty state.

#include <ui/Component.h>

namespace world_sim {

	struct SkillsData {};

	class SkillsTabView : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
		};

		void update(const SkillsData& data);

		[[nodiscard]] const char* typeName() const override { return "SkillsTabView"; }

	  protected:
		void onMount() override;
	};

} // namespace world_sim

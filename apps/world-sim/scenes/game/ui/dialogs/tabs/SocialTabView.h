#pragma once

// SocialTabView - the dossier's Social tab, a placeholder until relationships are tracked.

#include <ui/Component.h>

#include <string>

namespace world_sim {

	struct SocialData {
		std::string placeholder = "Relationships not yet tracked";
	};

	class SocialTabView : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
		};

		void update(const SocialData& data);

		[[nodiscard]] const char* typeName() const override { return "SocialTabView"; }

	  protected:
		void onMount() override;
	};

} // namespace world_sim

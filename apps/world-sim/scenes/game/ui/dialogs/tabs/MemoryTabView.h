#pragma once

// MemoryTabView - the dossier's Memory tab: a summary row (locations known, sight range) over a
// two-column grid of what the colonist remembers by category, each a name and a count badge over a few
// of its entities and their positions.

#include <ui/Component.h>
#include <ui/shapes/Text.h>

#include <string>
#include <vector>

namespace world_sim {

	struct MemoryEntity {
		std::string name; // "Berry Bush"
		float		x = 0.0F;
		float		y = 0.0F;
	};

	struct MemoryCategory {
		std::string				  name; // "Food Sources"
		std::vector<MemoryEntity> entities;
		size_t					  count = 0; // all of them, where entities may be truncated
	};

	struct MemoryData {
		std::vector<MemoryCategory> categories;
		size_t						totalKnown = 0;
		float						sightRadius = 30.0F;
	};

	class MemoryTabView : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
		};

		void update(const MemoryData& data);

		[[nodiscard]] const char* typeName() const override { return "MemoryTabView"; }

	  protected:
		void onMount() override;

	  private:
		ui::Ref<ui::Text>	   knownValue;
		ui::Ref<ui::Text>	   sightValue;
		ui::Ref<ui::Component> leftColumn;
		ui::Ref<ui::Component> rightColumn;
	};

} // namespace world_sim

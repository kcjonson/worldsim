#pragma once

// TaskListView - a colonist's decision trace, above EntityInfoView while its task list is open: a
// "<name> - Tasks" title with a close button, then why the winner won, the current task and action,
// and the task queue, each option's tier, reason and score over its bonus breakdown. The content
// scrolls once the panel reaches its max height. kWidth wide and at most kMaxHeight tall unless its
// size and max size say otherwise; its height follows the content.

#include <ecs/EntityID.h>
#include <ecs/World.h>

#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>

#include <functional>

namespace world_sim {

	class TaskListView : public ui::Rectangle {
	  public:
		static constexpr float kWidth = 360.0F;
		static constexpr float kMaxHeight = 400.0F;

		struct Args {
			UI_COMMON_ARGS
			std::function<void()> onClose;
		};

		explicit TaskListView(const Args& args);

		// Each frame while it's open; rebuilds when the colonist changes or its trace is evaluated again.
		void update(const ecs::World& world, ecs::EntityID colonistId);

		[[nodiscard]] const char* typeName() const override { return "TaskListView"; }

	  protected:
		void onMount() override;

	  private:
		void rebuild(const ecs::World& world, ecs::EntityID colonistId);

		std::function<void()>  onClose;
		ui::Size			   givenMaxSize; // Component keeps no getter for it
		ecs::EntityID		   lastColonistId{};
		bool				   built = false;
		float				   lastEvaluationTime = -1.0F;
		ui::Ref<ui::Text>	   title;
		ui::Ref<ui::Component> list;
	};

} // namespace world_sim

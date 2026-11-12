#pragma once

#include <ecs/ISystem.h>

#include <functional>
#include <utility>

namespace world_sim {

	/// Runs the scene's deferred entity removals and drops inside the tick, after every system that
	/// queues them and before NavigationSystem::endTick launches the next patch job. Draining after
	/// World::update instead would make a drop's first nav probe wait for that whole job.
	class DeferredEditsSystem : public ecs::ISystem {
	  public:
		explicit DeferredEditsSystem(std::function<void()> drain)
			: drain(std::move(drain)) {}

		void update(float /*deltaTime*/) override { drain(); }

		// After ActionSystem (350), the last system that queues edits; before rendering (900).
		[[nodiscard]] int		  priority() const override { return 800; }
		[[nodiscard]] const char* name() const override { return "DeferredEditsSystem"; }

	  private:
		std::function<void()> drain;
	};

} // namespace world_sim

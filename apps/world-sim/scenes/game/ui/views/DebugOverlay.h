#pragma once

// DebugOverlay - three development lines: chunks loaded, the camera's position and chunk, and that
// chunk's biome. A column hugging its text, which the pointer passes through to the world.

#include <ui/Component.h>
#include <ui/shapes/Text.h>
#include <world/camera/WorldCamera.h>
#include <world/chunk/ChunkManager.h>

namespace world_sim {

	class DebugOverlay : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
		};

		void updateData(const engine::world::WorldCamera& camera, const engine::world::ChunkManager& chunkManager);

		[[nodiscard]] const char* typeName() const override { return "DebugOverlay"; }

	  protected:
		void							onMount() override;
		[[nodiscard]] ui::PointerEvents defaultPointerEvents() const override { return ui::PointerEvents::None; }

	  private:
		ui::Ref<ui::Text> chunks;
		ui::Ref<ui::Text> cameraLine;
		ui::Ref<ui::Text> biome;
	};

} // namespace world_sim

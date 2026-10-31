#include "DebugOverlay.h"

#include "scenes/shared/SalalUi.h"

#include <ui/Context.h>
#include <world/Biome.h>

#include <format>

namespace world_sim {

	namespace {
		constexpr float kPadding = 10.0F;
	} // namespace

	void DebugOverlay::onMount() {
		const ui::Theme& theme = getContext()->getTheme();
		ui::Layout		 column = getLayout();
		column.display = ui::Display::Flex;
		column.flexDirection = ui::FlexDirection::Column;
		column.gap = theme.get(tokens::spacing::space05);
		column.padding = kPadding;
		column.alignItems = ui::AlignItems::Start;
		setLayout(column);
		const ui::Style line = world_sim::text(theme, {.size = tokens::fontSize::fsMd, .color = tokens::color::textBright});
		chunks = addChild<ui::Text>({.id = "debug_chunks", .style = line, .text = "Chunks: 0"});
		cameraLine = addChild<ui::Text>({.id = "debug_position", .style = line, .text = "Position: (0, 0)"});
		biome = addChild<ui::Text>({.id = "debug_biome", .style = line, .text = "Biome: Unknown"});
	}

	void DebugOverlay::updateData(const engine::world::WorldCamera& camera, const engine::world::ChunkManager& chunkManager) {
		if (chunks.get() == nullptr) {
			return;
		}
		chunks.get()->setText(std::format("Chunks: {}", chunkManager.loadedChunkCount()));
		const auto chunk = camera.currentChunk();
		cameraLine.get()->setText(
			std::format(
				"Position: ({}, {}) Chunk: ({}, {})",
				static_cast<int>(camera.position().x),
				static_cast<int>(camera.position().y),
				chunk.x,
				chunk.y
			)
		);
		const auto* current = chunkManager.getChunk(chunk);
		biome.get()->setText(
			current != nullptr ? "Biome: " + std::string(engine::world::biomeToString(current->primaryBiome())) : "Biome: Loading..."
		);
	}

} // namespace world_sim

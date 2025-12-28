#include "DebugOverlay.h"

#include <world/Biome.h>

#include <sstream>

namespace world_sim {

DebugOverlay::DebugOverlay(const Args& /*args*/) {
	createElements();
}

void DebugOverlay::createElements() {
	// Create elements with default positions in top-left corner
	chunksText = std::make_unique<UI::Text>(UI::Text::Args{
		.position = {10.0F, 10.0F},
		.text = "Chunks: 0",
		.style =
			{
				.color = Foundation::Color::white(),
				.fontSize = 16.0F,
				.hAlign = Foundation::HorizontalAlign::Left,
				.vAlign = Foundation::VerticalAlign::Top,
			},
		.id = "debug_chunks"
	});

	positionText = std::make_unique<UI::Text>(UI::Text::Args{
		.position = {10.0F, 30.0F},
		.text = "Position: (0, 0)",
		.style =
			{
				.color = Foundation::Color::white(),
				.fontSize = 16.0F,
				.hAlign = Foundation::HorizontalAlign::Left,
				.vAlign = Foundation::VerticalAlign::Top,
			},
		.id = "debug_position"
	});

	biomeText = std::make_unique<UI::Text>(UI::Text::Args{
		.position = {10.0F, 50.0F},
		.text = "Biome: Unknown",
		.style =
			{
				.color = Foundation::Color::white(),
				.fontSize = 16.0F,
				.hAlign = Foundation::HorizontalAlign::Left,
				.vAlign = Foundation::VerticalAlign::Top,
			},
		.id = "debug_biome"
	});
}

void DebugOverlay::layout(const Foundation::Rect& bounds) {
	// Position debug overlay below the TopBar
	float x = bounds.x + 10.0F;
	float y = bounds.y + 10.0F;
	constexpr float kLineSpacing = 20.0F;

	if (chunksText) {
		chunksText->setPosition(x, y);
	}
	if (positionText) {
		positionText->setPosition(x, y + kLineSpacing);
	}
	if (biomeText) {
		biomeText->setPosition(x, y + kLineSpacing * 2.0F);
	}
}

void DebugOverlay::update(
	const engine::world::WorldCamera& camera,
	const engine::world::ChunkManager& chunkManager
) {
	// Update chunk count
	std::ostringstream chunkStr;
	chunkStr << "Chunks: " << chunkManager.loadedChunkCount();
	chunksText->text = chunkStr.str();

	// Update position and chunk
	std::ostringstream posStr;
	posStr << "Position: (" << static_cast<int>(camera.position().x) << ", "
		   << static_cast<int>(camera.position().y) << ") Chunk: (" << camera.currentChunk().x
		   << ", " << camera.currentChunk().y << ")";
	positionText->text = posStr.str();

	// Update biome from current chunk
	std::ostringstream biomeStr;
	const auto* currentChunk = chunkManager.getChunk(camera.currentChunk());
	if (currentChunk != nullptr) {
		biomeStr << "Biome: " << engine::world::biomeToString(currentChunk->primaryBiome());
	} else {
		biomeStr << "Biome: Loading...";
	}
	biomeText->text = biomeStr.str();
}

void DebugOverlay::render() {
	if (chunksText) {
		chunksText->render();
	}
	if (positionText) {
		positionText->render();
	}
	if (biomeText) {
		biomeText->render();
	}
}

}  // namespace world_sim

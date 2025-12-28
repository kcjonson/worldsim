#include "DebugOverlay.h"

#include <world/Biome.h>

#include <sstream>

namespace world_sim {

DebugOverlay::DebugOverlay(const Args& /*args*/) {
	// Add text children to the layer system
	chunksTextHandle = addChild(UI::Text(UI::Text::Args{
		.position = {kPadding, kPadding},
		.text = "Chunks: 0",
		.style =
			{
				.color = Foundation::Color::white(),
				.fontSize = 16.0F,
				.hAlign = Foundation::HorizontalAlign::Left,
				.vAlign = Foundation::VerticalAlign::Top,
			},
		.id = "debug_chunks"}));

	positionTextHandle = addChild(UI::Text(UI::Text::Args{
		.position = {kPadding, kPadding + kLineSpacing},
		.text = "Position: (0, 0)",
		.style =
			{
				.color = Foundation::Color::white(),
				.fontSize = 16.0F,
				.hAlign = Foundation::HorizontalAlign::Left,
				.vAlign = Foundation::VerticalAlign::Top,
			},
		.id = "debug_position"}));

	biomeTextHandle = addChild(UI::Text(UI::Text::Args{
		.position = {kPadding, kPadding + kLineSpacing * 2.0F},
		.text = "Biome: Unknown",
		.style =
			{
				.color = Foundation::Color::white(),
				.fontSize = 16.0F,
				.hAlign = Foundation::HorizontalAlign::Left,
				.vAlign = Foundation::VerticalAlign::Top,
			},
		.id = "debug_biome"}));
}

void DebugOverlay::layout(const Foundation::Rect& newBounds) {
	// Store bounds for Component base class
	Component::layout(newBounds);

	// Position text elements within bounds
	float x = newBounds.x + kPadding;
	float y = newBounds.y + kPadding;

	if (auto* text = getChild<UI::Text>(chunksTextHandle)) {
		text->setPosition(x, y);
	}
	if (auto* text = getChild<UI::Text>(positionTextHandle)) {
		text->setPosition(x, y + kLineSpacing);
	}
	if (auto* text = getChild<UI::Text>(biomeTextHandle)) {
		text->setPosition(x, y + kLineSpacing * 2.0F);
	}
}

void DebugOverlay::updateData(
	const engine::world::WorldCamera& camera,
	const engine::world::ChunkManager& chunkManager
) {
	// Update chunk count
	if (auto* text = getChild<UI::Text>(chunksTextHandle)) {
		std::ostringstream oss;
		oss << "Chunks: " << chunkManager.loadedChunkCount();
		text->text = oss.str();
	}

	// Update position and chunk
	if (auto* text = getChild<UI::Text>(positionTextHandle)) {
		std::ostringstream oss;
		oss << "Position: (" << static_cast<int>(camera.position().x) << ", "
			<< static_cast<int>(camera.position().y) << ") Chunk: (" << camera.currentChunk().x
			<< ", " << camera.currentChunk().y << ")";
		text->text = oss.str();
	}

	// Update biome from current chunk
	if (auto* text = getChild<UI::Text>(biomeTextHandle)) {
		std::ostringstream oss;
		const auto* currentChunk = chunkManager.getChunk(camera.currentChunk());
		if (currentChunk != nullptr) {
			oss << "Biome: " << engine::world::biomeToString(currentChunk->primaryBiome());
		} else {
			oss << "Biome: Loading...";
		}
		text->text = oss.str();
	}
}

// render() is inherited from Component - automatically renders all children

}  // namespace world_sim

#include "GameOverlay.h"

#include <world/Biome.h>

#include <sstream>

namespace world_sim {

	GameOverlay::GameOverlay(const Args& args)
		: onZoomIn(args.onZoomIn),
		  onZoomOut(args.onZoomOut) {
		createElements();
	}

	void GameOverlay::createElements() {
		// Create elements with default positions - layout() will position them
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
			.id = "overlay_chunks"
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
			.id = "overlay_position"
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
			.id = "overlay_biome"
		});

		zoomControl = std::make_unique<ZoomControl>(
			ZoomControl::Args{.position = {10.0F, 75.0F}, .onZoomIn = onZoomIn, .onZoomOut = onZoomOut, .id = "zoom_control"}
		);
	}

	void GameOverlay::layout(const Foundation::Rect& newBounds) {
		viewportBounds = newBounds;
	}

	void GameOverlay::update(const engine::world::WorldCamera& camera, const engine::world::ChunkManager& chunkManager) {
		// Update chunk count text in-place (created in createElements())
		std::ostringstream chunkStr;
		chunkStr << "Chunks: " << chunkManager.loadedChunkCount();
		chunksText->text = chunkStr.str();

		// Update position text in-place (created in createElements())
		std::ostringstream posStr;
		posStr << "Position: (" << static_cast<int>(camera.position().x) << ", " << static_cast<int>(camera.position().y) << ") Chunk: ("
			   << camera.currentChunk().x << ", " << camera.currentChunk().y << ")";
		positionText->text = posStr.str();

		// Update biome text - get biome from current chunk
		std::ostringstream biomeStr;
		const auto*		   currentChunk = chunkManager.getChunk(camera.currentChunk());
		if (currentChunk != nullptr) {
			biomeStr << "Biome: " << engine::world::biomeToString(currentChunk->primaryBiome());
		} else {
			biomeStr << "Biome: Loading...";
		}
		biomeText->text = biomeStr.str();

		// Update zoom percentage
		if (zoomControl) {
			zoomControl->setZoomPercent(camera.zoomPercent());
		}
	}

	bool GameOverlay::handleEvent(UI::InputEvent& event) {
		if (zoomControl) {
			return zoomControl->handleEvent(event);
		}
		return false;
	}

	void GameOverlay::render() {
		if (chunksText) {
			chunksText->render();
		}
		if (positionText) {
			positionText->render();
		}
		if (biomeText) {
			biomeText->render();
		}
		if (zoomControl) {
			zoomControl->render();
		}
	}

} // namespace world_sim

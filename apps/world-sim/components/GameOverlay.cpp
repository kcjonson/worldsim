#include "GameOverlay.h"

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

		controlsText = std::make_unique<UI::Text>(UI::Text::Args{
			.position = {10.0F, 0.0F}, // Y will be set by layout()
			.text = "WASD to move, Scroll to zoom, ESC for menu",
			.style =
				{
					.color = Foundation::Color(0.6F, 0.6F, 0.6F, 1.0F),
					.fontSize = 14.0F,
					.hAlign = Foundation::HorizontalAlign::Left,
					.vAlign = Foundation::VerticalAlign::Bottom,
				},
			.id = "overlay_controls"
		});

		zoomControl = std::make_unique<ZoomControl>(
			ZoomControl::Args{.position = {10.0F, 55.0F}, .onZoomIn = onZoomIn, .onZoomOut = onZoomOut, .id = "zoom_control"}
		);
	}

	void GameOverlay::layout(const Foundation::Rect& newBounds) {
		viewportBounds = newBounds;

		// Position controls text at bottom of viewport
		if (controlsText) {
			controlsText->position.y = viewportBounds.height - 30.0F;
		}
	}

	void GameOverlay::update(const engine::world::WorldCamera& camera, const engine::world::ChunkManager& chunkManager) {
		// Update chunk count text
		std::ostringstream chunkStr;
		chunkStr << "Chunks: " << chunkManager.loadedChunkCount();
		chunksText = std::make_unique<UI::Text>(UI::Text::Args{
			.position = {10.0F, 10.0F},
			.text = chunkStr.str(),
			.style =
				{
					.color = Foundation::Color::white(),
					.fontSize = 16.0F,
					.hAlign = Foundation::HorizontalAlign::Left,
					.vAlign = Foundation::VerticalAlign::Top,
				},
			.id = "overlay_chunks"
		});

		// Update position text
		std::ostringstream posStr;
		posStr << "Position: (" << static_cast<int>(camera.position().x) << ", " << static_cast<int>(camera.position().y) << ") Chunk: ("
			   << camera.currentChunk().x << ", " << camera.currentChunk().y << ")";
		positionText = std::make_unique<UI::Text>(UI::Text::Args{
			.position = {10.0F, 30.0F},
			.text = posStr.str(),
			.style =
				{
					.color = Foundation::Color::white(),
					.fontSize = 16.0F,
					.hAlign = Foundation::HorizontalAlign::Left,
					.vAlign = Foundation::VerticalAlign::Top,
				},
			.id = "overlay_position"
		});

		// Update zoom percentage
		if (zoomControl) {
			zoomControl->setZoomPercent(camera.zoomPercent());
		}
	}

	void GameOverlay::handleInput() {
		if (zoomControl) {
			zoomControl->handleInput();
		}
	}

	void GameOverlay::render() {
		if (chunksText) {
			chunksText->render();
		}
		if (positionText) {
			positionText->render();
		}
		if (controlsText) {
			controlsText->render();
		}
		if (zoomControl) {
			zoomControl->render();
		}
	}

} // namespace world_sim

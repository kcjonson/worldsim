#include "GameOverlay.h"

#include <primitives/Primitives.h>

#include <sstream>

namespace world_sim {

GameOverlay::GameOverlay(const Args& /*args*/) {
	createTextElements();
}

void GameOverlay::createTextElements() {
	int viewportWidth = 0;
	Renderer::Primitives::getViewport(viewportWidth, m_viewportHeight);

	m_chunksText = std::make_unique<UI::Text>(UI::Text::Args{
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

	m_positionText = std::make_unique<UI::Text>(UI::Text::Args{
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

	m_controlsText = std::make_unique<UI::Text>(UI::Text::Args{
		.position = {10.0F, static_cast<float>(m_viewportHeight) - 30.0F},
		.text = "WASD to move, ESC for menu",
		.style =
			{
				.color = Foundation::Color(0.6F, 0.6F, 0.6F, 1.0F),
				.fontSize = 14.0F,
				.hAlign = Foundation::HorizontalAlign::Left,
				.vAlign = Foundation::VerticalAlign::Bottom,
			},
		.id = "overlay_controls"
	});
}

void GameOverlay::update(const engine::world::WorldCamera& camera,
						 const engine::world::ChunkManager& chunkManager) {
	// Update chunk count text
	std::ostringstream chunkText;
	chunkText << "Chunks: " << chunkManager.loadedChunkCount();
	m_chunksText = std::make_unique<UI::Text>(UI::Text::Args{
		.position = {10.0F, 10.0F},
		.text = chunkText.str(),
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
	std::ostringstream posText;
	posText << "Position: (" << static_cast<int>(camera.position().x) << ", "
			<< static_cast<int>(camera.position().y) << ") Chunk: (" << camera.currentChunk().x << ", "
			<< camera.currentChunk().y << ")";
	m_positionText = std::make_unique<UI::Text>(UI::Text::Args{
		.position = {10.0F, 30.0F},
		.text = posText.str(),
		.style =
			{
				.color = Foundation::Color::white(),
				.fontSize = 16.0F,
				.hAlign = Foundation::HorizontalAlign::Left,
				.vAlign = Foundation::VerticalAlign::Top,
			},
		.id = "overlay_position"
	});
}

void GameOverlay::render() {
	if (m_chunksText) {
		m_chunksText->render();
	}
	if (m_positionText) {
		m_positionText->render();
	}
	if (m_controlsText) {
		m_controlsText->render();
	}
}

}  // namespace world_sim

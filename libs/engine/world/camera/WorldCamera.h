#pragma once

// WorldCamera - 2D camera for panning around the world.
// Handles WASD/arrow key movement and provides view matrix for rendering.

#include "world/chunk/ChunkCoordinate.h"

#include <graphics/Rect.h>
#include <math/Types.h>

namespace engine::world {

/// Camera for 2D world view with panning support.
class WorldCamera {
  public:
	WorldCamera() = default;

	/// Set camera position (world coordinates)
	void setPosition(WorldPosition pos) {
		m_position = pos;
		m_targetPosition = pos;
	}

	/// Get current camera position
	[[nodiscard]] WorldPosition position() const { return m_position; }

	/// Get the chunk the camera is currently in
	[[nodiscard]] ChunkCoordinate currentChunk() const { return worldToChunk(m_position); }

	/// Set pan speed (world units per second)
	void setPanSpeed(float speed) { m_panSpeed = speed; }
	[[nodiscard]] float panSpeed() const { return m_panSpeed; }

	/// Set zoom level (1.0 = normal, >1 = zoomed in, <1 = zoomed out)
	void setZoom(float zoom) { m_zoom = std::max(0.1F, std::min(10.0F, zoom)); }
	[[nodiscard]] float zoom() const { return m_zoom; }

	/// Movement input (call each frame)
	/// @param dx Horizontal movement (-1 = left, +1 = right)
	/// @param dy Vertical movement (-1 = down, +1 = up)
	/// @param dt Delta time in seconds
	void move(float dx, float dy, float dt) {
		m_targetPosition.x += dx * m_panSpeed * dt;
		m_targetPosition.y += dy * m_panSpeed * dt;
	}

	/// Update camera position with smoothing
	/// @param dt Delta time in seconds
	void update(float dt) {
		// Smooth movement towards target (simple lerp)
		constexpr float kSmoothFactor = 10.0F;
		float t = std::min(1.0F, kSmoothFactor * dt);
		m_position.x += (m_targetPosition.x - m_position.x) * t;
		m_position.y += (m_targetPosition.y - m_position.y) * t;
	}

	/// Get visible world rectangle (in world coordinates)
	/// @param viewportWidth Viewport width in pixels
	/// @param viewportHeight Viewport height in pixels
	/// @param pixelsPerMeter Scale factor (pixels per world unit)
	[[nodiscard]] Foundation::Rect getVisibleRect(int viewportWidth, int viewportHeight, float pixelsPerMeter) const {
		float worldWidth = static_cast<float>(viewportWidth) / (pixelsPerMeter * m_zoom);
		float worldHeight = static_cast<float>(viewportHeight) / (pixelsPerMeter * m_zoom);

		return Foundation::Rect{
			m_position.x - worldWidth * 0.5F,
			m_position.y - worldHeight * 0.5F,
			worldWidth,
			worldHeight
		};
	}

	/// Get corners of visible world rectangle
	[[nodiscard]] std::pair<WorldPosition, WorldPosition>
	getVisibleCorners(int viewportWidth, int viewportHeight, float pixelsPerMeter) const {
		Foundation::Rect rect = getVisibleRect(viewportWidth, viewportHeight, pixelsPerMeter);
		return {
			WorldPosition{rect.x, rect.y},
			WorldPosition{rect.x + rect.width, rect.y + rect.height}
		};
	}

  private:
	WorldPosition m_position{0.0F, 0.0F};
	WorldPosition m_targetPosition{0.0F, 0.0F};
	float m_panSpeed = 500.0F;	// World units per second
	float m_zoom = 1.0F;
};

}  // namespace engine::world

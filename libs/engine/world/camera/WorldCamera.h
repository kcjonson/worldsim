#pragma once

// WorldCamera - 2D camera for panning around the world.
// Handles WASD/arrow key movement and provides view matrix for rendering.

#include "world/chunk/ChunkCoordinate.h"

#include <graphics/Rect.h>
#include <math/Types.h>

#include <array>
#include <cstddef>

namespace engine::world {

/// Predefined zoom levels for snap-to-zoom behavior
inline constexpr std::array<float, 13> kZoomLevels = {
	0.25F, 0.5F, 0.75F, 1.0F, 1.5F, 2.0F, 3.0F, 4.0F, 6.0F, 8.0F, 10.0F, 15.0F, 20.0F
};
inline constexpr size_t kDefaultZoomIndex = 6;  // 3.0x (displays as 100%)

/// Camera for 2D world view with panning support.
class WorldCamera {
  public:
	WorldCamera() : m_zoomIndex(kDefaultZoomIndex), m_zoom(kZoomLevels[kDefaultZoomIndex]) {}

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
	void setZoom(float zoom) { m_zoom = std::max(0.1F, std::min(25.0F, zoom)); }
	[[nodiscard]] float zoom() const { return m_zoom; }

	/// Get current zoom index in kZoomLevels
	[[nodiscard]] size_t zoomIndex() const { return m_zoomIndex; }

	/// Zoom in one step (increase zoom level index)
	void zoomIn() {
		if (m_zoomIndex < kZoomLevels.size() - 1) {
			m_zoomIndex++;
			m_zoom = kZoomLevels[m_zoomIndex];
		}
	}

	/// Zoom out one step (decrease zoom level index)
	void zoomOut() {
		if (m_zoomIndex > 0) {
			m_zoomIndex--;
			m_zoom = kZoomLevels[m_zoomIndex];
		}
	}

	/// Set zoom to specific index
	void setZoomIndex(size_t index) {
		m_zoomIndex = std::min(index, kZoomLevels.size() - 1);
		m_zoom = kZoomLevels[m_zoomIndex];
	}

	/// Get zoom as percentage integer (100 = 3.0x, which is the "normal" view)
	[[nodiscard]] int zoomPercent() const { return static_cast<int>((m_zoom / 3.0F) * 100.0F); }

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

	/// Convert screen coordinates to world coordinates
	/// @param screenX Screen X position in pixels (0 = left)
	/// @param screenY Screen Y position in pixels (0 = top)
	/// @param viewportWidth Viewport width in pixels
	/// @param viewportHeight Viewport height in pixels
	/// @param pixelsPerMeter Scale factor (pixels per world unit)
	[[nodiscard]] WorldPosition screenToWorld(float screenX, float screenY, int viewportWidth, int viewportHeight, float pixelsPerMeter) const {
		Foundation::Rect rect = getVisibleRect(viewportWidth, viewportHeight, pixelsPerMeter);
		float normalizedX = screenX / static_cast<float>(viewportWidth);
		float normalizedY = screenY / static_cast<float>(viewportHeight);
		return WorldPosition{
			rect.x + normalizedX * rect.width,
			rect.y + normalizedY * rect.height
		};
	}

	/// Convert world coordinates to screen coordinates
	/// @param worldX World X position
	/// @param worldY World Y position
	/// @param viewportWidth Viewport width in pixels
	/// @param viewportHeight Viewport height in pixels
	/// @param pixelsPerMeter Scale factor (pixels per world unit)
	[[nodiscard]] Foundation::Vec2 worldToScreen(float worldX, float worldY, int viewportWidth, int viewportHeight, float pixelsPerMeter) const {
		Foundation::Rect rect = getVisibleRect(viewportWidth, viewportHeight, pixelsPerMeter);
		float normalizedX = (worldX - rect.x) / rect.width;
		float normalizedY = (worldY - rect.y) / rect.height;
		return Foundation::Vec2{
			normalizedX * static_cast<float>(viewportWidth),
			normalizedY * static_cast<float>(viewportHeight)
		};
	}

	/// Convert a world-space distance to screen pixels
	/// @param worldDistance Distance in world units (meters)
	/// @param pixelsPerMeter Scale factor (pixels per world unit)
	[[nodiscard]] float worldDistanceToScreen(float worldDistance, float pixelsPerMeter) const {
		return worldDistance * pixelsPerMeter * m_zoom;
	}

  private:
	WorldPosition m_position{0.0F, 0.0F};
	WorldPosition m_targetPosition{0.0F, 0.0F};
	float m_panSpeed = 500.0F;	// World units per second
	float m_zoom = 3.0F;
	size_t m_zoomIndex = kDefaultZoomIndex;
};

}  // namespace engine::world

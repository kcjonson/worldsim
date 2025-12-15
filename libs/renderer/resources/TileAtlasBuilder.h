#pragma once

#include "resources/TileTextureAtlas.h"

#include <GL/glew.h>
#include <glm/vec4.hpp>
#include <graphics/Color.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Renderer {

// Builds a tile texture atlas by loading SVG patterns or generating fallbacks.
// Extracts atlas building logic from AppLauncher into a reusable class.
//
// Design: Uses callbacks for surface info to avoid renderer depending on engine.
// The caller (AppLauncher) provides surface names and colors.
class TileAtlasBuilder {
  public:
	struct Config {
		int patternSize = 512;	// Size of each pattern (square)
		int atlasSize = 2048;	// Total atlas texture size
	};

	// Default configuration values
	static constexpr Config kDefaultConfig{512, 2048};

	// Callbacks for surface information (provided by engine layer)
	using SurfaceNameFn = std::function<std::string(int surfaceId)>;
	using SurfaceColorFn = std::function<Foundation::Color(int surfaceId)>;

	explicit TileAtlasBuilder(const Config& config = kDefaultConfig);
	~TileAtlasBuilder() = default;

	TileAtlasBuilder(const TileAtlasBuilder&) = delete;
	TileAtlasBuilder& operator=(const TileAtlasBuilder&) = delete;
	TileAtlasBuilder(TileAtlasBuilder&&) = default;
	TileAtlasBuilder& operator=(TileAtlasBuilder&&) = default;

	// Build atlas for all surface types, returns UV rects for each surface.
	// UV rects are stored as (u0, v0, u1, v1) where u0,v0 is top-left and u1,v1 is bottom-right.
	// @param surfaceCount Number of surface types to allocate
	// @param surfaceNameFn Callback to get surface name for SVG path lookup
	// @param surfaceColorFn Callback to get fallback color for surface
	[[nodiscard]] std::vector<glm::vec4> buildForSurfaces(
		int surfaceCount,
		const SurfaceNameFn& surfaceNameFn,
		const SurfaceColorFn& surfaceColorFn);

	// Get the underlying atlas texture handle.
	[[nodiscard]] GLuint texture() const;

	// Get the atlas size.
	[[nodiscard]] int atlasSize() const { return config_.atlasSize; }

  private:
	// Load an SVG pattern for the given surface, returns true if successful.
	bool loadSvgPattern(const std::string& surfaceName, std::vector<uint8_t>& pixels);

	// Generate a checkerboard fallback pattern for the given surface color.
	void generateFallbackPattern(const Foundation::Color& color, std::vector<uint8_t>& pixels, int width, int height);

	// Calculate UV rectangle for an allocated atlas region.
	[[nodiscard]] glm::vec4 calculateUVRect(const AtlasRegion& region) const;

	Config config_;
	std::unique_ptr<TileTextureAtlas> atlas_;
};

} // namespace Renderer

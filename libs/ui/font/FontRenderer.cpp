// Font rendering implementation using MSDF (Multi-channel Signed Distance Field) atlas

#include "font/FontRenderer.h"
#include "utils/Log.h"
#include "utils/ResourcePath.h"
#include <cstdlib>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <stb_image.h>

namespace ui {

	FontRenderer::FontRenderer() = default;

	FontRenderer::~FontRenderer() {
		// Clean up SDF atlas texture
		if (atlasTexture != 0) {
			glDeleteTextures(1, &atlasTexture);
		}
	}

	bool FontRenderer::Initialize() {
		LOG_INFO(UI, "Initializing FontRenderer...");

		// Resolve font paths using resource finder (handles invalid cwd from IDEs)
		std::string sdfAtlasPath = Foundation::findResourceString("fonts/Roboto-SDF.png");
		std::string sdfMetadataPath = Foundation::findResourceString("fonts/Roboto-SDF.json");

		if (sdfAtlasPath.empty() || sdfMetadataPath.empty()) {
			LOG_ERROR(UI, "FATAL ERROR: SDF atlas files not found");
			std::exit(1);
		}

		if (!LoadSDFAtlas(sdfAtlasPath.c_str(), sdfMetadataPath.c_str())) {
			LOG_ERROR(UI, "FATAL ERROR: Failed to load SDF atlas");
			std::exit(1);
		}

		LOG_INFO(UI, "FontRenderer initialization complete (SDF atlas mode)");
		return true;
	}

	glm::vec2 FontRenderer::MeasureText(const std::string& text, float scale) const {
		if (text.empty()) {
			return glm::vec2(0.0F, 0.0F);
		}

		constexpr float BASE_FONT_SIZE = 16.0F; // scale=1.0 renders at this size
		float			totalWidth = 0.0F;
		float			fontSize = BASE_FONT_SIZE * scale; // Requested rendering size, not atlas size

		for (char c : text) {
			auto it = sdfGlyphs.find(c);
			if (it != sdfGlyphs.end()) {
				totalWidth += it->second.advance * fontSize;
			} else {
				// Fallback to '?' if character not found
				auto fallbackIt = sdfGlyphs.find('?');
				if (fallbackIt != sdfGlyphs.end()) {
					totalWidth += fallbackIt->second.advance * fontSize;
				}
			}
		}

		// For height, use the line height from atlas metadata
		float textHeight = atlasMetadata.lineHeight * fontSize;

		return glm::vec2(totalWidth, textHeight);
	}

	float FontRenderer::getMaxGlyphHeight(float scale) const {
		return maxGlyphHeightUnscaled * scale;
	}

	float FontRenderer::getAscent(float scale) const {
		return scaledAscender * scale;
	}

	bool FontRenderer::LoadSDFAtlas(const std::string& pngPath, const std::string& jsonPath) {
		LOG_INFO(UI, "Loading SDF atlas from: %s", pngPath.c_str());
		LOG_INFO(UI, "Loading SDF metadata from: %s", jsonPath.c_str());

		// Load JSON metadata
		std::ifstream jsonFile(jsonPath);
		if (!jsonFile.is_open()) {
			LOG_ERROR(UI, "Failed to open SDF metadata file: %s", jsonPath.c_str());
			return false;
		}

		nlohmann::json json;
		try {
			jsonFile >> json;
		} catch (const std::exception& e) {
			LOG_ERROR(UI, "Failed to parse SDF metadata JSON: %s", e.what());
			return false;
		}

		// Parse atlas metadata with error handling
		try {
			if (!json.contains("atlas") || !json.contains("metrics") || !json.contains("glyphs")) {
				LOG_ERROR(UI, "SDF metadata JSON missing required fields (atlas, metrics, or glyphs)");
				return false;
			}

			atlasMetadata.distanceRange = json["atlas"]["distanceRange"].get<float>();
			atlasMetadata.glyphSize = json["atlas"]["size"].get<int>();
			atlasMetadata.atlasWidth = json["atlas"]["width"].get<int>();
			atlasMetadata.atlasHeight = json["atlas"]["height"].get<int>();
			atlasMetadata.emSize = json["metrics"]["emSize"].get<float>();
			atlasMetadata.ascender = json["metrics"]["ascender"].get<float>();
			atlasMetadata.descender = json["metrics"]["descender"].get<float>();
			atlasMetadata.lineHeight = json["metrics"]["lineHeight"].get<float>();
		} catch (const std::exception& e) {
			LOG_ERROR(UI, "Failed to parse SDF atlas metadata: %s", e.what());
			return false;
		}

		LOG_INFO(
			UI,
			"Atlas metadata: size=%dx%d, glyphSize=%d, range=%.1f",
			atlasMetadata.atlasWidth,
			atlasMetadata.atlasHeight,
			atlasMetadata.glyphSize,
			atlasMetadata.distanceRange
		);

		// Parse glyphs with error handling
		try {
			const auto& glyphsJson = json["glyphs"];
			for (auto it = glyphsJson.begin(); it != glyphsJson.end(); ++it) {
				const std::string& key = it.key();
				if (key.empty())
					continue;

				char		c = key[0]; // Get first character (handles escaped chars)
				const auto& glyphJson = it.value();

				if (!glyphJson.contains("advance")) {
					LOG_WARNING(UI, "Glyph '%c' missing advance field, skipping", c);
					continue;
				}

				SDFGlyph glyph{};
				glyph.advance = glyphJson["advance"].get<float>();

				// Check if glyph has geometry (not whitespace)
				if (glyphJson.contains("atlas") && !glyphJson["atlas"].is_null()) {
					glyph.hasGeometry = true;

					// Atlas UV coordinates (normalized 0-1) - full allocated cell
					glyph.atlasUVMin.x = glyphJson["atlas"]["x"].get<float>();
					glyph.atlasUVMin.y = glyphJson["atlas"]["y"].get<float>();
					glyph.atlasUVMax.x = glyph.atlasUVMin.x + glyphJson["atlas"]["width"].get<float>();
					glyph.atlasUVMax.y = glyph.atlasUVMin.y + glyphJson["atlas"]["height"].get<float>();

					// Atlas bounds UV coordinates (normalized 0-1) - actual glyph content
					// Reference: https://github.com/Chlumsky/msdf-atlas-gen/issues/2
					// atlasBounds defines where the actual rendered glyph is within the cell
					// Fall back to full cell if atlasBounds not present (older atlas format)
					if (glyphJson.contains("atlasBounds") && !glyphJson["atlasBounds"].is_null()) {
						glyph.atlasBoundsMin.x = glyphJson["atlasBounds"]["left"].get<float>();
						glyph.atlasBoundsMin.y = glyphJson["atlasBounds"]["bottom"].get<float>();
						glyph.atlasBoundsMax.x = glyphJson["atlasBounds"]["right"].get<float>();
						glyph.atlasBoundsMax.y = glyphJson["atlasBounds"]["top"].get<float>();
					} else {
						// Fallback: use full atlas cell if atlasBounds not available
						glyph.atlasBoundsMin = glyph.atlasUVMin;
						glyph.atlasBoundsMax = glyph.atlasUVMax;
					}

					// Plane bounds (in em units)
					if (glyphJson.contains("plane") && !glyphJson["plane"].is_null()) {
						glyph.planeBoundsMin.x = glyphJson["plane"]["left"].get<float>();
						glyph.planeBoundsMin.y = glyphJson["plane"]["bottom"].get<float>();
						glyph.planeBoundsMax.x = glyphJson["plane"]["right"].get<float>();
						glyph.planeBoundsMax.y = glyphJson["plane"]["top"].get<float>();
					}
				} else {
					glyph.hasGeometry = false;
				}

				sdfGlyphs[c] = glyph;
			}
		} catch (const std::exception& e) {
			LOG_ERROR(UI, "Failed to parse SDF glyphs: %s", e.what());
			return false;
		}

		LOG_INFO(UI, "Loaded %zu SDF glyphs", sdfGlyphs.size());

		// Load PNG atlas texture using stb_image
		int width = 0;
		int height = 0;
		int channels = 0;
		// OpenGL expects (0,0) at bottom-left, but images are stored with (0,0) at top-left
		// Flip vertically so texture coordinates match
		stbi_set_flip_vertically_on_load(1);

		unsigned char* imageDataRaw = stbi_load(pngPath.c_str(), &width, &height, &channels, 3); // Force RGB

		if (!imageDataRaw) {
			LOG_ERROR(UI, "Failed to load SDF atlas texture: %s", pngPath.c_str());
			return false;
		}

		// Use RAII to ensure imageData is freed even if OpenGL operations fail
		std::unique_ptr<unsigned char, decltype(&stbi_image_free)> imageData(imageDataRaw, stbi_image_free);

		LOG_INFO(UI, "Loaded atlas texture: %dx%d, %d channels", width, height, channels);

		// Create OpenGL texture
		glGenTextures(1, &atlasTexture);
		glBindTexture(GL_TEXTURE_2D, atlasTexture);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, imageData.get());

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glBindTexture(GL_TEXTURE_2D, 0);

		// imageData automatically freed by unique_ptr

		// Update font metrics for compatibility with existing code
		// Use BASE_FONT_SIZE (16px) not glyphSize (32px) since atlas is 2x oversampled for quality
		constexpr float BASE_FONT_SIZE = 16.0F;
		scaledAscender = atlasMetadata.ascender * BASE_FONT_SIZE;
		maxGlyphHeightUnscaled = atlasMetadata.lineHeight;

		LOG_INFO(UI, "SDF atlas loaded successfully");
		return true;
	}

	void FontRenderer::generateGlyphQuads(
		const std::string&		text,
		const glm::vec2&		position,
		float					scale,
		const glm::vec4&		color,
		std::vector<GlyphQuad>& outQuads
	) const {
		// Try cache lookup if enabled
		if (FontRendererConfig::kEnableGlyphQuadCache) {
			CacheKey key{text, scale};
			auto	 it = glyphQuadCache.find(key);

			if (it != glyphQuadCache.end()) {
				// Cache hit! Copy quads and adjust position/color
				const std::vector<GlyphQuad>& cachedQuads = it->second.quads;

				// Update LRU tracking
				it->second.lastAccessFrame = currentFrame;

				// Copy quads with position/color adjustment
				outQuads.reserve(outQuads.size() + cachedQuads.size());
				for (const auto& cachedQuad : cachedQuads) {
					GlyphQuad quad = cachedQuad;
					// Adjust position (cached quads are relative to origin)
					quad.position += position;
					// Update color (cached quads have color from first render)
					quad.color = color;
					outQuads.push_back(quad);
				}

				return; // Cache hit, done!
			}
		}

		// Cache miss or caching disabled - generate quads
		size_t startIdx = outQuads.size(); // Track where we started adding

		// Calculate baseline position
		// IMPORTANT: fontSize should be the REQUESTED rendering size, not atlas glyph size!
		// The atlas may be generated at higher resolution (e.g., 32px) for quality,
		// but scale=1.0 should render at BASE_FONT_SIZE (16px), not glyphSize (32px).
		// The glyph metrics are in EM units, so we scale by the requested pixel size.
		constexpr float BASE_FONT_SIZE = 16.0F; // scale=1.0 renders at this size
		float			fontSize = BASE_FONT_SIZE * scale; // Requested rendering size in pixels
		float			ascenderAtCurrentScale = atlasMetadata.ascender * fontSize;

		glm::vec2 penPosition = glm::vec2(0, 0); // Generate relative to origin for caching
		penPosition.y += ascenderAtCurrentScale; // Move to baseline

		for (char currentChar : text) {
			auto			it = sdfGlyphs.find(currentChar);
			const SDFGlyph* glyphPtr = nullptr;

			if (it != sdfGlyphs.end()) {
				glyphPtr = &it->second;
			} else {
				// Fallback to '?' if character not found
				auto fallbackIt = sdfGlyphs.find('?');
				if (fallbackIt != sdfGlyphs.end()) {
					glyphPtr = &fallbackIt->second;
				}
			}

			if (!glyphPtr) {
				continue; // Skip if no valid glyph or fallback
			}

			const SDFGlyph& glyph = *glyphPtr;

			// Only generate quad if glyph has geometry (not whitespace)
			if (glyph.hasGeometry) {
				// Calculate quad position in screen space
				float xpos = penPosition.x + glyph.planeBoundsMin.x * fontSize;
				float ypos = penPosition.y - glyph.planeBoundsMax.y * fontSize; // Top-left corner

				float w = (glyph.planeBoundsMax.x - glyph.planeBoundsMin.x) * fontSize;
				float h = (glyph.planeBoundsMax.y - glyph.planeBoundsMin.y) * fontSize;

				// Create glyph quad
				// Use atlasBounds (actual glyph content) instead of atlas (full cell)
				// Reference: https://github.com/Chlumsky/msdf-atlas-gen/issues/2
				// This ensures we only sample the actual glyph pixels, not the empty padding
				GlyphQuad quad{};
				quad.position = glm::vec2(xpos, ypos);
				quad.size = glm::vec2(w, h);
				quad.uvMin = glyph.atlasBoundsMin;
				quad.uvMax = glyph.atlasBoundsMax;
				quad.color = color;

				outQuads.push_back(quad);
			}

			// Advance pen position
			penPosition.x += glyph.advance * fontSize;
		}

		// Cache the generated quads if enabled
		if (FontRendererConfig::kEnableGlyphQuadCache) {
			// Check if cache is full and needs eviction
			if (glyphQuadCache.size() >= FontRendererConfig::kMaxGlyphQuadCacheEntries) {
				// Find and evict the LRU entry
				auto oldestIt = glyphQuadCache.begin();
				for (auto it = glyphQuadCache.begin(); it != glyphQuadCache.end(); ++it) {
					if (it->second.lastAccessFrame < oldestIt->second.lastAccessFrame) {
						oldestIt = it;
					}
				}
				glyphQuadCache.erase(oldestIt);
			}

			// Cache the generated quads (relative to origin, before position adjustment)
			CacheEntry entry;
			entry.lastAccessFrame = currentFrame;
			entry.quads.reserve(outQuads.size() - startIdx);
			for (size_t i = startIdx; i < outQuads.size(); ++i) {
				entry.quads.push_back(outQuads[i]);
			}

			CacheKey key{text, scale};
			glyphQuadCache[key] = std::move(entry);

			// Now adjust positions in outQuads for the caller
			for (size_t i = startIdx; i < outQuads.size(); ++i) {
				outQuads[i].position += position;
			}
		} else {
			// No caching - just adjust positions
			for (size_t i = startIdx; i < outQuads.size(); ++i) {
				outQuads[i].position += position;
			}
		}
	}

	GLuint FontRenderer::getAtlasTexture() const {
		return atlasTexture;
	}

	void FontRenderer::updateFrame() {
		currentFrame++;
	}

	void FontRenderer::clearGlyphQuadCache() {
		glyphQuadCache.clear();
		LOG_DEBUG(UI, "Cleared glyph quad cache");
	}

	size_t FontRenderer::getGlyphQuadCacheSize() const {
		return glyphQuadCache.size();
	}

} // namespace ui

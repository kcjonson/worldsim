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

	namespace {
		// Atlas file basenames per FontFamily, indexed by static_cast<int>(family).
		// Order must match the FontFamily enum in libs/renderer.
		constexpr const char* kAtlasBaseNames[Renderer::kFontFamilyCount] = {
			"Roboto-SDF",		// FontFamily::Roboto
			"ChakraPetch-SDF",	// FontFamily::ChakraPetch
			"Barlow-SDF",		// FontFamily::Barlow
			"JetBrainsMono-SDF" // FontFamily::JetBrainsMono
		};

		// The single advance iteration shared by MeasureText and generateGlyphQuads,
		// so measured width always equals rendered width (alignment depends on it).
		// Resolves each char with '?' fallback, invokes fn(glyph, penX) at the
		// glyph's pen position, and returns the final pen x. Letter-spacing sits
		// between glyphs only (n glyphs -> n-1 gaps).
		template <typename GlyphMap, typename Fn>
		float ForEachGlyph(const GlyphMap& glyphs, const std::string& text, float fontSize, float letterSpacing, Fn&& fn) {
			float penX = 0.0F;
			for (size_t i = 0; i < text.size(); ++i) {
				auto it = glyphs.find(text[i]);
				if (it == glyphs.end()) {
					it = glyphs.find('?');
				}
				if (it != glyphs.end()) {
					fn(it->second, penX);
					penX += it->second.advance * fontSize;
				}
				if (i + 1 < text.size()) {
					penX += letterSpacing;
				}
			}
			return penX;
		}
	} // namespace

	FontRenderer::FontRenderer() = default;

	FontRenderer::~FontRenderer() {
		// Clean up every loaded atlas texture
		for (Atlas& atlas : atlases) {
			if (atlas.texture != 0) {
				glDeleteTextures(1, &atlas.texture);
				atlas.texture = 0;
			}
		}
	}

	bool FontRenderer::Initialize() {
		LOG_INFO(UI, "Initializing FontRenderer...");

		// Load every font family's atlas. Roboto (index 0) is the default used by
		// all existing callers, so a missing/broken Roboto atlas is fatal. The
		// other families are best-effort: warn and continue so Roboto still works.
		for (int i = 0; i < Renderer::kFontFamilyCount; ++i) {
			const std::string pngName = std::string("fonts/") + kAtlasBaseNames[i] + ".png";
			const std::string jsonName = std::string("fonts/") + kAtlasBaseNames[i] + ".json";

			// Resolve paths using resource finder (handles invalid cwd from IDEs)
			std::string pngPath = Foundation::findResourceString(pngName);
			std::string jsonPath = Foundation::findResourceString(jsonName);

			const bool isDefault = (i == static_cast<int>(Renderer::FontFamily::Roboto));

			if (pngPath.empty() || jsonPath.empty()) {
				if (isDefault) {
					LOG_ERROR(UI, "FATAL ERROR: default SDF atlas files not found (%s)", kAtlasBaseNames[i]);
					std::exit(1);
				}
				LOG_WARNING(UI, "SDF atlas '%s' not found, skipping (font family unavailable)", kAtlasBaseNames[i]);
				continue;
			}

			if (!LoadSDFAtlas(atlases[i], pngPath, jsonPath)) {
				if (isDefault) {
					LOG_ERROR(UI, "FATAL ERROR: failed to load default SDF atlas (%s)", kAtlasBaseNames[i]);
					std::exit(1);
				}
				LOG_WARNING(UI, "Failed to load SDF atlas '%s', skipping (font family unavailable)", kAtlasBaseNames[i]);
				continue;
			}
		}

		LOG_INFO(UI, "FontRenderer initialization complete (SDF atlas mode)");
		return true;
	}

	const FontRenderer::Atlas& FontRenderer::atlasFor(Renderer::FontFamily family) const {
		const Atlas& requested = atlases[static_cast<int>(family)];
		if (requested.loaded) {
			return requested;
		}
		// Requested family failed to load; fall back to Roboto, which is guaranteed
		// loaded (Initialize exits otherwise).
		return atlases[static_cast<int>(Renderer::FontFamily::Roboto)];
	}

	glm::vec2 FontRenderer::MeasureText(const std::string& text, float scale, Renderer::FontFamily family, float letterSpacing) const {
		if (text.empty()) {
			return glm::vec2(0.0F, 0.0F);
		}

		const Atlas& atlas = atlasFor(family);

		constexpr float BASE_FONT_SIZE = 16.0F;			   // scale=1.0 renders at this size
		float			fontSize = BASE_FONT_SIZE * scale; // Requested rendering size, not atlas size

		float totalWidth = ForEachGlyph(atlas.glyphs, text, fontSize, letterSpacing, [](const auto&, float) {});

		// For height, use the line height from atlas metadata
		float textHeight = atlas.metadata.lineHeight * fontSize;

		return glm::vec2(totalWidth, textHeight);
	}

	float FontRenderer::getMaxGlyphHeight(float scale, Renderer::FontFamily family) const {
		return atlasFor(family).maxGlyphHeightUnscaled * scale;
	}

	float FontRenderer::getAscent(float scale, Renderer::FontFamily family) const {
		return atlasFor(family).scaledAscender * scale;
	}

	bool FontRenderer::LoadSDFAtlas(Atlas& atlas, const std::string& pngPath, const std::string& jsonPath) {
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

		SDFAtlasMetadata& atlasMetadata = atlas.metadata;

		// Parse atlas metadata with error handling
		try {
			if (!json.contains("atlas") || !json.contains("metrics") || !json.contains("glyphs")) {
				LOG_ERROR(UI, "SDF metadata JSON missing required fields (atlas, metrics, or glyphs)");
				return false;
			}

			for (const char* field : {"distanceRange", "size", "width", "height"}) {
				if (!json["atlas"].contains(field)) {
					LOG_ERROR(UI, "SDF metadata missing atlas.%s: %s", field, jsonPath.c_str());
					return false;
				}
			}
			for (const char* field : {"emSize", "ascender", "descender", "lineHeight"}) {
				if (!json["metrics"].contains(field)) {
					LOG_ERROR(UI, "SDF metadata missing metrics.%s: %s", field, jsonPath.c_str());
					return false;
				}
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

		std::map<char, SDFGlyph>& sdfGlyphs = atlas.glyphs;

		// Parse glyphs with error handling
		int missingAtlasBounds = 0;
		int missingPlaneBounds = 0;
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

				// A null "atlas" marks whitespace (advance only). A non-null one
				// requires atlasBounds (actual glyph content within the cell,
				// https://github.com/Chlumsky/msdf-atlas-gen/issues/2) and plane
				// bounds; sampling the full cell instead would bleed neighboring
				// glyphs, so incomplete glyphs are kept advance-only.
				if (glyphJson.contains("atlas") && !glyphJson["atlas"].is_null()) {
					const bool hasAtlasBounds = glyphJson.contains("atlasBounds") && !glyphJson["atlasBounds"].is_null();
					const bool hasPlaneBounds = glyphJson.contains("plane") && !glyphJson["plane"].is_null();
					if (hasAtlasBounds && hasPlaneBounds) {
						glyph.hasGeometry = true;
						glyph.atlasBoundsMin.x = glyphJson["atlasBounds"]["left"].get<float>();
						glyph.atlasBoundsMin.y = glyphJson["atlasBounds"]["bottom"].get<float>();
						glyph.atlasBoundsMax.x = glyphJson["atlasBounds"]["right"].get<float>();
						glyph.atlasBoundsMax.y = glyphJson["atlasBounds"]["top"].get<float>();
						glyph.planeBoundsMin.x = glyphJson["plane"]["left"].get<float>();
						glyph.planeBoundsMin.y = glyphJson["plane"]["bottom"].get<float>();
						glyph.planeBoundsMax.x = glyphJson["plane"]["right"].get<float>();
						glyph.planeBoundsMax.y = glyphJson["plane"]["top"].get<float>();
					} else {
						missingAtlasBounds += hasAtlasBounds ? 0 : 1;
						missingPlaneBounds += hasPlaneBounds ? 0 : 1;
						glyph.hasGeometry = false;
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

		if (missingAtlasBounds > 0 || missingPlaneBounds > 0) {
			LOG_WARNING(
				UI,
				"SDF atlas %s: %d glyphs missing atlasBounds, %d missing plane bounds (rendered as whitespace)",
				jsonPath.c_str(),
				missingAtlasBounds,
				missingPlaneBounds
			);
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
		glGenTextures(1, &atlas.texture);
		glBindTexture(GL_TEXTURE_2D, atlas.texture);

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
		atlas.scaledAscender = atlasMetadata.ascender * BASE_FONT_SIZE;
		atlas.maxGlyphHeightUnscaled = atlasMetadata.lineHeight;
		atlas.loaded = true;

		LOG_INFO(UI, "SDF atlas loaded successfully");
		return true;
	}

	void FontRenderer::generateGlyphQuads(
		const std::string&		text,
		const glm::vec2&		position,
		float					scale,
		const glm::vec4&		color,
		std::vector<GlyphQuad>& outQuads,
		Renderer::FontFamily	family,
		float					letterSpacing
	) const {
		const Atlas&					atlas = atlasFor(family);
		const std::map<char, SDFGlyph>& sdfGlyphs = atlas.glyphs;

		// IMPORTANT: fontSize should be the REQUESTED rendering size, not atlas glyph size!
		// The atlas may be generated at higher resolution (e.g., 32px) for quality,
		// but scale=1.0 should render at BASE_FONT_SIZE (16px), not glyphSize (32px).
		// The glyph metrics are in EM units, so we scale by the requested pixel size.
		constexpr float BASE_FONT_SIZE = 16.0F;							// scale=1.0 renders at this size
		float			fontSize = BASE_FONT_SIZE * scale;				// Requested rendering size in pixels
		float			baselineY = atlas.metadata.ascender * fontSize; // Relative to origin for caching

		// The run origin the renderer snaps to the pixel grid is the pen origin AT
		// THE BASELINE (not the box top-left): aligning the baseline is what makes
		// stroke bottoms land on pixel boundaries, and it's shared by every glyph
		// of the run so advances/kerning are untouched.
		const glm::vec2 penOrigin = position + glm::vec2(0.0F, baselineY);

		// Try cache lookup if enabled
		if (FontRendererConfig::kEnableGlyphQuadCache) {
			CacheKey key{family, text, scale, letterSpacing};
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
					quad.runOrigin = penOrigin;
					// Update color (cached quads have color from first render)
					quad.color = color;
					outQuads.push_back(quad);
				}

				return; // Cache hit, done!
			}
		}

		// Cache miss or caching disabled - generate quads
		size_t startIdx = outQuads.size(); // Track where we started adding

		// atlasBounds/planeBounds are the tight glyph outline, but the MSDF stays
		// valid for distanceRange/2 texels beyond it, and that half holds the
		// anti-aliased edge falloff. A quad clipped to the tight bounds cuts that
		// falloff at its own (clamped) UV edge: invisible at large sizes, but at
		// small sizes baseline snapping lands a cap top on an unlucky subpixel and
		// the top AA row vanishes, shearing glyph tops flat and clipping the trailing
		// glyph. Grow each quad by the valid half-range (not the full range, whose
		// faint tail shows as edge lines on large text) in geometry and matching UV.
		// The atlas gutter is far wider than the half-range, so no neighbour bleed.
		const float glyphSizeTexels = atlas.metadata.glyphSize > 0 ? static_cast<float>(atlas.metadata.glyphSize) : 32.0F;
		const float halfRangeTexels = atlas.metadata.distanceRange * 0.5F;
		const float padPx = (halfRangeTexels / glyphSizeTexels) * fontSize;
		const float padU = atlas.metadata.atlasWidth > 0 ? halfRangeTexels / static_cast<float>(atlas.metadata.atlasWidth) : 0.0F;
		const float padV = atlas.metadata.atlasHeight > 0 ? halfRangeTexels / static_cast<float>(atlas.metadata.atlasHeight) : 0.0F;

		ForEachGlyph(sdfGlyphs, text, fontSize, letterSpacing, [&](const SDFGlyph& glyph, float penX) {
			// Only generate quad if glyph has geometry (not whitespace)
			if (!glyph.hasGeometry) {
				return;
			}

			// Use atlasBounds (actual glyph content) instead of the full atlas cell,
			// grown by the valid SDF half-range (see note above).
			// Reference: https://github.com/Chlumsky/msdf-atlas-gen/issues/2
			GlyphQuad quad{};
			quad.position = glm::vec2(penX + glyph.planeBoundsMin.x * fontSize - padPx, baselineY - glyph.planeBoundsMax.y * fontSize - padPx);
			quad.size = glm::vec2(
				(glyph.planeBoundsMax.x - glyph.planeBoundsMin.x) * fontSize + 2.0F * padPx,
				(glyph.planeBoundsMax.y - glyph.planeBoundsMin.y) * fontSize + 2.0F * padPx
			);
			quad.uvMin = glm::vec2(glyph.atlasBoundsMin.x - padU, glyph.atlasBoundsMin.y - padV);
			quad.uvMax = glm::vec2(glyph.atlasBoundsMax.x + padU, glyph.atlasBoundsMax.y + padV);
			quad.color = color;

			outQuads.push_back(quad);
		});

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

			CacheKey key{family, text, scale, letterSpacing};
			glyphQuadCache[key] = std::move(entry);

			// Now adjust positions in outQuads for the caller
			for (size_t i = startIdx; i < outQuads.size(); ++i) {
				outQuads[i].position += position;
				outQuads[i].runOrigin = penOrigin;
			}
		} else {
			// No caching - just adjust positions
			for (size_t i = startIdx; i < outQuads.size(); ++i) {
				outQuads[i].position += position;
				outQuads[i].runOrigin = penOrigin;
			}
		}
	}

	const FontRenderer::WrappedTextResult&
	FontRenderer::wrapText(const std::string& text, float scale, float maxWidth, Renderer::FontFamily family) const {
		// Check cache first
		WrapCacheKey key{family, text, scale, maxWidth};
		auto		 cacheIt = wrappedTextCache.find(key);
		if (cacheIt != wrappedTextCache.end()) {
			cacheIt->second.lastAccessFrame = currentFrame;
			return cacheIt->second.result;
		}

		// Cache miss - compute wrapped text
		WrappedTextResult result;

		constexpr float BASE_FONT_SIZE = 16.0F;
		float			fontSize = BASE_FONT_SIZE * scale;
		result.lineHeight = atlasFor(family).metadata.lineHeight * fontSize;

		// Handle empty text
		if (text.empty()) {
			result.lines.push_back("");
			result.lineWidths.push_back(0.0F);
			result.totalWidth = 0.0F;
			result.totalHeight = result.lineHeight;

			// Cache and return
			WrapCacheEntry entry{result, currentFrame};
			wrappedTextCache[key] = std::move(entry);
			return wrappedTextCache[key].result;
		}

		// No wrapping case (maxWidth <= 0)
		if (maxWidth <= 0.0F) {
			result.lines.push_back(text);
			glm::vec2 size = MeasureText(text, scale, family);
			result.lineWidths.push_back(size.x);
			result.totalWidth = size.x;
			result.totalHeight = size.y;

			// Cache and return
			WrapCacheEntry entry{result, currentFrame};
			wrappedTextCache[key] = std::move(entry);
			return wrappedTextCache[key].result;
		}

		// Word-based wrapping algorithm
		std::string currentLine;
		float		currentLineWidth = 0.0F;
		float		spaceWidth = MeasureText(" ", scale, family).x;

		size_t i = 0;
		while (i < text.size()) {
			// Handle explicit newlines
			if (text[i] == '\n') {
				result.lines.push_back(currentLine);
				result.lineWidths.push_back(currentLineWidth);
				currentLine.clear();
				currentLineWidth = 0.0F;
				++i;
				continue;
			}

			// Skip leading whitespace at line start (except first line)
			if (currentLine.empty() && !result.lines.empty() && text[i] == ' ') {
				++i;
				continue;
			}

			// Extract next word (sequence of non-space, non-newline characters)
			size_t wordStart = i;
			while (i < text.size() && text[i] != ' ' && text[i] != '\n') {
				++i;
			}
			std::string word = text.substr(wordStart, i - wordStart);
			float		wordWidth = MeasureText(word, scale, family).x;

			// Check if word fits on current line
			float neededWidth = currentLine.empty() ? wordWidth : (currentLineWidth + spaceWidth + wordWidth);

			if (neededWidth <= maxWidth || currentLine.empty()) {
				// Word fits, or line is empty (must add word even if too long)
				if (!currentLine.empty()) {
					currentLine += ' ';
					currentLineWidth += spaceWidth;
				}
				currentLine += word;
				currentLineWidth += wordWidth;
			} else {
				// Word doesn't fit - start new line
				result.lines.push_back(currentLine);
				result.lineWidths.push_back(currentLineWidth);
				currentLine = word;
				currentLineWidth = wordWidth;
			}

			// Skip single space after word (will be handled by next iteration)
			if (i < text.size() && text[i] == ' ') {
				++i;
			}
		}

		// Add final line if not empty
		if (!currentLine.empty() || result.lines.empty()) {
			result.lines.push_back(currentLine);
			result.lineWidths.push_back(currentLineWidth);
		}

		// Calculate totals
		result.totalWidth = 0.0F;
		for (float lineWidth : result.lineWidths) {
			result.totalWidth = std::max(result.totalWidth, lineWidth);
		}
		result.totalHeight = static_cast<float>(result.lines.size()) * result.lineHeight;

		// Evict oldest entry if cache is full
		if (wrappedTextCache.size() >= FontRendererConfig::kMaxGlyphQuadCacheEntries) {
			auto oldestIt = wrappedTextCache.begin();
			for (auto it = wrappedTextCache.begin(); it != wrappedTextCache.end(); ++it) {
				if (it->second.lastAccessFrame < oldestIt->second.lastAccessFrame) {
					oldestIt = it;
				}
			}
			wrappedTextCache.erase(oldestIt);
		}

		// Cache and return
		WrapCacheEntry entry{result, currentFrame};
		wrappedTextCache[key] = std::move(entry);
		return wrappedTextCache[key].result;
	}

	glm::vec2
	FontRenderer::measureTextWithWrapping(const std::string& text, float scale, float maxWidth, Renderer::FontFamily family) const {
		if (maxWidth <= 0.0F) {
			// No wrapping - use simple measurement
			return MeasureText(text, scale, family);
		}

		const WrappedTextResult& wrapped = wrapText(text, scale, maxWidth, family);
		return glm::vec2(wrapped.totalWidth, wrapped.totalHeight);
	}

	void FontRenderer::generateWrappedGlyphQuads(
		const std::vector<std::string>& lines,
		const glm::vec2&				position,
		float							scale,
		const glm::vec4&				color,
		float							lineHeight,
		Foundation::HorizontalAlign		hAlign,
		float							containerWidth,
		std::vector<GlyphQuad>&			outQuads,
		Renderer::FontFamily			family
	) const {
		float currentY = position.y;

		for (size_t lineIdx = 0; lineIdx < lines.size(); ++lineIdx) {
			const std::string& line = lines[lineIdx];

			// Calculate X offset for alignment
			float lineX = position.x;
			if (containerWidth > 0.0F && !line.empty()) {
				glm::vec2 lineSize = MeasureText(line, scale, family);
				switch (hAlign) {
					case Foundation::HorizontalAlign::Center:
						lineX += (containerWidth - lineSize.x) * 0.5F;
						break;
					case Foundation::HorizontalAlign::Right:
						lineX += containerWidth - lineSize.x;
						break;
					case Foundation::HorizontalAlign::Left:
					default:
						// No offset
						break;
				}
			}

			// Generate quads for this line
			if (!line.empty()) {
				generateGlyphQuads(line, glm::vec2(lineX, currentY), scale, color, outQuads, family);
			}

			currentY += lineHeight;
		}
	}

	GLuint FontRenderer::getAtlasTexture(Renderer::FontFamily family) const {
		return atlasFor(family).texture;
	}

	void FontRenderer::updateFrame() {
		currentFrame++;
	}

	void FontRenderer::clearGlyphQuadCache() {
		glyphQuadCache.clear();
		wrappedTextCache.clear();
		LOG_DEBUG(UI, "Cleared glyph quad cache and wrapped text cache");
	}

	size_t FontRenderer::getGlyphQuadCacheSize() const {
		return glyphQuadCache.size();
	}

} // namespace ui

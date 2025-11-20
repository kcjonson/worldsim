// Font rendering implementation

#include "font/font_renderer.h"
#include "utils/log.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace ui {

	FontRenderer::FontRenderer() = default;

	FontRenderer::~FontRenderer() {
		if (m_vao != 0) {
			glDeleteVertexArrays(1, &m_vao);
		}
		if (m_vbo != 0) {
			glDeleteBuffers(1, &m_vbo);
		}
		// Clean up FreeType textures
		for (auto& [c, character] : m_characters) {
			if (character.textureID) {
				glDeleteTextures(1, &character.textureID);
			}
		}
		// Clean up SDF atlas texture
		if (m_atlasTexture != 0) {
			glDeleteTextures(1, &m_atlasTexture);
		}
		if (m_face != nullptr) {
			FT_Done_Face(m_face);
		}
		if (m_library != nullptr) {
			FT_Done_FreeType(m_library);
		}
	}

	bool FontRenderer::Initialize() {
		LOG_INFO(UI, "Initializing FontRenderer...");

		// Initialize FreeType
		if (FT_Init_FreeType(&m_library) != 0) {
			LOG_ERROR(UI, "FATAL ERROR: Could not init FreeType Library");
			std::exit(1);
		}
		LOG_INFO(UI, "FreeType initialized successfully");

		// Try to load SDF atlas first, fall back to TTF rasterization
		bool fontLoaded = false;
		if (LoadSDFAtlas("fonts/Roboto-SDF.png", "fonts/Roboto-SDF.json")) {
			LOG_INFO(UI, "Using SDF atlas for text rendering");
			fontLoaded = true;
		} else {
			LOG_WARNING(UI, "SDF atlas not found, falling back to TTF rasterization");
			if (!LoadFont("fonts/Roboto-Regular.ttf")) {
				LOG_ERROR(UI, "FATAL ERROR: Failed to load font");
				FT_Done_FreeType(m_library);
				m_library = nullptr;
				std::exit(1);
			}
			LOG_INFO(UI, "Using TTF rasterization for text rendering");
			fontLoaded = true;
		}

		if (!fontLoaded) {
			LOG_ERROR(UI, "FATAL ERROR: Failed to load any font");
			FT_Done_FreeType(m_library);
			m_library = nullptr;
			std::exit(1);
		}

		// Initialize the shader
		if (!m_shader.LoadFromFile("text.vert", "text.frag")) {
			LOG_ERROR(UI, "FATAL ERROR: Failed to load text shaders");
			// Clean up textures created during LoadFont
			for (auto& [c, character] : m_characters) {
				if (character.textureID) {
					glDeleteTextures(1, &character.textureID);
				}
			}
			m_characters.clear();
			FT_Done_FreeType(m_library);
			m_library = nullptr;
			std::exit(1);
		}
		LOG_INFO(UI, "Shaders compiled successfully");

		// Setup buffers
		glGenVertexArrays(1, &m_vao);
		glGenBuffers(1, &m_vbo);
		glBindVertexArray(m_vao);
		glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);

		LOG_INFO(UI, "FontRenderer initialization complete");
		return true;
	}

	bool FontRenderer::LoadFont(const std::string& fontPath) {
		LOG_INFO(UI, "Loading font from: %s", fontPath.c_str());

		if (FT_New_Face(m_library, fontPath.c_str(), 0, &m_face)) {
			LOG_ERROR(UI, "Failed to load font from %s", fontPath.c_str());
			return false;
		}

		// Set base font size to 16px
		FT_Set_Pixel_Sizes(m_face, 0, 16);
		LOG_INFO(UI, "Font face loaded successfully");

		// Store the ascender for the base font size
		// face->size->metrics.ascender is in 26.6 fixed point format
		m_scaledAscender = static_cast<float>(m_face->size->metrics.ascender >> 6);
		// Store the maximum glyph line height for the base font size
		m_maxGlyphHeightUnscaled = static_cast<float>(m_face->size->metrics.height >> 6);

		// Load first 128 characters of ASCII set
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		for (unsigned char c = 0; c < 128; c++) {
			if (FT_Load_Char(m_face, c, FT_LOAD_RENDER) != 0) {
				LOG_WARNING(UI, "Failed to load glyph for character %d", static_cast<int>(c));
				continue;
			}

			GLuint texture = 0;
			glGenTextures(1, &texture);
			glBindTexture(GL_TEXTURE_2D, texture);
			glTexImage2D(
				GL_TEXTURE_2D,
				0,
				GL_RED,
				static_cast<GLsizei>(m_face->glyph->bitmap.width),
				static_cast<GLsizei>(m_face->glyph->bitmap.rows),
				0,
				GL_RED,
				GL_UNSIGNED_BYTE,
				m_face->glyph->bitmap.buffer
			);

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

			// Store first texture for batch rendering key
			if (m_firstGlyphTexture == 0) {
				m_firstGlyphTexture = texture;
			}

			Character character{
				.textureID = texture,
				.size = glm::ivec2(static_cast<GLsizei>(m_face->glyph->bitmap.width), m_face->glyph->bitmap.rows),
				.bearing = glm::ivec2(m_face->glyph->bitmap_left, m_face->glyph->bitmap_top),
				.advance = static_cast<unsigned int>(m_face->glyph->advance.x)
			};
			m_characters[c] = character;
		}
		glBindTexture(GL_TEXTURE_2D, 0);
		FT_Done_Face(m_face); // After this, m_face is no longer valid for metrics
		m_face = nullptr;

		LOG_INFO(UI, "Loaded %zu characters", m_characters.size());
		return true;
	}

	void FontRenderer::RenderText(const std::string& text, const glm::vec2& position, float scale, const glm::vec3& color) {
		// Enable blending for text transparency
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		m_shader.Use();
		glUniform3f(glGetUniformLocation(m_shader.GetProgram(), "textColor"), color.x, color.y, color.z);
		glActiveTexture(GL_TEXTURE0);
		glBindVertexArray(m_vao);

		// Calculate baseline position
		float	  ascenderAtCurrentScale = m_scaledAscender * scale;
		glm::vec2 penPosition = position;
		penPosition.y += ascenderAtCurrentScale;

		// Iterate through each character in the text
		for (char currentChar : text) {
			auto			 charIt = m_characters.find(currentChar);
			const Character* chToRenderPtr = nullptr;

			if (charIt != m_characters.end()) {
				chToRenderPtr = &charIt->second;
			} else {
				// Fallback to '?' if the character is not found
				auto fallbackIt = m_characters.find('?');
				if (fallbackIt != m_characters.end()) {
					chToRenderPtr = &fallbackIt->second;
				}
			}

			if (!chToRenderPtr) {
				continue; // Skip if no valid char or fallback
			}

			const Character& ch = *chToRenderPtr;

			// Calculate position of character quad
			float xpos = penPosition.x + static_cast<float>(ch.bearing.x) * scale;
			float ypos = penPosition.y - static_cast<float>(ch.bearing.y) * scale;

			float w = static_cast<float>(ch.size.x) * scale;
			float h = static_cast<float>(ch.size.y) * scale;

			float vertices[6][4] = {
				{xpos, ypos, 0.0F, 0.0F},
				{xpos, ypos + h, 0.0F, 1.0F},
				{xpos + w, ypos + h, 1.0F, 1.0F},

				{xpos, ypos, 0.0F, 0.0F},
				{xpos + w, ypos + h, 1.0F, 1.0F},
				{xpos + w, ypos, 1.0F, 0.0F}
			};

			glBindTexture(GL_TEXTURE_2D, ch.textureID);
			glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
			glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glDrawArrays(GL_TRIANGLES, 0, 6);

			penPosition.x += (ch.advance >> 6) * scale;
		}

		glBindVertexArray(0);
		glBindTexture(GL_TEXTURE_2D, 0);
		glDisable(GL_BLEND);
	}

	void FontRenderer::SetProjectionMatrix(const glm::mat4& projection) {
		m_shader.Use();
		m_shader.SetUniform("projection", projection);
	}

	glm::vec2
	FontRenderer::MeasureText(const std::string& text, float scale) const { // NOLINT(readability-convert-member-functions-to-static)
		if (text.empty()) {
			return glm::vec2(0.0F, 0.0F);
		}

		float totalWidth = 0.0F;
		float maxGlyphTopFromBaselineUnscaled = 0.0F;
		float minGlyphBottomFromBaselineUnscaled = 0.0F;
		bool  firstChar = true;

		for (char c : text) {
			auto			 it = m_characters.find(c);
			const Character* pCh = nullptr;

			if (it != m_characters.end()) {
				pCh = &it->second;
			} else {
				auto itQ = m_characters.find('?');
				if (itQ != m_characters.end()) {
					pCh = &itQ->second;
				} else {
					continue;
				}
			}

			const Character& ch = *pCh;

			float glyphTopUnscaled = static_cast<float>(ch.bearing.y);
			float glyphBottomUnscaled = static_cast<float>(ch.bearing.y) - static_cast<float>(ch.size.y);

			if (firstChar) {
				maxGlyphTopFromBaselineUnscaled = glyphTopUnscaled;
				minGlyphBottomFromBaselineUnscaled = glyphBottomUnscaled;
				firstChar = false;
			} else {
				maxGlyphTopFromBaselineUnscaled = std::max(maxGlyphTopFromBaselineUnscaled, glyphTopUnscaled);
				minGlyphBottomFromBaselineUnscaled = std::min(minGlyphBottomFromBaselineUnscaled, glyphBottomUnscaled);
			}

			totalWidth += (ch.advance >> 6);
		}

		float scaledTotalWidth = totalWidth * scale;
		float actualHeightScaled = 0.0F;
		if (!firstChar) {
			actualHeightScaled = (maxGlyphTopFromBaselineUnscaled - minGlyphBottomFromBaselineUnscaled) * scale;
		}
		actualHeightScaled = std::max(0.0F, actualHeightScaled);

		return glm::vec2(scaledTotalWidth, actualHeightScaled);
	}

	float FontRenderer::GetMaxGlyphHeight(float scale) const {
		return m_maxGlyphHeightUnscaled * scale;
	}

	float FontRenderer::GetAscent(float scale) const {
		return m_scaledAscender * scale;
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

			m_atlasMetadata.distanceRange = json["atlas"]["distanceRange"].get<float>();
			m_atlasMetadata.glyphSize = json["atlas"]["size"].get<int>();
			m_atlasMetadata.atlasWidth = json["atlas"]["width"].get<int>();
			m_atlasMetadata.atlasHeight = json["atlas"]["height"].get<int>();
			m_atlasMetadata.emSize = json["metrics"]["emSize"].get<float>();
			m_atlasMetadata.ascender = json["metrics"]["ascender"].get<float>();
			m_atlasMetadata.descender = json["metrics"]["descender"].get<float>();
			m_atlasMetadata.lineHeight = json["metrics"]["lineHeight"].get<float>();
		} catch (const std::exception& e) {
			LOG_ERROR(UI, "Failed to parse SDF atlas metadata: %s", e.what());
			return false;
		}

		LOG_INFO(
			UI,
			"Atlas metadata: size=%dx%d, glyphSize=%d, range=%.1f",
			m_atlasMetadata.atlasWidth,
			m_atlasMetadata.atlasHeight,
			m_atlasMetadata.glyphSize,
			m_atlasMetadata.distanceRange
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

				m_sdfGlyphs[c] = glyph;
			}
		} catch (const std::exception& e) {
			LOG_ERROR(UI, "Failed to parse SDF glyphs: %s", e.what());
			return false;
		}

		LOG_INFO(UI, "Loaded %zu SDF glyphs", m_sdfGlyphs.size());

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
		glGenTextures(1, &m_atlasTexture);
		glBindTexture(GL_TEXTURE_2D, m_atlasTexture);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, imageData.get());

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glBindTexture(GL_TEXTURE_2D, 0);

		// imageData automatically freed by unique_ptr

		// Update font metrics for compatibility with existing code
		m_scaledAscender = m_atlasMetadata.ascender * static_cast<float>(m_atlasMetadata.glyphSize);
		m_maxGlyphHeightUnscaled = m_atlasMetadata.lineHeight;
		m_usingSDF = true;

		LOG_INFO(UI, "SDF atlas loaded successfully");
		return true;
	}

	void FontRenderer::GenerateGlyphQuads(
		const std::string&		text,
		const glm::vec2&		position,
		float					scale,
		const glm::vec4&		color,
		std::vector<GlyphQuad>& outQuads
	) const {
		if (!m_usingSDF) {
			LOG_WARNING(UI, "GenerateGlyphQuads called but SDF atlas not loaded");
			return;
		}

		// Calculate baseline position
		// Font size in pixels = glyphSize * scale
		float fontSize = static_cast<float>(m_atlasMetadata.glyphSize) * scale;
		float ascenderAtCurrentScale = m_atlasMetadata.ascender * fontSize;

		glm::vec2 penPosition = position;
		penPosition.y += ascenderAtCurrentScale; // Move to baseline

		for (char currentChar : text) {
			auto			it = m_sdfGlyphs.find(currentChar);
			const SDFGlyph* glyphPtr = nullptr;

			if (it != m_sdfGlyphs.end()) {
				glyphPtr = &it->second;
			} else {
				// Fallback to '?' if character not found
				auto fallbackIt = m_sdfGlyphs.find('?');
				if (fallbackIt != m_sdfGlyphs.end()) {
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

				// Log first glyph for debugging
				if (outQuads.empty()) {
					LOG_INFO(
						UI,
						"    First glyph: char='%c', pos=(%.1f,%.1f), size=(%.1f,%.1f), uv=(%.3f,%.3f)-(%.3f,%.3f)",
						currentChar,
						xpos,
						ypos,
						w,
						h,
						glyph.atlasBoundsMin.x,
						glyph.atlasBoundsMin.y,
						glyph.atlasBoundsMax.x,
						glyph.atlasBoundsMax.y
					);
				}

				outQuads.push_back(quad);
			}

			// Advance pen position
			penPosition.x += glyph.advance * fontSize;
		}
	}

	GLuint FontRenderer::GetAtlasTexture() const {
		if (m_usingSDF) {
			return m_atlasTexture;
		} else {
			return m_firstGlyphTexture;
		}
	}

} // namespace ui

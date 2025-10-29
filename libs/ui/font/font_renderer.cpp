// Font rendering implementation

#include "font/font_renderer.h"
#include <iostream>
#include <algorithm>
#include <cstdlib>

namespace UI {

FontRenderer::FontRenderer() = default;

FontRenderer::~FontRenderer() {
	if (m_vao) {
		glDeleteVertexArrays(1, &m_vao);
	}
	if (m_vbo) {
		glDeleteBuffers(1, &m_vbo);
	}
	// Clean up FreeType textures
	for (auto& [c, character] : m_characters) {
		if (character.textureID) {
			glDeleteTextures(1, &character.textureID);
		}
	}
	if (m_face) {
		FT_Done_Face(m_face);
	}
	if (m_library) {
		FT_Done_FreeType(m_library);
	}
}

bool FontRenderer::Initialize() {
	std::cout << "Initializing FontRenderer..." << std::endl;

	// Initialize FreeType
	if (FT_Init_FreeType(&m_library)) {
		std::cerr << "FATAL ERROR: Could not init FreeType Library" << std::endl;
		std::exit(1);
	}
	std::cout << "FreeType initialized successfully" << std::endl;

	// Load font (hardcoded for now - can be made configurable later)
	if (!LoadFont("fonts/Roboto-Regular.ttf")) {
		std::cerr << "FATAL ERROR: Failed to load font" << std::endl;
		FT_Done_FreeType(m_library);
		m_library = nullptr;
		std::exit(1);
	}
	std::cout << "Font loaded successfully" << std::endl;

	// Initialize the shader
	if (!m_shader.LoadFromFile("text.vert", "text.frag")) {
		std::cerr << "FATAL ERROR: Failed to load text shaders" << std::endl;
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
	std::cout << "Shaders compiled successfully" << std::endl;

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

	std::cout << "FontRenderer initialization complete" << std::endl;
	return true;
}

bool FontRenderer::LoadFont(const std::string& fontPath) {
	std::cout << "Loading font from: " << fontPath << std::endl;

	if (FT_New_Face(m_library, fontPath.c_str(), 0, &m_face)) {
		std::cerr << "ERROR::FREETYPE: Failed to load font" << std::endl;
		return false;
	}

	// Set base font size to 16px
	FT_Set_Pixel_Sizes(m_face, 0, 16);
	std::cout << "Font face loaded successfully" << std::endl;

	// Store the ascender for the base font size
	// face->size->metrics.ascender is in 26.6 fixed point format
	m_scaledAscender = static_cast<float>(m_face->size->metrics.ascender >> 6);
	// Store the maximum glyph line height for the base font size
	m_maxGlyphHeightUnscaled = static_cast<float>(m_face->size->metrics.height >> 6);

	// Load first 128 characters of ASCII set
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	for (unsigned char c = 0; c < 128; c++) {
		if (FT_Load_Char(m_face, c, FT_LOAD_RENDER)) {
			std::cerr << "ERROR::FREETYPE: Failed to load Glyph " << static_cast<int>(c) << std::endl;
			continue;
		}

		GLuint texture;
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexImage2D(
			GL_TEXTURE_2D,
			0,
			GL_RED,
			m_face->glyph->bitmap.width,
			m_face->glyph->bitmap.rows,
			0,
			GL_RED,
			GL_UNSIGNED_BYTE,
			m_face->glyph->bitmap.buffer
		);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		Character character{
			.textureID = texture,
			.size = glm::ivec2(m_face->glyph->bitmap.width, m_face->glyph->bitmap.rows),
			.bearing = glm::ivec2(m_face->glyph->bitmap_left, m_face->glyph->bitmap_top),
			.advance = static_cast<unsigned int>(m_face->glyph->advance.x)
		};
		m_characters[c] = character;
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	FT_Done_Face(m_face); // After this, m_face is no longer valid for metrics
	m_face = nullptr;

	std::cout << "Loaded " << m_characters.size() << " characters" << std::endl;
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
	float ascenderAtCurrentScale = m_scaledAscender * scale;
	glm::vec2 penPosition = position;
	penPosition.y += ascenderAtCurrentScale;

	// Iterate through each character in the text
	for (char currentChar : text) {
		auto charIt = m_characters.find(currentChar);
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
			{ xpos,     ypos,       0.0f, 0.0f },
			{ xpos,     ypos + h,   0.0f, 1.0f },
			{ xpos + w, ypos + h,   1.0f, 1.0f },

			{ xpos,     ypos,       0.0f, 0.0f },
			{ xpos + w, ypos + h,   1.0f, 1.0f },
			{ xpos + w, ypos,       1.0f, 0.0f }
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

glm::vec2 FontRenderer::MeasureText(const std::string& text, float scale) const {
	if (text.empty()) {
		return glm::vec2(0.0f, 0.0f);
	}

	float totalWidth = 0.0f;
	float maxGlyphTopFromBaselineUnscaled = 0.0f;
	float minGlyphBottomFromBaselineUnscaled = 0.0f;
	bool firstChar = true;

	for (char c : text) {
		auto it = m_characters.find(c);
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
	float actualHeightScaled = 0.0f;
	if (!firstChar) {
		actualHeightScaled = (maxGlyphTopFromBaselineUnscaled - minGlyphBottomFromBaselineUnscaled) * scale;
	}
	actualHeightScaled = std::max(0.0f, actualHeightScaled);

	return glm::vec2(scaledTotalWidth, actualHeightScaled);
}

float FontRenderer::GetMaxGlyphHeight(float scale) const {
	return m_maxGlyphHeightUnscaled * scale;
}

} // namespace UI

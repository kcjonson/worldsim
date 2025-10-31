// Font rendering system using FreeType
// Renders text with TrueType fonts using texture atlas approach

#pragma once

#include <ft2build.h>
#include <glm/glm.hpp>
#include <map>
#include <string>
#include FT_FREETYPE_H
#include "shader/shader.h"
#include <GL/glew.h>

namespace UI {

	class FontRenderer {
	  public:
		FontRenderer();
		~FontRenderer();

		// Delete copy constructor and assignment operator
		FontRenderer(const FontRenderer&) = delete;
		FontRenderer& operator=(const FontRenderer&) = delete;

		/**
		 * Initialize the font renderer
		 * @return true if initialization was successful
		 */
		bool Initialize();

		/**
		 * Render text at the specified position
		 * @param text The string to render
		 * @param position Top-left position of the text in screen space
		 * @param scale Scaling factor for the text size (1.0F = 16px base size)
		 * @param color RGB color of the text (0-1 range)
		 */
		void RenderText(const std::string& text, const glm::vec2& position, float scale, const glm::vec3& color);

		/**
		 * Set the projection matrix for the text shader
		 * @param projection The projection matrix to use
		 */
		void SetProjectionMatrix(const glm::mat4& projection);

		/**
		 * Calculate the dimensions of a text string with the given scale
		 * @param text The text to measure
		 * @param scale Scaling factor (default 1.0F)
		 * @return Width and height of the text in pixels
		 */
		glm::vec2 MeasureText(const std::string& text, float scale = 1.0F) const;

		/**
		 * Get the maximum glyph height scaled by the given factor
		 * @param scale Scaling factor for the font size (1.0F = original size)
		 * @return Height of the tallest glyph in the font at the given scale
		 */
		float GetMaxGlyphHeight(float scale = 1.0F) const;

	  private:
		/**
		 * Character information for font rendering
		 */
		struct Character {
			GLuint		 textureID; // OpenGL texture ID for the character
			glm::ivec2	 size;		// Size of the character glyph
			glm::ivec2	 bearing;	// Offset from baseline to top-left of glyph
			unsigned int advance;	// Horizontal advance to next character
		};

		/**
		 * Load a font file
		 * @param fontPath Path to the font file
		 * @return true if font was loaded successfully
		 */
		bool LoadFont(const std::string& fontPath);

		std::map<char, Character> m_characters;					   // Map of loaded characters
		Renderer::Shader		  m_shader;						   // Shader for text rendering
		GLuint					  m_vao = 0;					   // Vertex Array Object
		GLuint					  m_vbo = 0;					   // Vertex Buffer Object
		FT_Library				  m_library = nullptr;			   // FreeType library instance
		FT_Face					  m_face = nullptr;				   // FreeType font face
		float					  m_scaledAscender = 0.0F;		   // Stores the ascender for the base font size
		float					  m_maxGlyphHeightUnscaled = 0.0F; // Unscaled maximum glyph height
	};

} // namespace UI

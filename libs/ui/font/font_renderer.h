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

namespace ui {

	class FontRenderer {
	  public:
		FontRenderer();
		~FontRenderer();

		// Delete copy constructor and assignment operator
		FontRenderer(const FontRenderer&) = delete;
		FontRenderer& operator=(const FontRenderer&) = delete;

		// Default move constructor and assignment operator
		FontRenderer(FontRenderer&&) = default;
		FontRenderer& operator=(FontRenderer&&) = default;

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

		/**
		 * Get the font's ascent (distance from baseline to top) scaled by the given factor
		 * @param scale Scaling factor for the font size (1.0F = original size)
		 * @return Font ascent at the given scale
		 */
		float GetAscent(float scale = 1.0F) const;

		/**
		 * Glyph quad data for batched text rendering
		 */
		struct GlyphQuad {
			glm::vec2 position; // Top-left position of the quad
			glm::vec2 size;		// Width and height of the quad
			glm::vec2 uvMin;	// Texture coordinate bottom-left
			glm::vec2 uvMax;	// Texture coordinate top-right
			glm::vec4 color;	// RGBA color
		};

		/**
		 * Generate glyph quads for batched rendering (does not render immediately)
		 * @param text The string to generate quads for
		 * @param position Top-left position of the text in screen space
		 * @param scale Scaling factor for the text size (1.0F = 16px base size)
		 * @param color RGBA color of the text (0-1 range)
		 * @param outQuads Output vector to append generated quads to
		 */
		void GenerateGlyphQuads(
			const std::string& text,
			const glm::vec2&   position,
			float			   scale,
			const glm::vec4&   color,
			std::vector<GlyphQuad>& outQuads
		) const;

		/**
		 * Get the texture ID of the font atlas (for batching)
		 * @return OpenGL texture ID
		 */
		GLuint GetAtlasTexture() const;

	  private:
		/**
		 * Character information for font rendering
		 */
		struct Character {
			GLuint		 textureID{}; // OpenGL texture ID for the character
			glm::ivec2	 size;		  // Size of the character glyph
			glm::ivec2	 bearing;	  // Offset from baseline to top-left of glyph
			unsigned int advance{};	  // Horizontal advance to next character
		};

		/**
		 * SDF atlas-based glyph information
		 */
		struct SDFGlyph {
			glm::vec2 atlasUVMin;	// Bottom-left UV in atlas texture
			glm::vec2 atlasUVMax;	// Top-right UV in atlas texture
			glm::vec2 atlasBoundsMin; // Bottom-left UV of actual glyph content
			glm::vec2 atlasBoundsMax; // Top-right UV of actual glyph content
			glm::vec2 planeBoundsMin; // Glyph bounds min (in em units)
			glm::vec2 planeBoundsMax; // Glyph bounds max (in em units)
			float advance;			  // Horizontal advance (in em units)
			bool hasGeometry;		  // False for whitespace characters
		};

		/**
		 * SDF atlas metadata
		 */
		struct SDFAtlasMetadata {
			float distanceRange;	// Distance field range in pixels
			int	  glyphSize;		// Size of each glyph in atlas
			int	  atlasWidth;		// Atlas texture width
			int	  atlasHeight;		// Atlas texture height
			float emSize;			// Font em size
			float ascender;			// Font ascender (in em units)
			float descender;		// Font descender (in em units)
			float lineHeight;		// Line height (in em units)
		};

		/**
		 * Load a font file using FreeType (legacy)
		 * @param fontPath Path to the font file
		 * @return true if font was loaded successfully
		 */
		bool LoadFont(const std::string& fontPath);

		/**
		 * Load SDF atlas from PNG and JSON files
		 * @param pngPath Path to the PNG atlas texture
		 * @param jsonPath Path to the JSON metadata file
		 * @return true if atlas was loaded successfully
		 */
		bool LoadSDFAtlas(const std::string& pngPath, const std::string& jsonPath);

		std::map<char, Character> m_characters;					   // Map of loaded characters (FreeType mode)
		std::map<char, SDFGlyph> m_sdfGlyphs;					   // Map of SDF glyphs (Atlas mode)
		SDFAtlasMetadata		 m_atlasMetadata{};				   // SDF atlas metadata
		Renderer::Shader		  m_shader;						   // Shader for text rendering
		GLuint					  m_vao = 0;					   // Vertex Array Object
		GLuint					  m_vbo = 0;					   // Vertex Buffer Object
		GLuint					  m_atlasTexture = 0;			   // SDF atlas texture
		bool					  m_usingSDF = false;			   // True if using SDF atlas mode
		FT_Library				  m_library = nullptr;			   // FreeType library instance
		FT_Face					  m_face = nullptr;				   // FreeType font face
		float					  m_scaledAscender = 0.0F;		   // Stores the ascender for the base font size
		float					  m_maxGlyphHeightUnscaled = 0.0F; // Unscaled maximum glyph height
		GLuint					  m_firstGlyphTexture = 0;		   // First glyph texture (for batch key)
	};

} // namespace ui

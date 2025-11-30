// Font rendering system using FreeType
// Renders text with TrueType fonts using texture atlas approach

#pragma once

#include <ft2build.h>
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include FT_FREETYPE_H
#include "shader/shader.h"
#include <GL/glew.h>

namespace ui {

	/**
	 * Configuration constants for FontRenderer performance tuning
	 */
	namespace FontRendererConfig {
		// Glyph quad cache settings
		// Size to handle complex UIs: ~1000 unique strings × ~4 scales = 4000 entries
		// Memory cost: ~6-8MB max (1.5KB average per entry)
		constexpr size_t kMaxGlyphQuadCacheEntries = 4096;

		// Runtime toggle for cache (disable for testing/comparison)
		constexpr bool kEnableGlyphQuadCache = true;
	} // namespace FontRendererConfig

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
			const std::string&		text,
			const glm::vec2&		position,
			float					scale,
			const glm::vec4&		color,
			std::vector<GlyphQuad>& outQuads
		) const;

		/**
		 * Get the texture ID of the font atlas (for batching)
		 * @return OpenGL texture ID
		 */
		GLuint GetAtlasTexture() const;

		/**
		 * Update the internal frame counter for cache LRU tracking
		 * Should be called once per frame from the main application loop
		 */
		void UpdateFrame();

		/**
		 * Clear the glyph quad cache (e.g., on scene transitions)
		 */
		void ClearGlyphQuadCache();

		/**
		 * Get the current size of the glyph quad cache (for debugging/profiling)
		 * @return Number of entries currently cached
		 */
		size_t GetGlyphQuadCacheSize() const;

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
			glm::vec2 atlasUVMin;	  // Bottom-left UV in atlas texture
			glm::vec2 atlasUVMax;	  // Top-right UV in atlas texture
			glm::vec2 atlasBoundsMin; // Bottom-left UV of actual glyph content
			glm::vec2 atlasBoundsMax; // Top-right UV of actual glyph content
			glm::vec2 planeBoundsMin; // Glyph bounds min (in em units)
			glm::vec2 planeBoundsMax; // Glyph bounds max (in em units)
			float	  advance;		  // Horizontal advance (in em units)
			bool	  hasGeometry;	  // False for whitespace characters
		};

		/**
		 * SDF atlas metadata
		 */
		struct SDFAtlasMetadata {
			float distanceRange; // Distance field range in pixels
			int	  glyphSize;	 // Size of each glyph in atlas
			int	  atlasWidth;	 // Atlas texture width
			int	  atlasHeight;	 // Atlas texture height
			float emSize;		 // Font em size
			float ascender;		 // Font ascender (in em units)
			float descender;	 // Font descender (in em units)
			float lineHeight;	 // Line height (in em units)
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

		/**
		 * Cache key for glyph quad caching (text + scale)
		 */
		struct CacheKey {
			std::string text;
			float		scale;

			bool operator==(const CacheKey& other) const {
				return text == other.text && std::abs(scale - other.scale) < 0.001F;
			}
		};

		/**
		 * Hash function for CacheKey
		 */
		struct CacheKeyHash {
			size_t operator()(const CacheKey& key) const {
				size_t h1 = std::hash<std::string>{}(key.text);
				size_t h2 = std::hash<float>{}(key.scale);
				return h1 ^ (h2 << 1);
			}
		};

		/**
		 * Cache entry storing generated quads + LRU tracking
		 */
		struct CacheEntry {
			std::vector<GlyphQuad> quads;
			uint64_t			   lastAccessFrame;
		};

		std::map<char, Character> characters;					   // Map of loaded characters (FreeType mode)
		std::map<char, SDFGlyph>  sdfGlyphs;					   // Map of SDF glyphs (Atlas mode)
		SDFAtlasMetadata		  atlasMetadata{};			   // SDF atlas metadata
		Renderer::Shader		  shader;						   // Shader for text rendering
		GLuint					  vao = 0;					   // Vertex Array Object
		GLuint					  vbo = 0;					   // Vertex Buffer Object
		GLuint					  atlasTexture = 0;			   // SDF atlas texture
		bool					  usingSDF = false;			   // True if using SDF atlas mode
		FT_Library				  library = nullptr;			   // FreeType library instance
		FT_Face					  face = nullptr;				   // FreeType font face
		float					  scaledAscender = 0.0F;		   // Stores the ascender for the base font size
		float					  maxGlyphHeightUnscaled = 0.0F; // Unscaled maximum glyph height
		GLuint					  firstGlyphTexture = 0;		   // First glyph texture (for batch key)

		// Glyph quad cache (mutable for const GenerateGlyphQuads)
		mutable std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> glyphQuadCache;
		mutable uint64_t												currentFrame = 0;
	};

} // namespace ui

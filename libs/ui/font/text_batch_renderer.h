// Text batch renderer for efficient, z-ordered text rendering
// Uses MSDF atlas and batches all text draw calls for proper layering

#pragma once

#include "font/font_renderer.h"
#include "shader/shader.h"
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>

namespace ui {

	/**
	 * Batches text rendering with z-ordering support
	 *
	 * Collects all text draw calls during the frame, sorts by z-index,
	 * then renders in a single batched draw call per z-layer.
	 */
	class TextBatchRenderer {
	  public:
		TextBatchRenderer();
		~TextBatchRenderer();

		// Delete copy constructor and assignment operator
		TextBatchRenderer(const TextBatchRenderer&) = delete;
		TextBatchRenderer& operator=(const TextBatchRenderer&) = delete;

		// Default move constructor and assignment operator
		TextBatchRenderer(TextBatchRenderer&&) = default;
		TextBatchRenderer& operator=(TextBatchRenderer&&) = default;

		/**
		 * Initialize the batch renderer
		 * @param fontRenderer FontRenderer with loaded SDF atlas
		 * @return true if initialization was successful
		 */
		bool Initialize(FontRenderer* fontRenderer);

		/**
		 * Set the projection matrix for text rendering
		 * @param projection The projection matrix to use
		 */
		void SetProjectionMatrix(const glm::mat4& projection);

		/**
		 * Add text to the batch for rendering
		 * @param text The string to render
		 * @param position Top-left position of the text in screen space
		 * @param scale Scaling factor for text size
		 * @param color RGBA color of the text (0-1 range)
		 * @param zIndex Z-index for sorting (higher = front)
		 */
		void AddText(
			const std::string& text,
			const glm::vec2&   position,
			float			   scale,
			const glm::vec4&   color,
			float			   zIndex
		);

		/**
		 * Render all batched text, then clear the batch
		 * Text is rendered in z-index order (back to front)
		 */
		void Flush();

		/**
		 * Clear all batched text without rendering
		 */
		void Clear();

	  private:
		/**
		 * A single text draw command with z-index
		 */
		struct TextCommand {
			std::vector<FontRenderer::GlyphQuad> glyphs; // Pre-generated glyph quads
			float								  zIndex; // Z-index for sorting
		};

		/**
		 * Vertex data for batched rendering
		 * Layout: position (vec2), texcoord (vec2), color (vec4)
		 */
		struct Vertex {
			glm::vec2 position;
			glm::vec2 texCoord;
			glm::vec4 color;
		};

		FontRenderer*			   m_fontRenderer = nullptr; // Reference to font renderer
		Renderer::Shader		   m_shader;				 // MSDF text shader
		std::vector<TextCommand>   m_commands;				 // Pending text commands
		GLuint					   m_vao = 0;				 // Vertex Array Object
		GLuint					   m_vbo = 0;				 // Vertex Buffer Object
		GLuint					   m_ebo = 0;				 // Element Buffer Object
		std::vector<Vertex>		   m_vertices;				 // Vertex buffer data
		std::vector<unsigned int>  m_indices;				 // Index buffer data
		static constexpr size_t	   kMaxVertices = 65536;	 // Max vertices per batch (16k quads)
		static constexpr size_t	   kMaxIndices = 98304;		 // Max indices per batch (16k quads * 6)
	};

} // namespace ui

// Text batch renderer implementation

#include "font/text_batch_renderer.h"
#include "utils/log.h"
#include <algorithm>
#include <cstdlib>

namespace ui {

	TextBatchRenderer::TextBatchRenderer() = default;

	TextBatchRenderer::~TextBatchRenderer() {
		if (m_vao != 0) {
			glDeleteVertexArrays(1, &m_vao);
		}
		if (m_vbo != 0) {
			glDeleteBuffers(1, &m_vbo);
		}
		if (m_ebo != 0) {
			glDeleteBuffers(1, &m_ebo);
		}
	}

	bool TextBatchRenderer::Initialize(FontRenderer* fontRenderer) {
		LOG_INFO(UI, "Initializing TextBatchRenderer...");

		if (!fontRenderer) {
			LOG_ERROR(UI, "FontRenderer is null");
			return false;
		}

		m_fontRenderer = fontRenderer;

		// Load MSDF shader
		if (!m_shader.LoadFromFile("msdf_text.vert", "msdf_text.frag")) {
			LOG_ERROR(UI, "Failed to load MSDF text shaders");
			return false;
		}

		LOG_INFO(UI, "MSDF shaders loaded successfully");

		// Create VAO and buffers
		glGenVertexArrays(1, &m_vao);
		glGenBuffers(1, &m_vbo);
		glGenBuffers(1, &m_ebo);

		glBindVertexArray(m_vao);

		// Setup VBO
		glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
		glBufferData(GL_ARRAY_BUFFER, kMaxVertices * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW);

		// Setup vertex attributes
		// Layout: position (vec2), texcoord (vec2), color (vec4)
		// Position attribute (location = 0)
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));

		// TexCoord attribute (location = 1)
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoord));

		// Color attribute (location = 2)
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));

		// Setup EBO
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, kMaxIndices * sizeof(unsigned int), nullptr, GL_DYNAMIC_DRAW);

		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

		// Pre-allocate command and vertex buffers
		m_commands.reserve(256);
		m_vertices.reserve(kMaxVertices);
		m_indices.reserve(kMaxIndices);

		LOG_INFO(UI, "TextBatchRenderer initialized successfully");
		return true;
	}

	void TextBatchRenderer::SetProjectionMatrix(const glm::mat4& projection) {
		m_shader.Use();
		m_shader.SetUniform("projection", projection);
	}

	void TextBatchRenderer::AddText(
		const std::string& text,
		const glm::vec2&   position,
		float			   scale,
		const glm::vec4&   color,
		float			   zIndex
	) {
		if (!m_fontRenderer) {
			LOG_WARNING(UI, "AddText called but FontRenderer not set");
			return;
		}

		// Create command with generated glyphs
		TextCommand cmd;
		cmd.zIndex = zIndex;

		// Generate glyph quads using FontRenderer
		m_fontRenderer->GenerateGlyphQuads(text, position, scale, color, cmd.glyphs);

		// Only add if we generated glyphs
		if (!cmd.glyphs.empty()) {
			m_commands.push_back(std::move(cmd));
		}
	}

	void TextBatchRenderer::Flush() {
		if (m_commands.empty()) {
			return; // Nothing to render
		}

		// Sort commands by z-index (back to front)
		std::stable_sort(m_commands.begin(), m_commands.end(), [](const TextCommand& a, const TextCommand& b) {
			return a.zIndex < b.zIndex;
		});

		// Build vertex and index buffers from sorted commands
		m_vertices.clear();
		m_indices.clear();

		unsigned int vertexOffset = 0;

		for (const auto& cmd : m_commands) {
			for (const auto& glyph : cmd.glyphs) {
				// Check if we have room for this quad (4 vertices, 6 indices)
				if (m_vertices.size() + 4 > kMaxVertices || m_indices.size() + 6 > kMaxIndices) {
					LOG_WARNING(UI, "TextBatchRenderer vertex/index buffer full, flushing partial batch");
					break;
				}

				// Create 4 vertices for this glyph quad
				// Top-left
				m_vertices.push_back(
					Vertex{
						.position = glyph.position,
						.texCoord = glm::vec2(glyph.uvMin.x, glyph.uvMin.y),
						.color = glyph.color
					}
				);

				// Top-right
				m_vertices.push_back(
					Vertex{
						.position = glm::vec2(glyph.position.x + glyph.size.x, glyph.position.y),
						.texCoord = glm::vec2(glyph.uvMax.x, glyph.uvMin.y),
						.color = glyph.color
					}
				);

				// Bottom-right
				m_vertices.push_back(
					Vertex{
						.position = glm::vec2(glyph.position.x + glyph.size.x, glyph.position.y + glyph.size.y),
						.texCoord = glm::vec2(glyph.uvMax.x, glyph.uvMax.y),
						.color = glyph.color
					}
				);

				// Bottom-left
				m_vertices.push_back(
					Vertex{
						.position = glm::vec2(glyph.position.x, glyph.position.y + glyph.size.y),
						.texCoord = glm::vec2(glyph.uvMin.x, glyph.uvMax.y),
						.color = glyph.color
					}
				);

				// Create indices for two triangles (0,1,2) and (0,2,3)
				m_indices.push_back(vertexOffset + 0);
				m_indices.push_back(vertexOffset + 1);
				m_indices.push_back(vertexOffset + 2);

				m_indices.push_back(vertexOffset + 0);
				m_indices.push_back(vertexOffset + 2);
				m_indices.push_back(vertexOffset + 3);

				vertexOffset += 4;
			}
		}

		// Upload and render
		if (!m_vertices.empty() && !m_indices.empty()) {
			// Enable blending for text transparency
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			m_shader.Use();

			// Bind atlas texture
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, m_fontRenderer->GetAtlasTexture());
			m_shader.SetUniform("msdfAtlas", 0);

			// Set pixel range uniform (hardcoded for now, could be from atlas metadata)
			m_shader.SetUniform("pixelRange", 4.0f);

			// Upload vertex data
			glBindVertexArray(m_vao);
			glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
			glBufferSubData(GL_ARRAY_BUFFER, 0, m_vertices.size() * sizeof(Vertex), m_vertices.data());

			// Upload index data
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
			glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, m_indices.size() * sizeof(unsigned int), m_indices.data());

			// Draw
			glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indices.size()), GL_UNSIGNED_INT, 0);

			// Cleanup
			glBindVertexArray(0);
			glBindTexture(GL_TEXTURE_2D, 0);
			glDisable(GL_BLEND);
		}

		// Clear for next frame
		Clear();
	}

	void TextBatchRenderer::Clear() {
		m_commands.clear();
		m_vertices.clear();
		m_indices.clear();
	}

} // namespace ui

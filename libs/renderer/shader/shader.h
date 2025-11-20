// Shader utility for loading and managing OpenGL shader programs
// Loads vertex and fragment shaders from files in the shaders/ directory

#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>

namespace Renderer { // NOLINT(readability-identifier-naming)

	class Shader {
	  public:
		Shader() = default;
		~Shader();

		// Delete copy constructor and assignment operator
		Shader(const Shader&) = delete;
		Shader& operator=(const Shader&) = delete;

		// Move constructor and assignment operator
		Shader(Shader&& other) noexcept;
		Shader& operator=(Shader&& other) noexcept;

		/**
		 * Load and compile shaders from files
		 * Files are loaded from the shaders/ directory relative to the executable
		 * @param vertexPath Vertex shader filename (e.g., "text.vert")
		 * @param fragmentPath Fragment shader filename (e.g., "text.frag")
		 * @return true if shaders were loaded and linked successfully
		 */
		bool LoadFromFile(const char* vertexPath, const char* fragmentPath);

		/**
		 * Activate this shader program for use
		 */
		void Use() const;

		/**
		 * Deactivate shader program
		 */
		void Unbind() const;

		/**
		 * Set a mat4 uniform
		 */
		void SetUniform(const char* name, const glm::mat4& value) const;

		/**
		 * Set an int uniform (used for texture samplers)
		 */
		void SetUniform(const char* name, int value) const;

		/**
		 * Set a float uniform
		 */
		void SetUniform(const char* name, float value) const;

		/**
		 * Get the OpenGL program ID
		 */
		GLuint GetProgram() const { return m_program; }

		/**
		 * Check if shader program is valid
		 */
		bool IsValid() const { return m_program != 0; }

	  private:
		GLuint m_program = 0;
	};

} // namespace Renderer

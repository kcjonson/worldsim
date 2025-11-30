#pragma once

// Shader Loader - Loads shader source from disk
//
// Utility for loading GLSL shader files (.vert, .frag, .geom, etc.)
// from the filesystem. Provides error handling and diagnostics.

#include <GL/glew.h>
#include <string>

namespace Renderer {

	// Shader loading and compilation utilities
	class ShaderLoader {
	  public:
		// Load shader source code from a file
		// Returns empty string on error (check stderr for error message)
		static std::string LoadShaderSource(const char* filepath);

		// Compile and link a shader program from file paths
		// Returns 0 on error (check stderr for error messages)
		static GLuint loadShaderProgram(const char* vertexPath, const char* fragmentPath);

	  private:
		// Compile a single shader from source code
		static GLuint compileShader(GLenum shaderType, const char* source, const char* filepath);

		// Link shader program from compiled shaders
		static GLuint linkProgram(GLuint vertexShader, GLuint fragmentShader);
	};

} // namespace Renderer

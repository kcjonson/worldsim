// Shader Loader implementation
// Loads shader source from disk and compiles into OpenGL programs

#include "shader/ShaderLoader.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace Renderer {

	std::string ShaderLoader::LoadShaderSource(const char* filepath) {
		std::ifstream file(filepath);

		if (!file.is_open()) {
			std::cerr << "Failed to open shader file: " << filepath << std::endl;
			return "";
		}

		std::stringstream buffer;
		buffer << file.rdbuf();
		return buffer.str();
	}

	GLuint ShaderLoader::compileShader(GLenum shaderType, const char* source, const char* filepath) {
		GLuint shader = glCreateShader(shaderType);
		glShaderSource(shader, 1, &source, nullptr);
		glCompileShader(shader);

		// Check for compilation errors
		GLint success = 0;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

		if (success == 0) {
			GLint logLength = 0;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);

			std::vector<char> infoLog(logLength > 0 ? logLength : 1);
			glGetShaderInfoLog(shader, logLength, nullptr, infoLog.data());

			const char* shaderTypeStr = (shaderType == GL_VERTEX_SHADER) ? "Vertex" : "Fragment";
			std::cerr << shaderTypeStr << " shader compilation failed (" << filepath << "):" << std::endl;
			std::cerr << infoLog.data() << std::endl;

			glDeleteShader(shader);
			return 0;
		}

		return shader;
	}

	GLuint ShaderLoader::linkProgram(GLuint vertexShader, GLuint fragmentShader) {
		GLuint program = glCreateProgram();
		glAttachShader(program, vertexShader);
		glAttachShader(program, fragmentShader);
		glLinkProgram(program);

		// Check for linking errors
		GLint success = 0;
		glGetProgramiv(program, GL_LINK_STATUS, &success);

		if (success == 0) {
			GLint logLength = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);

			std::vector<char> infoLog(logLength > 0 ? logLength : 1);
			glGetProgramInfoLog(program, logLength, nullptr, infoLog.data());
			std::cerr << "Shader program linking failed:" << std::endl;
			std::cerr << infoLog.data() << std::endl;

			glDeleteProgram(program);
			return 0;
		}

		return program;
	}

	GLuint ShaderLoader::loadShaderProgram(const char* vertexPath, const char* fragmentPath) {
		// Load shader source files
		std::string vertexSource = LoadShaderSource(vertexPath);
		std::string fragmentSource = LoadShaderSource(fragmentPath);

		if (vertexSource.empty() || fragmentSource.empty()) {
			std::cerr << "Failed to load shader sources" << std::endl;
			return 0;
		}

		// Compile shaders
		GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource.c_str(), vertexPath);
		if (vertexShader == 0) {
			return 0;
		}

		GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource.c_str(), fragmentPath);
		if (fragmentShader == 0) {
			glDeleteShader(vertexShader);
			return 0;
		}

		// Link program
		GLuint program = linkProgram(vertexShader, fragmentShader);

		// Clean up shaders (no longer needed after linking)
		glDeleteShader(vertexShader);
		glDeleteShader(fragmentShader);

		return program;
	}

} // namespace Renderer

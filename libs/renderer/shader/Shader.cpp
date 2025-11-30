// Shader utility implementation

#include "shader/Shader.h"
#include "utils/ResourcePath.h"
#include <filesystem>
#include <fstream>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <sstream>
#include <vector>

namespace Renderer {

	Shader::~Shader() {
		if (program != 0) {
			glDeleteProgram(program);
			program = 0;
		}
	}

	Shader::Shader(Shader&& other) noexcept
		: program(other.program) {
		other.program = 0;
	}

	Shader& Shader::operator=(Shader&& other) noexcept {
		if (this != &other) {
			if (program != 0) {
				glDeleteProgram(program);
			}
			program = other.program;
			other.program = 0;
		}
		return *this;
	}

	bool Shader::LoadFromFile(const char* vertexPath, const char* fragmentPath) {
		// Find shaders using the resource path utility (handles invalid cwd from IDEs)
		std::filesystem::path shadersDir("shaders");
		auto				  fullVertexPath = Foundation::findResource(shadersDir / vertexPath);
		auto				  fullFragmentPath = Foundation::findResource(shadersDir / fragmentPath);

		if (!fullVertexPath || !fullFragmentPath) {
			std::cerr << "ERROR::SHADER::FILES_NOT_FOUND" << std::endl;
			std::cerr << "Could not find shaders: " << vertexPath << " and " << fragmentPath << std::endl;
			std::cerr << "Searched relative to executable dir and current working directory" << std::endl;
			return false;
		}

		// Read vertex shader
		std::string	  vertexCode;
		std::ifstream vShaderFile;
		vShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
		try {
			vShaderFile.open(fullVertexPath.value());
			std::stringstream vShaderStream;
			vShaderStream << vShaderFile.rdbuf();
			vShaderFile.close();
			vertexCode = vShaderStream.str();
		} catch (const std::ifstream::failure& e) {
			std::cerr << "ERROR::SHADER::VERTEX::FILE_NOT_SUCCESSFULLY_READ: " << e.what() << std::endl;
			std::cerr << "Tried to open: " << fullVertexPath.value() << std::endl;
			return false;
		}

		// Read fragment shader
		std::string	  fragmentCode;
		std::ifstream fShaderFile;
		fShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
		try {
			fShaderFile.open(fullFragmentPath.value());
			std::stringstream fShaderStream;
			fShaderStream << fShaderFile.rdbuf();
			fShaderFile.close();
			fragmentCode = fShaderStream.str();
		} catch (const std::ifstream::failure& e) {
			std::cerr << "ERROR::SHADER::FRAGMENT::FILE_NOT_SUCCESSFULLY_READ: " << e.what() << std::endl;
			std::cerr << "Tried to open: " << fullFragmentPath.value() << std::endl;
			return false;
		}

		const char* vShaderCode = vertexCode.c_str();
		const char* fShaderCode = fragmentCode.c_str();

		// Compile shaders
		GLuint vertex = 0;
		GLuint fragment = 0;
		GLint  success = 0;
		char   infoLog[512];

		// Vertex shader
		vertex = glCreateShader(GL_VERTEX_SHADER);
		glShaderSource(vertex, 1, &vShaderCode, nullptr);
		glCompileShader(vertex);
		glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
		if (success == 0) {
			glGetShaderInfoLog(vertex, 512, nullptr, infoLog);
			std::cerr << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
			glDeleteShader(vertex);
			return false;
		}

		// Fragment shader
		fragment = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(fragment, 1, &fShaderCode, nullptr);
		glCompileShader(fragment);
		glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
		if (success == 0) {
			glGetShaderInfoLog(fragment, 512, nullptr, infoLog);
			std::cerr << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
			glDeleteShader(vertex);
			glDeleteShader(fragment);
			return false;
		}

		// Shader program
		program = glCreateProgram();
		glAttachShader(program, vertex);
		glAttachShader(program, fragment);
		glLinkProgram(program);
		glGetProgramiv(program, GL_LINK_STATUS, &success);
		if (success == 0) {
			glGetProgramInfoLog(program, 512, nullptr, infoLog);
			std::cerr << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
			glDeleteShader(vertex);
			glDeleteShader(fragment);
			glDeleteProgram(program);
			program = 0;
			return false;
		}

		// Delete shaders (they're linked into the program now)
		glDeleteShader(vertex);
		glDeleteShader(fragment);

		return true;
	}

	void Shader::use() const {
		if (program != 0) {
			glUseProgram(program);
		}
	}

	void Shader::unbind() const { // NOLINT(readability-convert-member-functions-to-static)
		glUseProgram(0);
	}

	void Shader::setUniform(const char* name, const glm::mat4& value) const {
		GLint location = glGetUniformLocation(program, name);
		if (location != -1) {
			glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(value));
		}
	}

	void Shader::setUniform(const char* name, int value) const {
		GLint location = glGetUniformLocation(program, name);
		if (location != -1) {
			glUniform1i(location, value);
		}
	}

	void Shader::setUniform(const char* name, float value) const {
		GLint location = glGetUniformLocation(program, name);
		if (location != -1) {
			glUniform1f(location, value);
		}
	}

} // namespace Renderer

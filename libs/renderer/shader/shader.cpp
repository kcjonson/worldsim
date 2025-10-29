// Shader utility implementation

#include "shader/shader.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <glm/gtc/type_ptr.hpp>

namespace Renderer {

Shader::~Shader() {
	if (m_program != 0) {
		glDeleteProgram(m_program);
		m_program = 0;
	}
}

Shader::Shader(Shader&& other) noexcept : m_program(other.m_program) {
	other.m_program = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
	if (this != &other) {
		if (m_program != 0) {
			glDeleteProgram(m_program);
		}
		m_program = other.m_program;
		other.m_program = 0;
	}
	return *this;
}

bool Shader::LoadFromFile(const char* vertexPath, const char* fragmentPath) {
	// Get the executable's directory and locate shaders
	std::filesystem::path exePath = std::filesystem::current_path();
	std::filesystem::path shaderDir = exePath / "shaders";

	// Construct full paths
	std::filesystem::path fullVertexPath = shaderDir / vertexPath;
	std::filesystem::path fullFragmentPath = shaderDir / fragmentPath;

	// Read vertex shader
	std::string vertexCode;
	std::ifstream vShaderFile;
	vShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
	try {
		vShaderFile.open(fullVertexPath);
		std::stringstream vShaderStream;
		vShaderStream << vShaderFile.rdbuf();
		vShaderFile.close();
		vertexCode = vShaderStream.str();
	} catch (const std::ifstream::failure& e) {
		std::cerr << "ERROR::SHADER::VERTEX::FILE_NOT_SUCCESSFULLY_READ: " << e.what() << std::endl;
		std::cerr << "Tried to open: " << fullVertexPath << std::endl;
		return false;
	}

	// Read fragment shader
	std::string fragmentCode;
	std::ifstream fShaderFile;
	fShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
	try {
		fShaderFile.open(fullFragmentPath);
		std::stringstream fShaderStream;
		fShaderStream << fShaderFile.rdbuf();
		fShaderFile.close();
		fragmentCode = fShaderStream.str();
	} catch (const std::ifstream::failure& e) {
		std::cerr << "ERROR::SHADER::FRAGMENT::FILE_NOT_SUCCESSFULLY_READ: " << e.what() << std::endl;
		std::cerr << "Tried to open: " << fullFragmentPath << std::endl;
		return false;
	}

	const char* vShaderCode = vertexCode.c_str();
	const char* fShaderCode = fragmentCode.c_str();

	// Compile shaders
	GLuint vertex, fragment;
	GLint success;
	char infoLog[512];

	// Vertex shader
	vertex = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertex, 1, &vShaderCode, nullptr);
	glCompileShader(vertex);
	glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
	if (!success) {
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
	if (!success) {
		glGetShaderInfoLog(fragment, 512, nullptr, infoLog);
		std::cerr << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
		glDeleteShader(vertex);
		glDeleteShader(fragment);
		return false;
	}

	// Shader program
	m_program = glCreateProgram();
	glAttachShader(m_program, vertex);
	glAttachShader(m_program, fragment);
	glLinkProgram(m_program);
	glGetProgramiv(m_program, GL_LINK_STATUS, &success);
	if (!success) {
		glGetProgramInfoLog(m_program, 512, nullptr, infoLog);
		std::cerr << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
		glDeleteShader(vertex);
		glDeleteShader(fragment);
		glDeleteProgram(m_program);
		m_program = 0;
		return false;
	}

	// Delete shaders (they're linked into the program now)
	glDeleteShader(vertex);
	glDeleteShader(fragment);

	return true;
}

void Shader::Use() const {
	if (m_program != 0) {
		glUseProgram(m_program);
	}
}

void Shader::Unbind() const {
	glUseProgram(0);
}

void Shader::SetUniform(const char* name, const glm::mat4& value) const {
	GLint location = glGetUniformLocation(m_program, name);
	if (location != -1) {
		glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(value));
	}
}

} // namespace Renderer

// ShaderPreprocessor implementation

#include "shader/ShaderPreprocessor.h"

#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

namespace Renderer {

std::optional<std::string> ShaderPreprocessor::process(const std::filesystem::path& shaderPath) {
	// Load the main shader file
	auto source = loadFile(shaderPath);
	if (!source) {
		return std::nullopt;
	}

	// Track included files to prevent cycles
	std::unordered_set<std::string> included;
	included.insert(std::filesystem::canonical(shaderPath).string());

	// Resolve includes relative to the shader's directory
	return resolveIncludes(*source, shaderPath.parent_path(), included);
}

std::optional<std::string> ShaderPreprocessor::loadFile(const std::filesystem::path& path) {
	std::ifstream file(path);
	if (!file.is_open()) {
		std::cerr << "ERROR::SHADER_PREPROCESSOR::FILE_NOT_FOUND: " << path << std::endl;
		return std::nullopt;
	}

	std::stringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}

std::optional<std::string> ShaderPreprocessor::resolveIncludes(
	const std::string&				  source,
	const std::filesystem::path&	  basePath,
	std::unordered_set<std::string>& included
) {
	std::stringstream result;
	std::istringstream stream(source);
	std::string		   line;
	int				   lineNumber = 0;

	while (std::getline(stream, line)) {
		lineNumber++;

		// Check if this line is an #include directive
		auto includeFile = parseIncludeLine(line);
		if (includeFile) {
			// Resolve the include path relative to current file
			std::filesystem::path includePath = basePath / *includeFile;

			// Normalize to absolute path for cycle detection
			std::error_code ec;
			auto			canonicalPath = std::filesystem::canonical(includePath, ec);
			if (ec) {
				std::cerr << "ERROR::SHADER_PREPROCESSOR::INCLUDE_NOT_FOUND: " << includePath << std::endl;
				std::cerr << "  Referenced from line " << lineNumber << std::endl;
				return std::nullopt;
			}

			std::string canonicalStr = canonicalPath.string();

			// Check for circular include
			if (included.count(canonicalStr) > 0) {
				std::cerr << "ERROR::SHADER_PREPROCESSOR::CIRCULAR_INCLUDE: " << *includeFile << std::endl;
				std::cerr << "  At line " << lineNumber << std::endl;
				return std::nullopt;
			}

			// Mark as included
			included.insert(canonicalStr);

			// Load and process the included file
			auto includedSource = loadFile(canonicalPath);
			if (!includedSource) {
				return std::nullopt;
			}

			// Add line directive for better error messages
			result << "// BEGIN INCLUDE: " << *includeFile << "\n";

			// Recursively resolve includes in the included file
			auto processedInclude = resolveIncludes(*includedSource, canonicalPath.parent_path(), included);
			if (!processedInclude) {
				return std::nullopt;
			}

			result << *processedInclude;
			result << "// END INCLUDE: " << *includeFile << "\n";
		} else {
			// Regular line, pass through
			result << line << "\n";
		}
	}

	return result.str();
}

std::optional<std::string> ShaderPreprocessor::parseIncludeLine(const std::string& line) {
	// Match #include "filename" with optional whitespace
	// Supports both #include "file.glsl" and #include <file.glsl> styles
	static const std::regex includeRegex(R"(^\s*#\s*include\s*["<]([^">]+)[">]\s*$)");

	std::smatch match;
	if (std::regex_match(line, match, includeRegex)) {
		return match[1].str();
	}

	return std::nullopt;
}

} // namespace Renderer

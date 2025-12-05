#pragma once

// ShaderPreprocessor - Resolves #include directives in GLSL shader files.
//
// Supports `#include "filename.glsl"` directive for organizing shader code
// across multiple files while maintaining a single compiled shader.
//
// Include paths are resolved relative to the including file's directory.
// Circular includes are detected and prevented.

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>

namespace Renderer {

class ShaderPreprocessor {
  public:
	/// Process a shader file, resolving all #include directives recursively.
	/// @param shaderPath Absolute path to the shader file
	/// @return Preprocessed shader source, or nullopt on error
	static std::optional<std::string> process(const std::filesystem::path& shaderPath);

  private:
	/// Load file contents as string
	static std::optional<std::string> loadFile(const std::filesystem::path& path);

	/// Recursively resolve #include directives
	/// @param source Shader source code
	/// @param basePath Directory containing the current file (for relative includes)
	/// @param included Set of already-included files (for cycle detection)
	/// @return Processed source with includes expanded
	static std::optional<std::string> resolveIncludes(
		const std::string&				  source,
		const std::filesystem::path&	  basePath,
		std::unordered_set<std::string>& included
	);

	/// Parse an #include directive line and extract the filename
	/// @param line Line of source code
	/// @return Included filename if line is an include directive, nullopt otherwise
	static std::optional<std::string> parseIncludeLine(const std::string& line);
};

} // namespace Renderer

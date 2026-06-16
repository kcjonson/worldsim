#pragma once

// Load-time validation of asset definitions. Surfaces the problems the loader
// otherwise swallows (silent coercions, documented-but-ignored fields, missing
// references, duplicate names, orphaned files), so the game (at launch) and the
// Asset Manager share one rule set and one report.

#include <filesystem>
#include <string>
#include <vector>

namespace engine::assets {

	enum class Severity {
		Warning,
		Error,
	};

	struct ValidationIssue {
		Severity	severity;
		std::string defName; // empty for file/folder-level issues
		std::string field;	 // empty if not field-specific
		std::string message;
		std::string context; // e.g. the offending raw value or resolved path
	};

	struct ValidationReport {
		std::vector<ValidationIssue> issues;

		void add(Severity severity, std::string defName, std::string field, std::string message, std::string context = "");
		[[nodiscard]] int	errorCount() const;
		[[nodiscard]] int	warningCount() const;
		[[nodiscard]] bool	hasErrors() const;
	};

	class AssetValidator {
	  public:
		// Walk the asset folder tree and validate every primary definition.
		// No GL and no AssetRegistry state; safe to run on a load worker thread.
		// @param assetsRoot Absolute path to the assets root that was loaded
		// @param sharedScriptsPath Path used to resolve @shared/ script references
		static ValidationReport validate(const std::filesystem::path& assetsRoot, const std::filesystem::path& sharedScriptsPath);
	};

} // namespace engine::assets

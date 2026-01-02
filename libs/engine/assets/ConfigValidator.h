#pragma once

// Config Validator
// Validates cross-registry references in work configuration files.
// Follows fail-fast philosophy - any invalid reference is fatal.
//
// See /docs/technical/task-generation-architecture.md#config-validation for details.

#include <string>
#include <vector>

namespace engine::assets {

/// Validation error information
struct ValidationError {
    /// Source config file/type
    std::string source;

    /// Error message
    std::string message;

    /// Additional context (e.g., available options)
    std::string context;
};

/// Validates work configuration files for referential integrity.
/// Call after loading each registry in dependency order.
class ConfigValidator {
  public:
    // --- Per-Registry Validation ---

    /// Validate ActionTypes - just syntax (no dependencies)
    /// @return true if valid
    static bool validateActionTypes();

    /// Validate TaskChains - check action references
    /// @return true if valid
    static bool validateTaskChains();

    /// Validate WorkTypes - check chain references and capability names
    /// @return true if valid
    static bool validateWorkTypes();

    /// Validate PriorityConfig - check category references
    /// @return true if valid
    static bool validatePriorityConfig();

    // --- Full Validation ---

    /// Validate all registries (call after all configs loaded)
    /// @return true if all valid
    static bool validateAll();

    // --- Error Reporting ---

    /// Get all validation errors from last validation run
    [[nodiscard]] static const std::vector<ValidationError>& getErrors();

    /// Get error count from last validation run
    [[nodiscard]] static size_t getErrorCount();

    /// Clear accumulated errors
    static void clearErrors();

  private:
    /// Add an error to the list
    static void addError(const std::string& source, const std::string& message,
                        const std::string& context = "");

    /// Error storage
    static std::vector<ValidationError> s_errors;
};

} // namespace engine::assets

// Config Validator Implementation
// Validates cross-registry references for work configuration.

#include "assets/ConfigValidator.h"
#include "assets/ActionTypeRegistry.h"
#include "assets/PriorityConfig.h"
#include "assets/TaskChainRegistry.h"
#include "assets/WorkTypeRegistry.h"

#include <utils/Log.h>

namespace engine::assets {

std::vector<ValidationError> ConfigValidator::s_errors;

bool ConfigValidator::validateActionTypes() {
    // ActionTypes has no dependencies - just check it's loaded
    if (ActionTypeRegistry::Get().size() == 0) {
        addError("ActionTypes", "No action types loaded",
                "Ensure assets/config/actions/action-types.xml exists and is valid");
        return false;
    }
    return true;
}

bool ConfigValidator::validateTaskChains() {
    const auto& chainRegistry = TaskChainRegistry::Get();
    const auto& actionRegistry = ActionTypeRegistry::Get();

    bool valid = true;

    for (const auto& [chainDefName, chain] : chainRegistry.getAllChains()) {
        for (const auto& step : chain.steps) {
            // Check action reference
            if (!actionRegistry.hasAction(step.actionDefName)) {
                addError("TaskChains",
                        "Chain '" + chainDefName + "' step " + std::to_string(step.order) +
                        " references unknown action '" + step.actionDefName + "'",
                        "Available actions: " + actionRegistry.getAvailableActionsString());
                valid = false;
            }
        }
    }

    return valid;
}

bool ConfigValidator::validateWorkTypes() {
    const auto& workTypeRegistry = WorkTypeRegistry::Get();
    const auto& chainRegistry = TaskChainRegistry::Get();

    bool valid = true;

    for (const auto& workTypeDefName : workTypeRegistry.getWorkTypeNames()) {
        const auto* workType = workTypeRegistry.getWorkType(workTypeDefName);
        if (!workType) continue;

        // Check task chain reference
        if (workType->taskChain.has_value() && !workType->taskChain->empty()) {
            if (!chainRegistry.hasChain(*workType->taskChain)) {
                std::string availableChains;
                for (const auto& name : chainRegistry.getChainNames()) {
                    if (!availableChains.empty()) availableChains += ", ";
                    availableChains += name;
                }

                addError("WorkTypes",
                        "WorkType '" + workTypeDefName + "' references unknown chain '" +
                        *workType->taskChain + "'",
                        "Available chains: " + availableChains);
                valid = false;
            }
        }

        // Check trigger capability is specified
        if (workType->triggerCapability.empty()) {
            LOG_WARNING(Engine,
                "WorkType '%s' has no triggerCapability - it won't generate any tasks",
                workTypeDefName.c_str());
            // Not an error, just a warning
        }
    }

    return valid;
}

bool ConfigValidator::validatePriorityConfig() {
    const auto& priorityConfig = PriorityConfig::Get();
    const auto& workTypeRegistry = WorkTypeRegistry::Get();

    bool valid = true;

    // Check that category order references valid categories
    for (const auto& categoryName : priorityConfig.getCategoryOrder()) {
        if (!workTypeRegistry.hasCategory(categoryName)) {
            std::string availableCategories;
            for (const auto& name : workTypeRegistry.getCategoryNames()) {
                if (!availableCategories.empty()) availableCategories += ", ";
                availableCategories += name;
            }

            addError("PriorityConfig",
                    "WorkCategoryOrder references unknown category '" + categoryName + "'",
                    "Available categories: " + availableCategories);
            valid = false;
        }
    }

    return valid;
}

bool ConfigValidator::validateAll() {
    clearErrors();

    bool valid = true;

    // Validate in dependency order
    if (!validateActionTypes()) {
        LOG_ERROR(Engine, "ActionTypes validation failed");
        valid = false;
    }

    if (!validateTaskChains()) {
        LOG_ERROR(Engine, "TaskChains validation failed");
        valid = false;
    }

    if (!validateWorkTypes()) {
        LOG_ERROR(Engine, "WorkTypes validation failed");
        valid = false;
    }

    if (!validatePriorityConfig()) {
        LOG_ERROR(Engine, "PriorityConfig validation failed");
        valid = false;
    }

    // Log all errors
    for (const auto& error : s_errors) {
        LOG_ERROR(Engine, "[%s] %s\n  %s",
                 error.source.c_str(),
                 error.message.c_str(),
                 error.context.c_str());
    }

    if (valid) {
        LOG_INFO(Engine, "All work configs validated successfully");
    } else {
        LOG_ERROR(Engine, "Work config validation failed with %zu error(s)", s_errors.size());
    }

    return valid;
}

const std::vector<ValidationError>& ConfigValidator::getErrors() {
    return s_errors;
}

size_t ConfigValidator::getErrorCount() {
    return s_errors.size();
}

void ConfigValidator::clearErrors() {
    s_errors.clear();
}

void ConfigValidator::addError(const std::string& source, const std::string& message,
                              const std::string& context) {
    s_errors.push_back({source, message, context});
}

} // namespace engine::assets

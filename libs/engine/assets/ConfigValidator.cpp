// Config Validator Implementation
// Validates cross-registry references for work configuration.

#include "assets/ConfigValidator.h"
#include "assets/ActionTypeRegistry.h"
#include "assets/ConstructionRegistry.h"
#include "assets/PriorityConfig.h"
#include "assets/TaskChainRegistry.h"
#include "assets/WorkTypeRegistry.h"

#include <utils/Log.h>

namespace engine::assets {

std::vector<ValidationError> ConfigValidator::errors;

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

bool ConfigValidator::validateConstruction() {
    const auto& reg = ConstructionRegistry::Get();

    bool valid = true;

    // Materials must be loaded and each palette must be non-empty.
    if (!reg.materialsLoaded()) {
        addError("ConstructionMaterials",
                 "No construction materials loaded",
                 "Ensure assets/config/construction/materials.xml exists and is valid");
        return false;
    }

    for (const auto& [name, mat] : reg.getAllMaterials()) {
        if (mat.pattern.palette.empty()) {
            addError("ConstructionMaterials",
                     "Material '" + name + "' has an empty pattern palette",
                     "Add at least one <color> entry inside <palette>");
            valid = false;
        }
    }

    // Constraints must be loaded and internally consistent.
    if (!reg.constraintsLoaded()) {
        addError("ConstructionConstraints",
                 "Construction constraints not loaded",
                 "Ensure assets/config/construction/constraints.xml exists and is valid");
        return false;
    }

    const auto& c = reg.constraints();

    if (c.minCornerAngleDegrees <= 0.0F || c.minCornerAngleDegrees >= 180.0F) {
        addError("ConstructionConstraints",
                 "minCornerAngleDegrees must be in (0, 180), got " +
                     std::to_string(c.minCornerAngleDegrees),
                 "Suggested range: 15-60 degrees");
        valid = false;
    }

    if (c.pathingClearanceMeters > c.segmentClearanceMeters) {
        addError("ConstructionConstraints",
                 "pathingClearanceMeters (" + std::to_string(c.pathingClearanceMeters) +
                     ") must be <= segmentClearanceMeters (" +
                     std::to_string(c.segmentClearanceMeters) + ")",
                 "segmentClearance enforces the gap that pathingClearance requires");
        valid = false;
    }

    if (c.minAreaSquareMeters >= c.maxAreaSquareMeters) {
        addError("ConstructionConstraints",
                 "minAreaSquareMeters must be < maxAreaSquareMeters",
                 "Got min=" + std::to_string(c.minAreaSquareMeters) +
                     " max=" + std::to_string(c.maxAreaSquareMeters));
        valid = false;
    }

    if (c.maxPoints < 3) {
        addError("ConstructionConstraints",
                 "maxPoints must be >= 3 (a triangle is the minimum polygon), got " +
                     std::to_string(c.maxPoints),
                 "");
        valid = false;
    }

    if (c.refundPercent < 0.0F || c.refundPercent > 100.0F) {
        addError("ConstructionConstraints",
                 "refundPercent must be in [0, 100], got " + std::to_string(c.refundPercent),
                 "");
        valid = false;
    }

    // Snapping must be loaded with positive radii.
    if (!reg.snappingLoaded()) {
        addError("ConstructionSnapping",
                 "Construction snapping config not loaded",
                 "Ensure assets/config/construction/snapping.xml exists and is valid");
        return false;
    }

    const auto& s = reg.snapping();

    if (s.vertexSnapRadiusMeters <= 0.0F) {
        addError("ConstructionSnapping",
                 "vertexSnapRadiusMeters must be positive, got " +
                     std::to_string(s.vertexSnapRadiusMeters),
                 "");
        valid = false;
    }

    if (s.edgeSnapRadiusMeters <= 0.0F) {
        addError("ConstructionSnapping",
                 "edgeSnapRadiusMeters must be positive, got " +
                     std::to_string(s.edgeSnapRadiusMeters),
                 "");
        valid = false;
    }

    if (s.originCloseRadiusMeters <= 0.0F) {
        addError("ConstructionSnapping",
                 "originCloseRadiusMeters must be positive, got " +
                     std::to_string(s.originCloseRadiusMeters),
                 "");
        valid = false;
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

    // Construction config is optional at this call site; only validate if loaded.
    if (ConstructionRegistry::Get().materialsLoaded() ||
        ConstructionRegistry::Get().constraintsLoaded() ||
        ConstructionRegistry::Get().snappingLoaded()) {
        if (!validateConstruction()) {
            LOG_ERROR(Engine, "Construction config validation failed");
            valid = false;
        }
    }

    // Log all errors
    for (const auto& error : errors) {
        LOG_ERROR(Engine, "[%s] %s\n  %s",
                 error.source.c_str(),
                 error.message.c_str(),
                 error.context.c_str());
    }

    if (valid) {
        LOG_INFO(Engine, "All configs validated successfully");
    } else {
        LOG_ERROR(Engine, "Config validation failed with %zu error(s)", errors.size());
    }

    return valid;
}

const std::vector<ValidationError>& ConfigValidator::getErrors() {
    return errors;
}

size_t ConfigValidator::getErrorCount() {
    return errors.size();
}

void ConfigValidator::clearErrors() {
    errors.clear();
}

void ConfigValidator::addError(const std::string& source, const std::string& message,
                              const std::string& context) {
    errors.push_back({source, message, context});
}

} // namespace engine::assets

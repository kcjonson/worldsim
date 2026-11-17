// Config Validator Implementation
// Validates cross-registry references for work configuration.

#include "assets/ConfigValidator.h"
#include "assets/ActionTypeRegistry.h"
#include "assets/ConstructionRegistry.h"
#include "assets/MemoryConfig.h"
#include "assets/MoodConfig.h"
#include "assets/PriorityConfig.h"
#include "assets/TaskChainRegistry.h"
#include "assets/WorkTypeRegistry.h"

#include <utils/Log.h>

#include <cmath>
#include <string>
#include <unordered_set>

namespace assets {

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

    for (const std::string& problem : priorityConfig.problems()) {
        addError("PriorityConfig", problem);
        valid = false;
    }

    const float maxDistance = priorityConfig.getDistanceFactorConfig().maxDistance;
    if (std::isfinite(maxDistance) && maxDistance <= 0.0F) {
        addError("PriorityConfig", "DistanceFactor maxDistance must be a positive number", "got " + std::to_string(maxDistance));
        valid = false;
    }

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

bool ConfigValidator::validateMemory(const MemoryConfig& config) {
    const std::string source = "Memory";

    if (!config.loaded()) {
        addError(source, "No memory config loaded", "Ensure assets/config/colonists/memory.xml exists and is valid");
        return false;
    }

    if (!std::isfinite(config.recentSightingGameMinutes()) || config.recentSightingGameMinutes() <= 0.0F) {
        addError(source, "RecentSighting gameMinutes must be a positive number", "Set <RecentSighting gameMinutes=\"...\"/>");
        return false;
    }
    return true;
}

bool ConfigValidator::validateMood(const MoodConfig& config) {
    const std::string source = "Mood";
    bool valid = true;

    for (const std::string& problem : config.problems()) {
        addError(source, problem);
        valid = false;
    }

    if (!config.loaded()) {
        if (config.problems().empty()) {
            addError(source, "No mood config loaded", "Ensure assets/config/colonists/mood.xml exists and is valid");
        }
        return false;
    }

    if (std::isfinite(config.pointsPerGameHour()) && config.pointsPerGameHour() <= 0.0F) {
        addError(source, "Easing pointsPerGameHour must be a positive number", "Set <Easing pointsPerGameHour=\"...\"/>");
        valid = false;
    }

    const NeedPenaltyCurve& curve = config.needPenaltyCurve();
    if (std::isfinite(curve.comfortable) && std::isfinite(curve.warning) && std::isfinite(curve.penaltyAtWarning)) {
        if (curve.warning <= 0.0F || curve.comfortable <= curve.warning || curve.comfortable > 100.0F) {
            addError(source, "NeedPenalty needs 0 < warning < comfortable <= 100",
                     "comfortable " + std::to_string(curve.comfortable) + ", warning " + std::to_string(curve.warning));
            valid = false;
        }
        if (curve.penaltyAtWarning < 0.0F || curve.penaltyAtWarning > 1.0F) {
            addError(source, "NeedPenalty penaltyAtWarning must be 0 to 1",
                     "got " + std::to_string(curve.penaltyAtWarning));
            valid = false;
        }
    }

    float totalWeight = 0.0F;
    std::unordered_set<std::string> weightedNeeds;
    for (const NeedMoodWeight& needWeight : config.needWeights()) {
        if (needWeight.need.empty()) {
            addError(source, "A <Need> in <NeedWeights> has no name");
            valid = false;
            continue;
        }
        if (!weightedNeeds.insert(needWeight.need).second) {
            addError(source, "Need '" + needWeight.need + "' is weighted more than once");
            valid = false;
        }
        if (!std::isfinite(needWeight.weight)) {
            continue;
        }
        if (needWeight.weight < 0.0F) {
            addError(source, "Need '" + needWeight.need + "' weight must be zero or more");
            valid = false;
            continue;
        }
        totalWeight += needWeight.weight;
    }
    if (totalWeight <= 0.0F) {
        addError(source, "NeedWeights must give at least one need a positive weight");
        valid = false;
    }

    const auto& bands = config.bands();
    if (bands.empty()) {
        addError(source, "No mood bands", "Add <Band name=\"...\" from=\"...\"/> entries inside <Bands>");
        return false;
    }
    for (size_t i = 0; i < bands.size(); ++i) {
        if (bands[i].name.empty()) {
            addError(source, "A <Band> has no name");
            valid = false;
        }
        if (std::isfinite(bands[i].from) && i > 0 && bands[i].from == bands[i - 1].from) {
            addError(source, "Bands '" + bands[i - 1].name + "' and '" + bands[i].name + "' start at the same mood");
            valid = false;
        }
    }
    if (std::isfinite(bands.back().from) && bands.back().from > 0.0F) {
        addError(source, "The lowest band must start at 0 or below so every mood has a band",
                 "Band '" + bands.back().name + "' starts at " + std::to_string(bands.back().from));
        valid = false;
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

        // Numeric rates must be positive: a zero workRate yields workTotal 0 (a
        // free instant building) and a zero costRate yields no material
        // requirement at all. beauty is intentionally unchecked (may be 0 or
        // negative). See ConstructionRegistry.test.cpp Materials_SaneValues for
        // the contract this enforces.
        if (mat.costRatePerSquareMeter <= 0.0F) {
            addError("ConstructionMaterials",
                     "Material '" + name + "' costRatePerSquareMeter must be positive, got " +
                         std::to_string(mat.costRatePerSquareMeter),
                     "Set <costRatePerSquareMeter> to a positive value");
            valid = false;
        }
        if (mat.workRatePerSquareMeter <= 0.0F) {
            addError("ConstructionMaterials",
                     "Material '" + name + "' workRatePerSquareMeter must be positive, got " +
                         std::to_string(mat.workRatePerSquareMeter),
                     "Set <workRatePerSquareMeter> to a positive value");
            valid = false;
        }
        if (mat.hp <= 0.0F) {
            addError("ConstructionMaterials",
                     "Material '" + name + "' hp must be positive, got " + std::to_string(mat.hp),
                     "Set <hp> to a positive value");
            valid = false;
        }
        if (mat.speedModifier <= 0.0F) {
            addError("ConstructionMaterials",
                     "Material '" + name + "' speedModifier must be positive, got " +
                         std::to_string(mat.speedModifier),
                     "Set <speedModifier> to a positive value (1.0 = no change)");
            valid = false;
        }
        if (mat.flammability < 0.0F || mat.flammability > 1.0F) {
            addError("ConstructionMaterials",
                     "Material '" + name + "' flammability must be in [0, 1], got " +
                         std::to_string(mat.flammability),
                     "0.0 = fireproof, 1.0 = highly flammable");
            valid = false;
        }

        // Wall thickness preset validation
        std::unordered_map<std::string, bool> seenPresetNames;
        for (const auto& preset : mat.wallThicknesses) {
            if (seenPresetNames.count(preset.name)) {
                addError("ConstructionMaterials",
                         "Material '" + name + "' has duplicate wall preset name '" + preset.name + "'",
                         "Each preset name must be unique within a material");
                valid = false;
            }
            seenPresetNames[preset.name] = true;

            if (preset.thicknessMeters <= 0.0F) {
                addError("ConstructionMaterials",
                         "Material '" + name + "' preset '" + preset.name +
                             "' thicknessMeters must be positive, got " +
                             std::to_string(preset.thicknessMeters),
                         "Set <thicknessMeters> to a positive value");
                valid = false;
            }
            if (preset.costMultiplier <= 0.0F) {
                addError("ConstructionMaterials",
                         "Material '" + name + "' preset '" + preset.name +
                             "' costMultiplier must be positive, got " +
                             std::to_string(preset.costMultiplier),
                         "Set <costMultiplier> to a positive value");
                valid = false;
            }
            if (preset.workMultiplier <= 0.0F) {
                addError("ConstructionMaterials",
                         "Material '" + name + "' preset '" + preset.name +
                             "' workMultiplier must be positive, got " +
                             std::to_string(preset.workMultiplier),
                         "Set <workMultiplier> to a positive value");
                valid = false;
            }
            if (preset.hpMultiplier <= 0.0F) {
                addError("ConstructionMaterials",
                         "Material '" + name + "' preset '" + preset.name +
                             "' hpMultiplier must be positive, got " +
                             std::to_string(preset.hpMultiplier),
                         "Set <hpMultiplier> to a positive value");
                valid = false;
            }
            if (preset.insulation < 0.0F) {
                addError("ConstructionMaterials",
                         "Material '" + name + "' preset '" + preset.name +
                             "' insulation must be >= 0, got " + std::to_string(preset.insulation),
                         "Set <insulation> to a non-negative value");
                valid = false;
            }
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

    if (c.builderSpotMinSpacingMeters <= 0.0F) {
        addError("ConstructionConstraints",
                 "builderSpotMinSpacingMeters must be positive, got " +
                     std::to_string(c.builderSpotMinSpacingMeters),
                 "Set <builderSpotMinSpacingMeters> to a positive value (suggested: 1.0)");
        valid = false;
    }

    // Wall constraint validation
    if (c.minSegmentLengthMeters <= 0.0F) {
        addError("ConstructionConstraints",
                 "minSegmentLengthMeters must be positive, got " +
                     std::to_string(c.minSegmentLengthMeters),
                 "Set <minSegmentLengthMeters> to a positive value (suggested: 0.5)");
        valid = false;
    }

    if (c.minWallJunctionAngleDegrees <= 0.0F || c.minWallJunctionAngleDegrees >= 180.0F) {
        addError("ConstructionConstraints",
                 "minWallJunctionAngleDegrees must be in (0, 180), got " +
                     std::to_string(c.minWallJunctionAngleDegrees),
                 "Suggested range: 15-60 degrees");
        valid = false;
    }

    if (c.minParallelClearanceMeters < 0.0F) {
        addError("ConstructionConstraints",
                 "minParallelClearanceMeters must be >= 0, got " +
                     std::to_string(c.minParallelClearanceMeters),
                 "Set <minParallelClearanceMeters> to a non-negative value");
        valid = false;
    }

    if (c.minParallelClearanceMeters > 0.0F && c.minParallelClearanceMeters < c.pathingClearanceMeters) {
        addError("ConstructionConstraints",
                 "minParallelClearanceMeters (" + std::to_string(c.minParallelClearanceMeters) +
                     ") must be >= pathingClearanceMeters (" +
                     std::to_string(c.pathingClearanceMeters) + ") so colonists can pass between walls",
                 "Increase <minParallelClearanceMeters> or decrease <pathingClearanceMeters>");
        valid = false;
    }

    if (c.openingMarginMeters < 0.0F) {
        addError("ConstructionConstraints",
                 "openingMarginMeters must be >= 0, got " + std::to_string(c.openingMarginMeters),
                 "Set <openingMarginMeters> to a non-negative value (suggested: 0.3)");
        valid = false;
    }

    // Opening type validation. Cost/work are constants per type; widths must be
    // positive, the material must resolve, pathable is a parsed bool (no check
    // needed), and the opening plus its end margins must fit within the LONGEST
    // wall a tool could draw, or it could never be placed anywhere. Cross-checks
    // materials AND constraints, so it runs here after both are confirmed loaded.
    for (const auto& type : reg.openingTypes()) {
        if (type.widthMeters <= 0.0F) {
            addError("ConstructionOpenings",
                     "Opening type '" + type.name + "' widthMeters must be positive, got " +
                         std::to_string(type.widthMeters),
                     "Set <widthMeters> to a positive value");
            valid = false;
        }
        if (type.material.empty() || reg.getMaterial(type.material) == nullptr) {
            addError("ConstructionOpenings",
                     "Opening type '" + type.name + "' references unknown material '" + type.material + "'",
                     "Set <material> to a loaded construction material (e.g. Wood)");
            valid = false;
        }
        if (type.costItems <= 0.0F) {
            addError("ConstructionOpenings",
                     "Opening type '" + type.name + "' costItems must be positive, got " +
                         std::to_string(type.costItems),
                     "Set <costItems> to a positive value");
            valid = false;
        }
        if (type.workUnits <= 0.0F) {
            addError("ConstructionOpenings",
                     "Opening type '" + type.name + "' workUnits must be positive, got " +
                         std::to_string(type.workUnits),
                     "Set <workUnits> to a positive value");
            valid = false;
        }
        // Fit sanity: an opening needs width + 2*margin of wall to honor its end
        // margins. Reject a type only if that exceeds the LONGEST wall a tool could
        // draw -- a wall can't be longer than the largest foundation's diagonal, so
        // bound it coarsely by the DIAGONAL of the max-area square, sqrt(2*maxArea)
        // (the side, sqrt(maxArea), would under-count and false-reject a type that
        // fits on a diagonal wall). A type needing more than that could never be
        // placed on any legal wall.
        if (type.widthMeters > 0.0F && c.maxAreaSquareMeters > 0.0F) {
            const float needed = type.widthMeters + 2.0F * c.openingMarginMeters;
            const float maxWallLength = std::sqrt(2.0F * c.maxAreaSquareMeters);
            if (needed > maxWallLength) {
                addError("ConstructionOpenings",
                         "Opening type '" + type.name + "' needs " + std::to_string(needed) +
                             " m (width + 2*margin) but the longest possible wall is about " +
                             std::to_string(maxWallLength) + " m",
                         "Reduce <widthMeters>/<openingMarginMeters> or raise <maxAreaSquareMeters>");
                valid = false;
            }
        }
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

    const char* const snappingSource = "ConstructionSnapping";
    const char* const renderingSource = "ConstructionRendering";

    const auto require = [&](const char* source, bool ok, const char* name, float value, const char* bound) {
        if (!ok) {
            addError(source, std::string(name) + " must be " + bound + ", got " + std::to_string(value), "");
            valid = false;
        }
    };
    const auto rejectParseErrors = [&](const char* source, const std::vector<ConfigParseError>& parseErrors) {
        for (const auto& parseError : parseErrors) {
            addError(source, parseError.message, parseError.context);
            valid = false;
        }
    };
    rejectParseErrors(snappingSource, reg.snappingParseErrors());
    rejectParseErrors(renderingSource, reg.renderingParseErrors());

    const float angleWindow = s.effectiveGuideAngleWindowDegrees();
    const float crossingMinimum = s.effectiveGuideCrossingMinAngleDegrees();

    require(snappingSource, s.angleIncrementDegrees > 0.0F && s.angleIncrementDegrees <= 90.0F, "angleIncrementDegrees",
            s.angleIncrementDegrees, "in (0, 90]");
    require(snappingSource, s.guideRangeMeters > 0.0F, "guideRangeMeters", s.guideRangeMeters, "positive");
    require(snappingSource, s.guideToleranceMeters > 0.0F, "guideToleranceMeters", s.guideToleranceMeters, "positive");
    require(snappingSource, angleWindow >= 0.0F && angleWindow < 90.0F, "the angle catch window", angleWindow, "in [0, 90)");
    require(snappingSource, crossingMinimum > s.guideDirectionToleranceDegrees && crossingMinimum <= 90.0F,
            "the crossing minimum angle", crossingMinimum, "above the direction tolerance and at most 90");
    require(snappingSource, s.effectiveGuideMergeDistanceMeters() >= 0.0F, "the guide merge distance",
            s.effectiveGuideMergeDistanceMeters(), "non-negative");
    require(snappingSource, s.guideCrossingRadiusMeters >= s.guideToleranceMeters, "guideCrossingRadiusMeters",
            s.guideCrossingRadiusMeters, "at least guideToleranceMeters");
    require(snappingSource, s.guideDirectionToleranceDegrees > 0.0F && s.guideDirectionToleranceDegrees < angleWindow,
            "guideDirectionToleranceDegrees", s.guideDirectionToleranceDegrees,
            "positive and below the angle catch window");
    require(snappingSource, s.nearbyGuideDirectionLimit >= 0, "nearbyGuideDirectionLimit",
            static_cast<float>(s.nearbyGuideDirectionLimit), "non-negative");
    require(snappingSource, s.guideCornerLimit >= 0, "guideCornerLimit", static_cast<float>(s.guideCornerLimit), "non-negative");

    require(snappingSource, s.commonLengthRangeMeters >= 0.0F, "commonLengthRangeMeters", s.commonLengthRangeMeters, "non-negative");
    require(snappingSource, s.commonLengthCatchMeters >= 0.0F, "commonLengthCatchMeters", s.commonLengthCatchMeters, "non-negative");
    require(snappingSource, s.commonLengthMergeMeters >= 0.0F && s.commonLengthMergeMeters < s.commonLengthMinimumMeters,
            "commonLengthMergeMeters", s.commonLengthMergeMeters, "non-negative and below commonLengthMinimumMeters");
    const std::string maxCountBound =
        "in [0, " + std::to_string(kMaxCommonLengthCount) + "] (the most common lengths the snap engine reports)";
    require(snappingSource, s.commonLengthMaxCount >= 0 && s.commonLengthMaxCount <= kMaxCommonLengthCount, "commonLengthMaxCount",
            static_cast<float>(s.commonLengthMaxCount), maxCountBound.c_str());
    require(snappingSource, s.commonLengthMinimumMeters >= 0.0F, "commonLengthMinimumMeters", s.commonLengthMinimumMeters,
            "non-negative");
    require(snappingSource, s.lengthIncrementMeters >= 0.0F, "lengthIncrementMeters", s.lengthIncrementMeters, "non-negative");
    require(snappingSource, s.doubleClickSeconds > 0.0F, "doubleClickSeconds", s.doubleClickSeconds, "positive");
    require(snappingSource, s.doubleClickDistancePx >= 0.0F, "doubleClickDistancePx", s.doubleClickDistancePx, "non-negative");

    const float grid = s.drawingSettingsDefaults.rectangleGridSpacingMeters;
    require(snappingSource, grid == 1.0F || grid == 0.5F, "rectangleGridSpacingMeters", grid, "1.0 or 0.5");

    const auto& p = reg.rendering().preview;

    const auto requirePositivePixels = [&](const char* name, float value) {
        require(renderingSource, value > 0.0F, name, value, "positive");
    };
    const auto requireOpacity = [&](const char* name, float value) {
        require(renderingSource, value >= 0.0F && value <= 1.0F, name, value, "in [0, 1]");
    };
    const auto requireDecimals = [&](const char* name, int value) {
        require(renderingSource, value >= 0 && value <= 6, name, static_cast<float>(value), "in [0, 6]");
    };

    requirePositivePixels("guideWidth", p.guideWidth);
    require(renderingSource, p.guideDashPx >= 0.0F, "guideDashPx", p.guideDashPx, "non-negative");
    requirePositivePixels("guideGapPx", p.guideGapPx);
    requireOpacity("guideOpacity", p.guideOpacity);
    requireOpacity("markerOpacity", p.markerOpacity);
    requireDecimals("guideDistanceDecimals", p.guideDistanceDecimals);
    requireDecimals("angleReadoutDecimals", p.angleReadoutDecimals);
    requirePositivePixels("guideLabelPx", p.guideLabelPx);
    requirePositivePixels("rightAngleMarkerPx", p.rightAngleMarkerPx);
    requirePositivePixels("guideCrossingDotPx", p.guideCrossingDotPx);
    requirePositivePixels("cursorRadiusPx", p.cursorRadiusPx);

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

    if (MoodConfig::Get().loaded() && !validateMood(MoodConfig::Get())) {
        LOG_ERROR(Engine, "Mood config validation failed");
        valid = false;
    }

    if (MemoryConfig::Get().loaded() && !validateMemory(MemoryConfig::Get())) {
        LOG_ERROR(Engine, "Memory config validation failed");
        valid = false;
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

} // namespace assets

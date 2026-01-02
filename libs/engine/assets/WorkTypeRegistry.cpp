// Work Type Registry Implementation
// Handles XML parsing with pugixml for work categories and work types.

#include "assets/WorkTypeRegistry.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <algorithm>
#include <filesystem>

namespace engine::assets {

WorkTypeRegistry& WorkTypeRegistry::Get() {
    static WorkTypeRegistry instance;
    return instance;
}

bool WorkTypeRegistry::loadFromFile(const std::string& xmlPath) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(xmlPath.c_str());

    if (!result) {
        LOG_ERROR(Engine, "Failed to load work types XML: %s - %s", xmlPath.c_str(), result.description());
        return false;
    }

    pugi::xml_node root = doc.child("WorkTypes");
    if (!root) {
        LOG_ERROR(Engine, "No WorkTypes root element in: %s", xmlPath.c_str());
        return false;
    }

    bool anyLoaded = false;
    for (pugi::xml_node categoryNode : root.children("Category")) {
        if (parseCategoryFromNode(&categoryNode)) {
            anyLoaded = true;
        }
    }

    if (anyLoaded) {
        buildCapabilityIndex();
        LOG_INFO(Engine, "Loaded %zu work types in %zu categories from %s",
                m_workTypes.size(), m_categories.size(), xmlPath.c_str());
    }

    return anyLoaded;
}

size_t WorkTypeRegistry::loadFromFolder(const std::string& folderPath) {
    namespace fs = std::filesystem;

    if (!fs::exists(folderPath)) {
        LOG_ERROR(Engine, "Work types folder not found: %s", folderPath.c_str());
        return 0;
    }

    size_t loadedBefore = m_workTypes.size();

    try {
        for (const auto& entry : fs::recursive_directory_iterator(folderPath)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".xml") continue;
            // Skip non-work-type files
            if (entry.path().filename().string().find("work-types") == std::string::npos &&
                entry.path().filename().string().find("WorkTypes") == std::string::npos) {
                continue;
            }

            loadFromFile(entry.path().string());
        }
    } catch (const std::exception& e) {
        LOG_ERROR(Engine, "Error scanning work types folder: %s", e.what());
    }

    buildCapabilityIndex();
    return m_workTypes.size() - loadedBefore;
}

void WorkTypeRegistry::clear() {
    m_categories.clear();
    m_workTypes.clear();
    m_byCapability.clear();
}

const WorkCategoryDef* WorkTypeRegistry::getCategory(const std::string& defName) const {
    auto it = m_categories.find(defName);
    if (it != m_categories.end()) {
        return &it->second;
    }
    return nullptr;
}

bool WorkTypeRegistry::hasCategory(const std::string& defName) const {
    return m_categories.find(defName) != m_categories.end();
}

std::vector<const WorkCategoryDef*> WorkTypeRegistry::getAllCategories() const {
    std::vector<const WorkCategoryDef*> result;
    result.reserve(m_categories.size());
    for (const auto& [_, cat] : m_categories) {
        result.push_back(&cat);
    }
    // Sort by tier (ascending = highest priority first)
    std::sort(result.begin(), result.end(),
              [](const WorkCategoryDef* a, const WorkCategoryDef* b) {
                  return a->tier < b->tier;
              });
    return result;
}

std::vector<std::string> WorkTypeRegistry::getCategoryNames() const {
    std::vector<std::string> names;
    names.reserve(m_categories.size());
    for (const auto& [name, _] : m_categories) {
        names.push_back(name);
    }
    return names;
}

const WorkTypeDef* WorkTypeRegistry::getWorkType(const std::string& defName) const {
    auto it = m_workTypes.find(defName);
    if (it != m_workTypes.end()) {
        return &it->second;
    }
    return nullptr;
}

bool WorkTypeRegistry::hasWorkType(const std::string& defName) const {
    return m_workTypes.find(defName) != m_workTypes.end();
}

std::vector<const WorkTypeDef*> WorkTypeRegistry::getWorkTypesInCategory(
    const std::string& categoryDefName) const {
    std::vector<const WorkTypeDef*> result;
    auto catIt = m_categories.find(categoryDefName);
    if (catIt == m_categories.end()) {
        return result;
    }

    for (const auto& workTypeDefName : catIt->second.workTypeDefNames) {
        if (const auto* wt = getWorkType(workTypeDefName)) {
            result.push_back(wt);
        }
    }
    return result;
}

std::vector<const WorkTypeDef*> WorkTypeRegistry::getWorkTypesForCapability(
    const std::string& capabilityName) const {
    auto it = m_byCapability.find(capabilityName);
    if (it != m_byCapability.end()) {
        return it->second;
    }
    return {};
}

std::vector<std::string> WorkTypeRegistry::getWorkTypeNames() const {
    std::vector<std::string> names;
    names.reserve(m_workTypes.size());
    for (const auto& [name, _] : m_workTypes) {
        names.push_back(name);
    }
    return names;
}

size_t WorkTypeRegistry::categoryCount() const {
    return m_categories.size();
}

size_t WorkTypeRegistry::workTypeCount() const {
    return m_workTypes.size();
}

bool WorkTypeRegistry::parseCategoryFromNode(const void* nodePtr) {
    const pugi::xml_node& node = *static_cast<const pugi::xml_node*>(nodePtr);

    WorkCategoryDef category;

    // Required: defName (as attribute)
    pugi::xml_attribute defNameAttr = node.attribute("defName");
    if (!defNameAttr) {
        LOG_WARNING(Engine, "Category missing defName attribute");
        return false;
    }
    category.defName = defNameAttr.as_string();

    if (category.defName.empty()) {
        LOG_WARNING(Engine, "Category has empty defName");
        return false;
    }

    // Required: tier (as attribute)
    category.tier = node.attribute("tier").as_float(5.0F);

    // Optional: canDisable (as attribute, default true)
    category.canDisable = node.attribute("canDisable").as_bool(true);

    // Optional: label
    if (auto labelNode = node.child("label")) {
        category.label = labelNode.text().as_string();
    } else {
        category.label = category.defName;
    }

    // Optional: description
    if (auto descNode = node.child("description")) {
        category.description = descNode.text().as_string();
    }

    // Store category first (need it for work type parsing)
    std::string categoryDefName = category.defName;
    auto [catIt, catInserted] = m_categories.emplace(categoryDefName, std::move(category));
    if (!catInserted) {
        // Category already exists (from another file) - merge work types into it
        catIt = m_categories.find(categoryDefName);
    }

    // Parse work types
    bool anyWorkTypeLoaded = false;
    for (pugi::xml_node workTypeNode : node.children("WorkType")) {
        if (parseWorkTypeFromNode(&workTypeNode, categoryDefName)) {
            anyWorkTypeLoaded = true;
        }
    }

    LOG_DEBUG(Engine, "Loaded category: %s (tier=%.1f, %zu work types)",
             catIt->first.c_str(), catIt->second.tier, catIt->second.workTypeDefNames.size());

    return anyWorkTypeLoaded || catInserted;
}

bool WorkTypeRegistry::parseWorkTypeFromNode(const void* nodePtr, const std::string& categoryDefName) {
    const pugi::xml_node& node = *static_cast<const pugi::xml_node*>(nodePtr);

    WorkTypeDef workType;
    workType.categoryDefName = categoryDefName;

    // Required: defName (as attribute)
    pugi::xml_attribute defNameAttr = node.attribute("defName");
    if (!defNameAttr) {
        LOG_WARNING(Engine, "WorkType missing defName attribute");
        return false;
    }
    workType.defName = defNameAttr.as_string();

    if (workType.defName.empty()) {
        LOG_WARNING(Engine, "WorkType has empty defName");
        return false;
    }

    // Optional: label
    if (auto labelNode = node.child("label")) {
        workType.label = labelNode.text().as_string();
    } else {
        workType.label = workType.defName;
    }

    // Optional: description
    if (auto descNode = node.child("description")) {
        workType.description = descNode.text().as_string();
    }

    // Optional but important: triggerCapability
    if (auto triggerNode = node.child("triggerCapability")) {
        workType.triggerCapability = triggerNode.text().as_string();
    }

    // Optional: targetCapability
    if (auto targetNode = node.child("targetCapability")) {
        workType.targetCapability = targetNode.text().as_string();
    }

    // Optional: skillRequired
    if (auto skillNode = node.child("skillRequired")) {
        workType.skillRequired = skillNode.text().as_string();
    }

    // Optional: minSkillLevel
    if (auto minSkillNode = node.child("minSkillLevel")) {
        workType.minSkillLevel = minSkillNode.text().as_float(0.0F);
    }

    // Optional: taskChain
    if (auto chainNode = node.child("taskChain")) {
        workType.taskChain = chainNode.text().as_string();
    }

    // Optional: filter
    if (auto filterNode = node.child("filter")) {
        workType.filter = parseFilter(&filterNode);
    }

    // Store work type
    auto [it, inserted] = m_workTypes.emplace(workType.defName, std::move(workType));
    if (!inserted) {
        LOG_WARNING(Engine, "Duplicate work type defName: %s (ignoring)", workType.defName.c_str());
        return false;
    }

    // Add to category's work type list
    auto catIt = m_categories.find(categoryDefName);
    if (catIt != m_categories.end()) {
        catIt->second.workTypeDefNames.push_back(it->first);
    }

    LOG_DEBUG(Engine, "Loaded work type: %s (trigger=%s)",
             it->first.c_str(), it->second.triggerCapability.c_str());

    return true;
}

WorkTypeFilter WorkTypeRegistry::parseFilter(const void* nodePtr) {
    const pugi::xml_node& node = *static_cast<const pugi::xml_node*>(nodePtr);

    WorkTypeFilter filter;

    if (auto egNode = node.child("entityGroup")) {
        filter.entityGroup = egNode.text().as_string();
    }

    if (auto liNode = node.child("looseItem")) {
        filter.looseItem = liNode.text().as_bool();
    }

    if (auto indoorNode = node.child("indoor")) {
        filter.indoor = indoorNode.text().as_bool();
    }

    if (auto nbrNode = node.child("neededByRecipe")) {
        filter.neededByRecipe = nbrNode.text().as_bool();
    }

    if (auto nbbNode = node.child("neededByBlueprint")) {
        filter.neededByBlueprint = nbbNode.text().as_bool();
    }

    if (auto stNode = node.child("stationType")) {
        filter.stationType = stNode.text().as_string();
    }

    if (auto hptNode = node.child("hasPlacementTarget")) {
        filter.hasPlacementTarget = hptNode.text().as_bool();
    }

    return filter;
}

void WorkTypeRegistry::buildCapabilityIndex() {
    m_byCapability.clear();

    for (const auto& [_, workType] : m_workTypes) {
        if (!workType.triggerCapability.empty()) {
            m_byCapability[workType.triggerCapability].push_back(&workType);
        }
    }
}

} // namespace engine::assets

// Task Chain Registry Implementation
// Handles XML parsing with pugixml for task chain definitions.

#include "assets/TaskChainRegistry.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <algorithm>

namespace engine::assets {

TaskChainRegistry& TaskChainRegistry::Get() {
    static TaskChainRegistry instance;
    return instance;
}

bool TaskChainRegistry::loadFromFile(const std::string& xmlPath) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(xmlPath.c_str());

    if (!result) {
        LOG_ERROR(Engine, "Failed to load task chains XML: %s - %s", xmlPath.c_str(), result.description());
        return false;
    }

    pugi::xml_node root = doc.child("TaskChains");
    if (!root) {
        LOG_ERROR(Engine, "No TaskChains root element in: %s", xmlPath.c_str());
        return false;
    }

    bool anyLoaded = false;
    for (pugi::xml_node chainNode : root.children("Chain")) {
        if (parseChainFromNode(&chainNode)) {
            anyLoaded = true;
        }
    }

    if (anyLoaded) {
        LOG_INFO(Engine, "Loaded %zu task chains from %s", m_chains.size(), xmlPath.c_str());
    }

    return anyLoaded;
}

void TaskChainRegistry::clear() {
    m_chains.clear();
}

const TaskChainDef* TaskChainRegistry::getChain(const std::string& defName) const {
    auto it = m_chains.find(defName);
    if (it != m_chains.end()) {
        return &it->second;
    }
    return nullptr;
}

bool TaskChainRegistry::hasChain(const std::string& defName) const {
    return m_chains.find(defName) != m_chains.end();
}

std::vector<std::string> TaskChainRegistry::getChainNames() const {
    std::vector<std::string> names;
    names.reserve(m_chains.size());
    for (const auto& [name, _] : m_chains) {
        names.push_back(name);
    }
    return names;
}

const std::unordered_map<std::string, TaskChainDef>& TaskChainRegistry::getAllChains() const {
    return m_chains;
}

size_t TaskChainRegistry::size() const {
    return m_chains.size();
}

bool TaskChainRegistry::parseChainFromNode(const void* nodePtr) {
    const pugi::xml_node& node = *static_cast<const pugi::xml_node*>(nodePtr);

    TaskChainDef chain;

    // Required: defName (as attribute)
    pugi::xml_attribute defNameAttr = node.attribute("defName");
    if (!defNameAttr) {
        LOG_WARNING(Engine, "Chain missing defName attribute");
        return false;
    }
    chain.defName = defNameAttr.as_string();

    if (chain.defName.empty()) {
        LOG_WARNING(Engine, "Chain has empty defName");
        return false;
    }

    // Optional: label
    if (auto labelNode = node.child("label")) {
        chain.label = labelNode.text().as_string();
    } else {
        chain.label = chain.defName;
    }

    // Optional: description
    if (auto descNode = node.child("description")) {
        chain.description = descNode.text().as_string();
    }

    // Required: steps
    pugi::xml_node stepsNode = node.child("steps");
    if (!stepsNode) {
        LOG_WARNING(Engine, "Chain '%s' missing <steps> element", chain.defName.c_str());
        return false;
    }

    for (pugi::xml_node stepNode : stepsNode.children("Step")) {
        ChainStep step;

        // Required: order
        step.order = stepNode.attribute("order").as_uint(0);

        // Required: action
        step.actionDefName = stepNode.attribute("action").as_string();
        if (step.actionDefName.empty()) {
            LOG_WARNING(Engine, "Chain '%s' step %d missing action attribute",
                       chain.defName.c_str(), step.order);
            continue;
        }

        // Required: target
        step.target = stepNode.attribute("target").as_string();

        // Optional: optional (default false)
        step.optional = stepNode.attribute("optional").as_bool(false);

        // Optional: requiresPreviousStep (default true)
        step.requiresPreviousStep = stepNode.attribute("requiresPreviousStep").as_bool(true);

        chain.steps.push_back(step);
    }

    // Sort steps by order
    std::sort(chain.steps.begin(), chain.steps.end(),
              [](const ChainStep& a, const ChainStep& b) { return a.order < b.order; });

    if (chain.steps.empty()) {
        LOG_WARNING(Engine, "Chain '%s' has no valid steps", chain.defName.c_str());
        return false;
    }

    // Store chain
    auto [it, inserted] = m_chains.emplace(chain.defName, std::move(chain));
    if (!inserted) {
        LOG_WARNING(Engine, "Duplicate chain defName: %s (ignoring)", chain.defName.c_str());
        return false;
    }

    LOG_DEBUG(Engine, "Loaded task chain: %s (%zu steps)", it->first.c_str(), it->second.steps.size());
    return true;
}

} // namespace engine::assets

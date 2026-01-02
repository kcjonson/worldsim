// Action Type Registry Implementation
// Handles XML parsing with pugixml for action type definitions.

#include "assets/ActionTypeRegistry.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <sstream>

namespace engine::assets {

ActionTypeRegistry& ActionTypeRegistry::Get() {
    static ActionTypeRegistry instance;
    return instance;
}

bool ActionTypeRegistry::loadFromFile(const std::string& xmlPath) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(xmlPath.c_str());

    if (!result) {
        LOG_ERROR(Engine, "Failed to load action types XML: %s - %s", xmlPath.c_str(), result.description());
        return false;
    }

    pugi::xml_node root = doc.child("ActionTypes");
    if (!root) {
        LOG_ERROR(Engine, "No ActionTypes root element in: %s", xmlPath.c_str());
        return false;
    }

    bool anyLoaded = false;
    for (pugi::xml_node actionNode : root.children("Action")) {
        if (parseActionFromNode(&actionNode)) {
            anyLoaded = true;
        }
    }

    if (anyLoaded) {
        LOG_INFO(Engine, "Loaded %zu action types from %s", m_actions.size(), xmlPath.c_str());
    }

    return anyLoaded;
}

void ActionTypeRegistry::clear() {
    m_actions.clear();
}

const ActionTypeDef* ActionTypeRegistry::getAction(const std::string& defName) const {
    auto it = m_actions.find(defName);
    if (it != m_actions.end()) {
        return &it->second;
    }
    return nullptr;
}

bool ActionTypeRegistry::hasAction(const std::string& defName) const {
    return m_actions.find(defName) != m_actions.end();
}

bool ActionTypeRegistry::actionNeedsHands(const std::string& defName) const {
    auto it = m_actions.find(defName);
    if (it != m_actions.end()) {
        return it->second.needsHands;
    }
    return false;
}

std::vector<std::string> ActionTypeRegistry::getActionNames() const {
    std::vector<std::string> names;
    names.reserve(m_actions.size());
    for (const auto& [name, _] : m_actions) {
        names.push_back(name);
    }
    return names;
}

std::string ActionTypeRegistry::getAvailableActionsString() const {
    std::ostringstream oss;
    bool first = true;
    for (const auto& [name, _] : m_actions) {
        if (!first) {
            oss << ", ";
        }
        oss << name;
        first = false;
    }
    return oss.str();
}

size_t ActionTypeRegistry::size() const {
    return m_actions.size();
}

bool ActionTypeRegistry::parseActionFromNode(const void* nodePtr) {
    const pugi::xml_node& node = *static_cast<const pugi::xml_node*>(nodePtr);

    ActionTypeDef action;

    // Required: defName (as attribute)
    pugi::xml_attribute defNameAttr = node.attribute("defName");
    if (!defNameAttr) {
        LOG_WARNING(Engine, "Action missing defName attribute");
        return false;
    }
    action.defName = defNameAttr.as_string();

    if (action.defName.empty()) {
        LOG_WARNING(Engine, "Action has empty defName");
        return false;
    }

    // Required: needsHands (as attribute)
    pugi::xml_attribute needsHandsAttr = node.attribute("needsHands");
    if (needsHandsAttr) {
        action.needsHands = needsHandsAttr.as_bool(false);
    }

    // Optional: description (as child element)
    if (auto descNode = node.child("description")) {
        action.description = descNode.text().as_string();
    }

    // Store action
    auto [it, inserted] = m_actions.emplace(action.defName, std::move(action));
    if (!inserted) {
        LOG_WARNING(Engine, "Duplicate action defName: %s (ignoring)", action.defName.c_str());
        return false;
    }

    LOG_DEBUG(Engine, "Loaded action type: %s (needsHands=%s)",
              it->first.c_str(),
              it->second.needsHands ? "true" : "false");
    return true;
}

} // namespace engine::assets

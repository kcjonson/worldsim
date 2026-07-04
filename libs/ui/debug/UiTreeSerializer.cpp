#include "debug/UiTreeSerializer.h"

namespace UI {

	Foundation::Rect uiElementBounds(const IComponent& element) {
		const Foundation::Vec2 position = element.getPosition();
		return {position.x, position.y, element.getWidth(), element.getHeight()};
	}

	const std::vector<IComponent*>* uiElementChildren(const IComponent& element) {
		const auto* component = dynamic_cast<const Component*>(&element);
		return component != nullptr ? &component->getChildren() : nullptr;
	}

	nlohmann::json serializeUiElement(const IComponent& element) {
		nlohmann::json out;
		const char*	   id = element.debugId();
		out["id"] = id != nullptr ? nlohmann::json(id) : nlohmann::json(nullptr);
		out["type"] = element.debugTypeName();
		const Foundation::Rect bounds = uiElementBounds(element);
		out["bounds"] = {{"x", bounds.x}, {"y", bounds.y}, {"w", bounds.width}, {"h", bounds.height}};
		out["margin"] = element.margin;
		out["zIndex"] = element.zIndex;
		out["visible"] = element.visible;

		nlohmann::json children = nlohmann::json::array();
		if (const auto* childList = uiElementChildren(element)) {
			for (const auto* child : *childList) {
				children.push_back(serializeUiElement(*child));
			}
		}
		out["children"] = std::move(children);
		return out;
	}

	nlohmann::json serializeUiTree(const std::vector<const IComponent*>& roots, Foundation::Vec2 viewportSize) {
		nlohmann::json out;
		out["viewport"] = {{"width", viewportSize.x}, {"height", viewportSize.y}};
		nlohmann::json rootsJson = nlohmann::json::array();
		for (const auto* root : roots) {
			if (root != nullptr) {
				rootsJson.push_back(serializeUiElement(*root));
			}
		}
		out["roots"] = std::move(rootsJson);
		return out;
	}

	std::string serializeUiTreeJson(const std::vector<const IComponent*>& roots, Foundation::Vec2 viewportSize) {
		// replace, not throw: a stray non-UTF-8 debugId must not abort the frame's
		// update (the drain swallows the exception and the endpoint times out).
		return serializeUiTree(roots, viewportSize).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	}

} // namespace UI

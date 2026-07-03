#pragma once

#include "component/Component.h"

#include <math/Types.h>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

// UI-tree snapshot for /api/ui/tree.
//
// Walks live IComponent roots on the main thread (the app-side state drain
// calls this while the HTTP thread parks in DebugServer::requestState) and
// emits one JSON node per element: id, type, bounds, margin, zIndex, visible,
// children. Bounds are the margin box (getPosition + getWidth/getHeight,
// which bake margin in); margin is emitted separately so consumers can derive
// content bounds. The snapshot root carries the logical viewport size.

namespace UI {

	// Reported bounds of an element: margin-box origin + margin-inclusive size.
	// Shared by the serializer and LayoutLint so both agree on geometry.
	[[nodiscard]] Foundation::Rect uiElementBounds(const IComponent& element);

	// Children of an element, or nullptr for leaves. Only Component subclasses
	// carry children; shapes and other direct IComponent implementers are leaves.
	[[nodiscard]] const std::vector<IComponent*>* uiElementChildren(const IComponent& element);

	[[nodiscard]] nlohmann::json serializeUiElement(const IComponent& element);

	[[nodiscard]] nlohmann::json serializeUiTree(const std::vector<const IComponent*>& roots, Foundation::Vec2 viewportSize);

	[[nodiscard]] std::string serializeUiTreeJson(const std::vector<const IComponent*>& roots, Foundation::Vec2 viewportSize);

} // namespace UI

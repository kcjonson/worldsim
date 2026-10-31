#pragma once

#include "component/Component.h"

#include <graphics/Rect.h>
#include <math/Types.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

// Layout linter for /api/ui/lint and unit tests.
//
// One implementation, two entry points: lintUiTree returns a structured
// LintResult for direct C++ assertions; lintUiTreeJson wraps the same run for
// the HTTP endpoint. Checks the invariants normal HTML flow gives for free:
//   1. no two visible siblings overlap unless their zIndex differs
//   2. every visible child sits inside its parent's bounds (0.5px epsilon)
//   3. nothing lands outside the viewport
//   4. no visible element has zero or negative size
// Roots are treated as siblings of each other. Invisible elements are skipped
// along with their whole subtree. Bounds come from uiElementBounds (margin box).

namespace UI {

	enum class LintRule : std::uint8_t {
		SiblingOverlap,
		ChildOutsideParent,
		OutsideViewport,
		ZeroOrNegativeSize,
		// Reserved: sibling gaps match the container's declared gap. Not
		// implementable until LayoutContainer grows a gap property (engine work).
		SiblingGapMismatch,
	};

	[[nodiscard]] const char* lintRuleName(LintRule rule);

	struct LintViolation {
		LintRule		 rule;
		std::string		 path; // element path from root, ids when present: "vertical_layout/btn_two"
		Foundation::Rect bounds;
		std::string		 otherPath; // overlap: the sibling; containment: the parent or "viewport"
		Foundation::Rect otherBounds;
	};

	struct LintResult {
		std::vector<LintViolation> violations;

		[[nodiscard]] bool clean() const { return violations.empty(); }
	};

	[[nodiscard]] LintResult lintUiTree(const std::vector<const IComponent*>& roots, Foundation::Vec2 viewportSize);

	[[nodiscard]] nlohmann::json lintResultToJson(const LintResult& result);

	[[nodiscard]] std::string lintUiTreeJson(const std::vector<const IComponent*>& roots, Foundation::Vec2 viewportSize);

} // namespace UI

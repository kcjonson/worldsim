#include "debug/LayoutLint.h"

#include "debug/UiTreeSerializer.h"

#include <algorithm>

namespace UI {

	namespace {

		constexpr float kEpsilon = 0.5F;

		std::string pathSegment(const IComponent& element, size_t index) {
			const char* id = element.debugId();
			if (id != nullptr && id[0] != '\0') {
				return id;
			}
			return std::string(element.debugTypeName()) + "[" + std::to_string(index) + "]";
		}

		std::string joinPath(const std::string& parentPath, const std::string& segment) {
			return parentPath.empty() ? segment : parentPath + "/" + segment;
		}

		bool containsWithEpsilon(const Foundation::Rect& outer, const Foundation::Rect& inner) {
			return inner.x >= outer.x - kEpsilon && inner.y >= outer.y - kEpsilon &&
				   inner.right() <= outer.right() + kEpsilon && inner.bottom() <= outer.bottom() + kEpsilon;
		}

		// Touching edges are not overlap; the intersection must be deeper than
		// epsilon on both axes.
		bool overlapsBeyondEpsilon(const Foundation::Rect& a, const Foundation::Rect& b) {
			const float overlapWidth = std::min(a.right(), b.right()) - std::max(a.x, b.x);
			const float overlapHeight = std::min(a.bottom(), b.bottom()) - std::max(a.y, b.y);
			return overlapWidth > kEpsilon && overlapHeight > kEpsilon;
		}

		struct SiblingEntry {
			const IComponent* element;
			std::string		  path;
			Foundation::Rect  bounds;
		};

		// Lints one sibling group, then recurses into each visible member.
		// parentPath/parentBounds are null for the root group (roots have no
		// containment check beyond the viewport).
		void lintSiblings(
			const std::vector<const IComponent*>& siblings,
			const std::string&					  parentPath,
			const Foundation::Rect*				  parentBounds,
			const Foundation::Rect&				  viewportRect,
			LintResult&							  result
		) {
			std::vector<SiblingEntry> visible;
			visible.reserve(siblings.size());

			for (size_t i = 0; i < siblings.size(); ++i) {
				const IComponent* element = siblings[i];
				if (element == nullptr || !element->visible) {
					continue;
				}
				const Foundation::Rect bounds = uiElementBounds(*element);
				const std::string	   path = joinPath(parentPath, pathSegment(*element, i));

				if (bounds.width <= 0.0F || bounds.height <= 0.0F) {
					result.violations.push_back({LintRule::ZeroOrNegativeSize, path, bounds, "", {}});
				}
				if (parentBounds != nullptr && !containsWithEpsilon(*parentBounds, bounds)) {
					result.violations.push_back({LintRule::ChildOutsideParent, path, bounds, parentPath, *parentBounds});
				}
				if (!containsWithEpsilon(viewportRect, bounds)) {
					result.violations.push_back({LintRule::OutsideViewport, path, bounds, "viewport", viewportRect});
				}

				visible.push_back({element, path, bounds});
			}

			for (size_t i = 0; i < visible.size(); ++i) {
				for (size_t j = i + 1; j < visible.size(); ++j) {
					if (visible[i].element->zIndex == visible[j].element->zIndex &&
						overlapsBeyondEpsilon(visible[i].bounds, visible[j].bounds)) {
						result.violations.push_back(
							{LintRule::SiblingOverlap, visible[i].path, visible[i].bounds, visible[j].path, visible[j].bounds}
						);
					}
				}
			}

			for (const auto& entry : visible) {
				if (const auto* childList = uiElementChildren(*entry.element)) {
					const std::vector<const IComponent*> children(childList->begin(), childList->end());
					lintSiblings(children, entry.path, &entry.bounds, viewportRect, result);
				}
			}
		}

	} // namespace

	const char* lintRuleName(LintRule rule) {
		switch (rule) {
			case LintRule::SiblingOverlap:
				return "sibling-overlap";
			case LintRule::ChildOutsideParent:
				return "child-outside-parent";
			case LintRule::OutsideViewport:
				return "outside-viewport";
			case LintRule::ZeroOrNegativeSize:
				return "zero-or-negative-size";
			case LintRule::SiblingGapMismatch:
				return "sibling-gap-mismatch";
		}
		return "unknown";
	}

	LintResult lintUiTree(const std::vector<const IComponent*>& roots, Foundation::Vec2 viewportSize) {
		LintResult result;
		const Foundation::Rect viewportRect{0.0F, 0.0F, viewportSize.x, viewportSize.y};
		lintSiblings(roots, "", nullptr, viewportRect, result);
		return result;
	}

	nlohmann::json lintResultToJson(const LintResult& result) {
		nlohmann::json violations = nlohmann::json::array();
		for (const auto& violation : result.violations) {
			nlohmann::json entry;
			entry["rule"] = lintRuleName(violation.rule);
			entry["path"] = violation.path;
			entry["bounds"] = {
				{"x", violation.bounds.x}, {"y", violation.bounds.y}, {"w", violation.bounds.width}, {"h", violation.bounds.height}
			};
			if (!violation.otherPath.empty()) {
				entry["otherPath"] = violation.otherPath;
				entry["otherBounds"] = {
					{"x", violation.otherBounds.x},
					{"y", violation.otherBounds.y},
					{"w", violation.otherBounds.width},
					{"h", violation.otherBounds.height}
				};
			}
			violations.push_back(std::move(entry));
		}
		nlohmann::json out;
		out["count"] = result.violations.size();
		out["violations"] = std::move(violations);
		return out;
	}

	std::string lintUiTreeJson(const std::vector<const IComponent*>& roots, Foundation::Vec2 viewportSize) {
		// replace, not throw: a stray non-UTF-8 debugId must not abort the frame's
		// update (the drain swallows the exception and the endpoint times out).
		return lintResultToJson(lintUiTree(roots, viewportSize)).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	}

} // namespace UI

#pragma once

#include "component/component.h"

namespace UI {

// Container - Pure organizational component with no visual representation
// Used for grouping and organizing other components, like a div in HTML.
//
// Usage:
//   auto container = Container{};
//   container.AddChild(Rectangle{...});
//   container.AddChild(Button{...});

class Container : public Component {
  public:
	Container() = default;

	// Container has no visual representation of its own,
	// but it propagates Render() to its children (inherited from Component)
};

} // namespace UI

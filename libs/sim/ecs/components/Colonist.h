#pragma once

#include <string>

namespace ecs {

/// Tag component marking an entity as a colonist
struct Colonist {
    std::string name;
};

}  // namespace ecs

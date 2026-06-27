#pragma once

#include <stdexcept>
#include <string>

namespace worldgen {

// Thrown by a generation stage (or the pipeline runner) when a precondition or
// invariant that would otherwise be a debug-only assert is violated in a way
// that must not silently produce a corrupt world in release builds. The runner
// catches it, records what() as the failure reason, and sets state to Failed.
struct GenerationError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

} // namespace worldgen

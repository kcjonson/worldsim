#pragma once

#include "TermSignature.h"

#include <vector>

namespace conditions {

	/// The terms mood.md's Conditions table names, with their types. Every vocabulary starts with these.
	[[nodiscard]] std::vector<TermSignature> knownTerms();

} // namespace conditions

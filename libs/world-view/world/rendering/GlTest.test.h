#pragma once

// Shared by the GL test fixtures (world-view-tests and assets-tests). Without a GL
// context a test skips, so a dev box or the Windows job stays green; with
// WORLDSIM_REQUIRE_GL set (the Linux CI job, which runs under Xvfb and Mesa llvmpipe)
// the same test fails with the call that didn't come up, so a broken software GL
// can't turn the whole GL suite into silent skips.

#include <gtest/gtest.h>

#include <cstdlib>

namespace worldsim::test {

	inline bool glRequired() {
		const char* value = std::getenv("WORLDSIM_REQUIRE_GL");
		return value != nullptr && value[0] != '\0';
	}

} // namespace worldsim::test

// `fixture` has `bool ok` and `const char* failure` (what failed, set when ok is false).
#define SKIP_OR_FAIL_WITHOUT_GL(fixture)                                               \
	do {                                                                               \
		if (!(fixture).ok) {                                                           \
			if (::worldsim::test::glRequired()) {                                      \
				FAIL() << "WORLDSIM_REQUIRE_GL is set but no GL context came up: "     \
					   << (fixture).failure;                                           \
			}                                                                          \
			GTEST_SKIP() << "No GL context (" << (fixture).failure << ")";             \
		}                                                                              \
	} while (false)

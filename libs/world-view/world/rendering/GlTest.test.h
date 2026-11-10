#pragma once

// Shared by the GL test fixtures (world-view-tests and assets-tests). Without a GL
// context a test skips, so a dev box or the Windows job stays green; with
// WORLDSIM_REQUIRE_GL set (the Linux CI job, which runs under Xvfb and Mesa llvmpipe)
// the same test fails with the call that didn't come up, so a broken software GL
// can't turn the whole GL suite into silent skips.

// GLFW's functions only; each test includes GLEW before any GL header.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

namespace worldsim::test {

	inline bool glRequired() {
		const char* value = std::getenv("WORLDSIM_REQUIRE_GL");
		return value != nullptr && value[0] != '\0';
	}

	/// GLFW's last error, for a fixture's failure message.
	inline std::string& glfwLastError() {
		static std::string description;
		return description;
	}

	/// Starts GLFW once for the process and leaves it running until exit: on X11 (Xvfb and Mesa
	/// on CI) glfwInit right after a glfwTerminate fails, so fixtures that terminated per test lost
	/// every other test. Empty when GLFW is up, otherwise why it isn't.
	inline const std::string& startGlfw() {
		static const std::string failure = [] {
			glfwSetErrorCallback([](int, const char* description) { glfwLastError() = description; });
			return glfwInit() == GLFW_TRUE ? std::string() : "glfwInit failed: " + glfwLastError();
		}();
		return failure;
	}

} // namespace worldsim::test

// `fixture` has `bool ok` and `std::string failure` (what failed, set when ok is false).
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

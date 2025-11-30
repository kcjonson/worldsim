add_library(foundation
    metrics/PerformanceMetrics.cpp
    debug/DebugServer.cpp
    utils/Log.cpp
    utils/ResourcePath.cpp
    utils/Utf8.cpp
)

target_include_directories(foundation
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(foundation PUBLIC cxx_std_20)

# Define DEVELOPMENT_BUILD for Debug and RelWithDebInfo builds
# This enables Debug, Info, and Warning log levels
if(CMAKE_BUILD_TYPE MATCHES "Debug|RelWithDebInfo")
    target_compile_definitions(foundation PUBLIC DEVELOPMENT_BUILD)
endif()

# Link dependencies
target_link_libraries(foundation
    PUBLIC
        glm::glm
        httplib::httplib
        OpenGL::GL  # DebugServer uses glReadPixels and glGetIntegerv
)

# Link CoreFoundation on macOS (required by httplib for DNS resolution)
if(APPLE)
    target_link_libraries(foundation PUBLIC "-framework CoreFoundation")
endif()

# Tests
if(BUILD_TESTING)
    # Discover test files using naming pattern
    file(GLOB_RECURSE FOUNDATION_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.test.cpp"
    )

    # Discover benchmark files using naming pattern
    file(GLOB_RECURSE FOUNDATION_BENCHMARK_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.bench.cpp"
    )

    # Create test executable if tests found
    if(FOUNDATION_TEST_SOURCES)
        find_package(GTest CONFIG REQUIRED)
        add_executable(foundation-tests ${FOUNDATION_TEST_SOURCES})
        target_link_libraries(foundation-tests
            PRIVATE
                foundation
                GTest::gtest
                GTest::gtest_main
        )
        # Debug builds with no optimization for accurate debugging
        target_compile_options(foundation-tests PRIVATE -O0 -g)

        # Register with CTest
        add_test(NAME foundation-tests COMMAND foundation-tests)
    endif()

    # Create benchmark executable if benchmarks found
    if(FOUNDATION_BENCHMARK_SOURCES)
        find_package(benchmark CONFIG REQUIRED)
        add_executable(foundation-benchmarks ${FOUNDATION_BENCHMARK_SOURCES})
        target_link_libraries(foundation-benchmarks
            PRIVATE
                foundation
                benchmark::benchmark
                benchmark::benchmark_main
        )
        # Release builds with full optimization for accurate performance measurement
        target_compile_options(foundation-benchmarks PRIVATE -O3)

        # Register with CTest (benchmarks can be run as tests too)
        add_test(NAME foundation-benchmarks COMMAND foundation-benchmarks)
    endif()
endif()

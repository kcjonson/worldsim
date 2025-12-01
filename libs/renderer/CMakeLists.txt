add_library(renderer
    primitives/Primitives.cpp
    primitives/BatchRenderer.cpp
    metrics/MetricsCollector.cpp
    vector/Tessellator.cpp
    vector/Bezier.cpp
    shader/Shader.cpp
    shader/ShaderLoader.cpp
    CoordinateSystem/CoordinateSystem.cpp
)

target_include_directories(renderer
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(renderer PUBLIC cxx_std_20)

# Link dependencies
target_link_libraries(renderer
    PUBLIC
        foundation
        OpenGL::GL
        GLEW::GLEW
        glfw
        glm::glm
)

# Copy shaders to build directory
add_custom_command(TARGET renderer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders
    ${CMAKE_BINARY_DIR}/shaders
    COMMENT "Copying shader files to build directory"
)

# Tests
if(BUILD_TESTING)
    # Discover test files using naming pattern
    file(GLOB_RECURSE RENDERER_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.test.cpp"
    )

    # Discover benchmark files using naming pattern
    file(GLOB_RECURSE RENDERER_BENCHMARK_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.bench.cpp"
    )

    # Create test executable if tests found
    if(RENDERER_TEST_SOURCES)
        find_package(GTest CONFIG REQUIRED)
        add_executable(renderer-tests ${RENDERER_TEST_SOURCES})
        target_link_libraries(renderer-tests
            PRIVATE
                renderer
                GTest::gtest
                GTest::gtest_main
        )
        target_compile_options(renderer-tests PRIVATE -O0 -g)
        add_test(NAME renderer-tests COMMAND renderer-tests)
    endif()

    # Create benchmark executable if benchmarks found
    if(RENDERER_BENCHMARK_SOURCES)
        find_package(benchmark CONFIG REQUIRED)
        add_executable(renderer-benchmarks ${RENDERER_BENCHMARK_SOURCES})
        target_link_libraries(renderer-benchmarks
            PRIVATE
                renderer
                benchmark::benchmark
                benchmark::benchmark_main
        )
        target_compile_options(renderer-benchmarks PRIVATE -O3)
        add_test(NAME renderer-benchmarks COMMAND renderer-benchmarks)
    endif()
endif()

find_package(NanoSVG CONFIG REQUIRED)

add_library(renderer
    primitives/Primitives.cpp
    primitives/BatchRenderer.cpp
    metrics/MetricsCollector.cpp
    metrics/GPUTimer.cpp
    vector/Tessellator.cpp
    vector/Bezier.cpp
    vector/SVGLoader.cpp
    shader/Shader.cpp
    shader/ShaderLoader.cpp
    shader/ShaderPreprocessor.cpp
    resources/RenderToTexture.cpp
    resources/TileTextureAtlas.cpp
    resources/TilePatternBaker.cpp
    resources/TileAtlasBuilder.cpp
    CoordinateSystem/CoordinateSystem.cpp
)

target_include_directories(renderer
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        # Include ui headers for FontRenderer (used in Primitives::drawText)
        # This is include-only, not a link dependency, to avoid circular deps
        ${CMAKE_CURRENT_SOURCE_DIR}/../ui
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
    PRIVATE
        # NanoSVG is used only for internal SVG parsing in SVGLoader.cpp.
        # No NanoSVG types are exposed in public headers, so PRIVATE linkage is correct.
        NanoSVG::nanosvg
)

# Copy shaders to build and runtime directories whenever shader sources change.
file(GLOB_RECURSE RENDERER_SHADER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*"
)

set(RENDERER_SHADERS_STAMP "${CMAKE_BINARY_DIR}/shaders/.renderer_shaders_copied")

add_custom_command(
    OUTPUT ${RENDERER_SHADERS_STAMP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/apps/world-sim/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders
        ${CMAKE_BINARY_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders
        ${CMAKE_BINARY_DIR}/apps/world-sim/shaders
    COMMAND ${CMAKE_COMMAND} -E touch ${RENDERER_SHADERS_STAMP}
    DEPENDS ${RENDERER_SHADER_SOURCES}
    COMMENT "Syncing renderer shaders to build and runtime directories"
    VERBATIM
)

add_custom_target(renderer-shaders ALL DEPENDS ${RENDERER_SHADERS_STAMP})

add_dependencies(renderer renderer-shaders)

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

add_library(planet-view
    PlanetMesh.cpp
    PlanetColorizer.cpp
    OrbitCamera.cpp
    PlanetRenderer.cpp
)

target_include_directories(planet-view
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(planet-view PUBLIC cxx_std_20)

target_link_libraries(planet-view
    PUBLIC
        foundation
        renderer
        world
        OpenGL::GL
        GLEW::GLEW
        glm::glm
)

# ── Shader deployment ──
# Mirror the renderer's pattern: copy planet-view shaders to the shared build
# shaders/ directory so both ui-sandbox and world-sim find them at runtime.
file(GLOB_RECURSE PLANET_VIEW_SHADER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*"
)

set(PLANET_VIEW_SHADERS_STAMP "${CMAKE_BINARY_DIR}/shaders/.planet_view_shaders_copied")

add_custom_command(
    OUTPUT ${PLANET_VIEW_SHADERS_STAMP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders
        ${CMAKE_BINARY_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E touch ${PLANET_VIEW_SHADERS_STAMP}
    DEPENDS ${PLANET_VIEW_SHADER_SOURCES}
    COMMENT "Syncing planet-view shaders to build shaders directory"
    VERBATIM
)

add_custom_target(planet-view-shaders ALL DEPENDS ${PLANET_VIEW_SHADERS_STAMP})
add_dependencies(planet-view planet-view-shaders)

# ── Tests ──
if(BUILD_TESTING)
    file(GLOB_RECURSE PLANET_VIEW_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.test.cpp"
    )

    file(GLOB_RECURSE PLANET_VIEW_BENCHMARK_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.bench.cpp"
    )

    if(PLANET_VIEW_TEST_SOURCES)
        find_package(GTest CONFIG REQUIRED)
        add_executable(planet-view-tests ${PLANET_VIEW_TEST_SOURCES})
        target_link_libraries(planet-view-tests
            PRIVATE
                planet-view
                GTest::gtest
                GTest::gtest_main
        )
        if(MSVC)
            target_compile_options(planet-view-tests PRIVATE /Od /Zi)
        else()
            target_compile_options(planet-view-tests PRIVATE -O0 -g)
        endif()
        add_test(NAME planet-view-tests COMMAND planet-view-tests)
    endif()

    if(PLANET_VIEW_BENCHMARK_SOURCES)
        find_package(benchmark CONFIG REQUIRED)
        add_executable(planet-view-benchmarks ${PLANET_VIEW_BENCHMARK_SOURCES})
        target_link_libraries(planet-view-benchmarks
            PRIVATE
                planet-view
                benchmark::benchmark
                benchmark::benchmark_main
        )
        if(MSVC)
            target_compile_options(planet-view-benchmarks PRIVATE /O2)
        else()
            target_compile_options(planet-view-benchmarks PRIVATE -O3)
        endif()
        add_test(NAME planet-view-benchmarks COMMAND planet-view-benchmarks)
    endif()
endif()

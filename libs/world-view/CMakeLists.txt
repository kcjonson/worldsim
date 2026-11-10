add_library(world-view
    world/rendering/ChunkRenderer.cpp
    world/rendering/EntityRenderer.cpp
    world/rendering/BakedChunkRenderer.cpp
    world/rendering/GroundcoverRenderer.cpp
    world/rendering/GroundPairs.cpp
    world/rendering/GroundTileBank.cpp
    world/rendering/GroundTileBaker.cpp
    world/rendering/GroundTileCache.cpp
    world/rendering/GroundTileFilter.cpp
    world/rendering/InstancedEntityRenderer.cpp
    world/rendering/BatchedEntityRenderer.cpp
    world/rendering/TemplateMeshCache.cpp
    world/rendering/DynamicEntityRenderSystem.cpp
)

target_include_directories(world-view
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(world-view PUBLIC cxx_std_20)

# The world's rendering: the ground pass, the entity renderers and the ECS system that
# draws dynamic entities. OpenGL and GLEW are PUBLIC: the chunk renderers' headers hold
# GL buffers.
target_link_libraries(world-view
    PUBLIC
        salal::foundation
        salal::renderer
        world
        assets
        sim
        OpenGL::GL
        GLEW::GLEW
)

# salal's base shader with the game's hooks, assembled at build time and compiled in
# (salal's docs/shader-hooks.md). Declared here once for every executable that starts the
# renderer: each GUI app's AppConfig, asset-cli and the GL test fixtures hand
# base_shader::source() to the renderer, and the groundcover renderer and the grass scene
# fill the hook's block.
salal_base_shader(world-view HOOKS world/rendering/hooks)

# salal's version, part of the ground bake cache key: SalalVersion.h, which only GroundTileCache.cpp
# includes. The step runs on every build, since a sibling checkout (FETCHCONTENT_SOURCE_DIR_SALAL) can
# change under it, and rewrites the header only when the version does (cmake/salal-version.cmake).
find_package(Git)
if(NOT Git_FOUND)
    message(FATAL_ERROR "world-view: git not found. The ground bake cache key needs salal's version, git describe in ${salal_SOURCE_DIR}.")
endif()
set(WORLD_VIEW_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated/world-view")
add_custom_target(world-view-salal-version
    COMMAND ${CMAKE_COMMAND}
        -DGIT=${GIT_EXECUTABLE}
        -DSALAL_DIR=${salal_SOURCE_DIR}
        -DOUTPUT=${WORLD_VIEW_GENERATED_DIR}/SalalVersion.h
        -P ${CMAKE_SOURCE_DIR}/cmake/salal-version.cmake
    BYPRODUCTS ${WORLD_VIEW_GENERATED_DIR}/SalalVersion.h
    COMMENT "Reading salal's version for the ground bake cache key"
    VERBATIM
)
add_dependencies(world-view world-view-salal-version)
target_include_directories(world-view PRIVATE ${WORLD_VIEW_GENERATED_DIR})

# The ground pass (tile.vert, tile.frag, includes/) is the game's own GL, loaded at run
# time. Stage it into the shared build shaders/ directory, as planet-view stages its own.
# salal's base shader and the hooks (world/rendering/hooks/, outside this folder) are
# compiled in, so nothing of theirs is staged.
file(GLOB_RECURSE WORLD_VIEW_SHADER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/world/rendering/shaders/*"
)

set(WORLD_VIEW_SHADERS_STAMP "${CMAKE_BINARY_DIR}/shaders/.world_view_shaders_copied")

add_custom_command(
    OUTPUT ${WORLD_VIEW_SHADERS_STAMP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/world/rendering/shaders
        ${CMAKE_BINARY_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E touch ${WORLD_VIEW_SHADERS_STAMP}
    DEPENDS ${WORLD_VIEW_SHADER_SOURCES}
    COMMENT "Syncing world-view shaders to build shaders directory"
    VERBATIM
)

add_custom_target(world-view-shaders ALL DEPENDS ${WORLD_VIEW_SHADERS_STAMP})
add_dependencies(world-view world-view-shaders)

# Tests
if(BUILD_TESTING)
    file(GLOB_RECURSE WORLD_VIEW_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.test.cpp"
    )

    if(WORLD_VIEW_TEST_SOURCES)
        find_package(GTest CONFIG REQUIRED)
        add_executable(world-view-tests ${WORLD_VIEW_TEST_SOURCES})
        # SurfaceFieldGolden.test.cpp renders land.glsl in a hidden GL window.
        target_link_libraries(world-view-tests
            PRIVATE
                world-view
                glfw
                GTest::gtest
                GTest::gtest_main
        )
        # Where SurfaceFieldGolden reads land.glsl from, whatever the working directory
        # (MSVC's __FILE__ is relative to the build dir, and ctest runs from another).
        target_compile_definitions(world-view-tests PRIVATE
            WORLD_VIEW_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/world/rendering/shaders")
        if(MSVC)
            target_compile_options(world-view-tests PRIVATE /Od)
        else()
            target_compile_options(world-view-tests PRIVATE -O0 -g)
        endif()
        add_test(NAME world-view-tests COMMAND world-view-tests)
        # TIMEOUT is the only hang guard. On the Linux CI job the GL tests run on Mesa
        # llvmpipe: 69 s there with half of them up (2026-11-10). Keep it at least 2.5x
        # the measured wall time.
        set_tests_properties(world-view-tests PROPERTIES TIMEOUT 300)
    endif()
endif()

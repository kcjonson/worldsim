add_library(world-view
    world/rendering/ChunkRenderer.cpp
    world/rendering/EntityRenderer.cpp
    world/rendering/BakedChunkRenderer.cpp
    world/rendering/GroundcoverRenderer.cpp
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
        if(MSVC)
            target_compile_options(world-view-tests PRIVATE /Od)
        else()
            target_compile_options(world-view-tests PRIVATE -O0 -g)
        endif()
        add_test(NAME world-view-tests COMMAND world-view-tests)
    endif()
endif()

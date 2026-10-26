add_library(engine
    application/Application.cpp
    application/AppLauncher.cpp
    debug/DebugServer.cpp
    graphics/PngEncoder.cpp
    scene/SceneManager.cpp
    input/InputManager.cpp
    clipboard/ClipboardManager.cpp
    world/TileGrid.cpp
    world/chunk/Chunk.cpp
    world/chunk/ChunkManager.cpp
    world/chunk/TilePostProcessor.cpp
    world/chunk/MockWorldSampler.cpp
    world/chunk/GeneratedWorldSampler.cpp
    world/chunk/ApronField.cpp
    world/chunk/SurfaceField.cpp
    world/chunk/TerrainChannelBuilder.cpp
    world/chunk/TerrainDistanceField.cpp
    world/chunk/TerrainPolygonBuilder.cpp
    world/chunk/TerrainPolygonQuery.cpp
    world/rendering/BakedEntityMesh.cpp
    world/rendering/ChunkRenderer.cpp
    world/rendering/EntityRenderer.cpp
    world/rendering/BakedChunkRenderer.cpp
    world/rendering/GroundcoverRenderer.cpp
    world/rendering/InstancedEntityRenderer.cpp
    world/rendering/BatchedEntityRenderer.cpp
    world/rendering/TemplateMeshCache.cpp
    world/rendering/WorldDepthSort.cpp
    ecs/systems/TimeSystem.cpp
    ecs/systems/MovementSystem.cpp
    ecs/systems/PhysicsSystem.cpp
    ecs/systems/DynamicEntityRenderSystem.cpp
    ecs/systems/NeedsDecaySystem.cpp
    ecs/systems/VisionSystem.cpp
    ecs/systems/AIDecisionSystem.cpp
    ecs/systems/ActionSystem.cpp
    ecs/systems/action/NeedActions.cpp
    ecs/systems/action/HarvestActions.cpp
    ecs/systems/action/CraftActions.cpp
    ecs/systems/action/HaulActions.cpp
    ecs/systems/action/BuildActions.cpp
    ecs/systems/StorageGoalSystem.cpp
    ecs/systems/CraftingGoalSystem.cpp
    ecs/systems/BuildGoalSystem.cpp
    ecs/systems/ConstructionSystem.cpp
    ecs/systems/RoomDetectionSystem.cpp
    ecs/systems/CollisionSystem.cpp
    ecs/systems/WallCollisionSystem.cpp
    ecs/systems/StaticRectCollisionSystem.cpp
    ecs/systems/NavigationSystem.cpp
    ecs/spatial/AgentSpatialHash.cpp
    ecs/GoalTaskRegistry.cpp
    ecs/components/MemoryQueries.cpp
    ecs/components/ToiletLocationFinder.cpp
    construction/ConstructionWorld.cpp
    construction/ConstructionValidator.cpp
    construction/OpeningGeometry.cpp
    construction/RoomDetection.cpp
    construction/RoomHitTest.cpp
    construction/SnapEngine.cpp
    nav/NavInputBuilder.cpp
    vision/GeometryIndex.cpp
)

target_include_directories(engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(engine PUBLIC cxx_std_20)

# Link dependencies. cpp-httplib is an implementation detail of debug/DebugServer.cpp
# (DebugServer.h forward-declares httplib::Server): PRIVATE keeps its include dir off
# downstream TUs while static-lib link requirements still propagate to executables.
# OpenGL, GLEW and glfw are PUBLIC: the renderer keeps them private behind its
# headers, and the game's own GL (the chunk and planet passes, the scenes' clear
# pairs, the GLFW window here) reaches them through engine.
target_link_libraries(engine
    PUBLIC
        salal::foundation
        geometry
        salal::renderer
        ui
        assets
        world
        OpenGL::GL
        GLEW::GLEW
        glfw
    PRIVATE
        httplib::httplib
)

# Link CoreFoundation on macOS (required by httplib for DNS resolution)
if(APPLE)
    target_link_libraries(engine PRIVATE "-framework CoreFoundation")
endif()

# timeBeginPeriod/timeEndPeriod for frame-pacing timer resolution
if(WIN32)
    target_link_libraries(engine PRIVATE winmm)
endif()

# The ground pass (tile.vert, tile.frag, includes/) is the engine's, not the
# renderer's. Stage it into the shared build shaders/ directory the way the
# renderer and planet-view stage theirs, so the runtime layout is unchanged.
file(GLOB_RECURSE ENGINE_SHADER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/world/rendering/shaders/*"
)

set(ENGINE_SHADERS_STAMP "${CMAKE_BINARY_DIR}/shaders/.engine_shaders_copied")

add_custom_command(
    OUTPUT ${ENGINE_SHADERS_STAMP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/world/rendering/shaders
        ${CMAKE_BINARY_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E touch ${ENGINE_SHADERS_STAMP}
    DEPENDS ${ENGINE_SHADER_SOURCES}
    COMMENT "Syncing engine shaders to build shaders directory"
    VERBATIM
)

add_custom_target(engine-shaders ALL DEPENDS ${ENGINE_SHADERS_STAMP})
add_dependencies(engine engine-shaders)

# Tests
if(BUILD_TESTING)
    # Discover test files using naming pattern
    file(GLOB_RECURSE ENGINE_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.test.cpp"
    )

    # Discover benchmark files using naming pattern
    file(GLOB_RECURSE ENGINE_BENCHMARK_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.bench.cpp"
    )

    # Create test executable if tests found
    if(ENGINE_TEST_SOURCES)
        find_package(GTest CONFIG REQUIRED)
        add_executable(engine-tests ${ENGINE_TEST_SOURCES})
        target_link_libraries(engine-tests
            PRIVATE
                engine
                GTest::gtest
                GTest::gtest_main
        )
        if(MSVC)
            target_compile_options(engine-tests PRIVATE /Od)
        else()
            target_compile_options(engine-tests PRIVATE -O0 -g)
        endif()
        add_test(NAME engine-tests COMMAND engine-tests)
    endif()

    # Create benchmark executable if benchmarks found
    if(ENGINE_BENCHMARK_SOURCES)
        find_package(benchmark CONFIG REQUIRED)
        add_executable(engine-benchmarks ${ENGINE_BENCHMARK_SOURCES})
        target_link_libraries(engine-benchmarks
            PRIVATE
                engine
                benchmark::benchmark
                benchmark::benchmark_main
        )
        # MSVC: per-config defaults (/O2 in Release); forcing /O2 collides with
        # Debug's /RTC1 (D8016).
        if(NOT MSVC)
            target_compile_options(engine-benchmarks PRIVATE -O3)
        endif()
        add_test(NAME engine-benchmarks COMMAND engine-benchmarks)
    endif()
endif()

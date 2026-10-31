add_library(world
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
)

target_include_directories(world
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(world PUBLIC cxx_std_20)

# The game world: chunks, tiles, terrain and the world camera. No renderer, no ui.
target_link_libraries(world
    PUBLIC
        salal::foundation
        geometry
        worldgen
)

# Tests
if(BUILD_TESTING)
    file(GLOB_RECURSE WORLD_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.test.cpp"
    )

    file(GLOB_RECURSE WORLD_BENCHMARK_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.bench.cpp"
    )

    if(WORLD_TEST_SOURCES)
        find_package(GTest CONFIG REQUIRED)
        add_executable(world-tests ${WORLD_TEST_SOURCES})
        target_link_libraries(world-tests
            PRIVATE
                world
                GTest::gtest
                GTest::gtest_main
        )
        if(MSVC)
            target_compile_options(world-tests PRIVATE /Od)
        else()
            target_compile_options(world-tests PRIVATE -O0 -g)
        endif()
        add_test(NAME world-tests COMMAND world-tests)
    endif()

    if(WORLD_BENCHMARK_SOURCES)
        find_package(benchmark CONFIG REQUIRED)
        add_executable(world-benchmarks ${WORLD_BENCHMARK_SOURCES})
        target_link_libraries(world-benchmarks
            PRIVATE
                world
                benchmark::benchmark
                benchmark::benchmark_main
        )
        # MSVC: per-config defaults (/O2 in Release); forcing /O2 collides with
        # Debug's /RTC1 (D8016).
        if(NOT MSVC)
            target_compile_options(world-benchmarks PRIVATE -O3)
        endif()
        add_test(NAME world-benchmarks COMMAND world-benchmarks)
    endif()
endif()

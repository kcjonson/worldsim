add_library(world
    worldgen/grid/SphereGrid.cpp
    worldgen/data/PlanetParams.cpp
    worldgen/pipeline/PlanetGenerator.cpp
    worldgen/stages/TectonicHistoryStage.cpp
    worldgen/stages/CrustStage.cpp
    worldgen/stages/TerrainStage.cpp
    worldgen/stages/ErosionStage.cpp
    worldgen/tectonics/PlateSim.cpp
    worldgen/tectonics/TectonicHistory.cpp
    worldgen/stages/AtmosphereStage.cpp
    worldgen/stages/DrainageRouting.cpp
    worldgen/stages/PrecipitationStage.cpp
    worldgen/stages/OceanStage.cpp
    worldgen/stages/BiomeStage.cpp
    worldgen/stages/SnowStage.cpp
    worldgen/sampling/SphericalProjection.cpp
    worldgen/sampling/PlanetSampler.cpp
    worldgen/sampling/LandingSite.cpp
    worldgen/io/PlanetIO.cpp
    worldgen/debug/DebugImageExporter.cpp
    worldgen/debug/WorldStats.cpp
)

target_include_directories(world
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(world PUBLIC cxx_std_20)

# Link dependencies
target_link_libraries(world
    PUBLIC
        foundation
)

# Tests
if(BUILD_TESTING)
    # Discover test files using naming pattern
    file(GLOB_RECURSE WORLD_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.test.cpp"
    )

    # Discover benchmark files using naming pattern
    file(GLOB_RECURSE WORLD_BENCHMARK_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.bench.cpp"
    )

    # Create test executable if tests found
    if(WORLD_TEST_SOURCES)
        find_package(GTest CONFIG REQUIRED)
        add_executable(world-tests ${WORLD_TEST_SOURCES})
        target_link_libraries(world-tests
            PRIVATE
                world
                GTest::gtest
                GTest::gtest_main
        )
        if(NOT MSVC)
            target_compile_options(world-tests PRIVATE -O0 -g)
        endif()
        # Two ctest entries against the SAME executable, split by gtest SUITE name:
        # any suite whose name ends in "Heavy" is the heavy bucket. The filters use
        # "*Heavy.*" (Heavy immediately before the Suite.Test separator) so only a
        # suite-name suffix classifies a test, never a "Heavy" substring in a test
        # name. Add the "Heavy" suffix to a test's SUITE to make it heavy; no list to
        # maintain here.
        #
        # A test belongs in a *Heavy suite when it is materially slow in a Debug build:
        # in practice that means it runs the full PlanetGenerator pipeline
        # (runPipeline/runToCompletion) or a full PlateSim history. These dominate the
        # suite at the resolutions used (coarseN ~32-256); cheap tests (tiny n,
        # single-stage, or pure-logic) stay in the fast bucket.
        #
        # Fast-bucket TIMEOUT calibration (2026-06-14):
        #   RelWithDebInfo wall time: ~7s
        #   Debug-CI multiplier: ~9x -> ~63s on CI
        #   Formula: max(300, ceil(7 * 12)) = max(300, 84) = 300
        #   300s gives >4x margin over the worst-case Debug-CI estimate.
        # If the fast wall time grows, rerun and set TIMEOUT = max(300, ceil(newWall * 12)).
        add_test(NAME world-tests COMMAND world-tests --gtest_filter=-*Heavy.*)
        set_tests_properties(world-tests PROPERTIES LABELS "fast" TIMEOUT 300)
        add_test(NAME world-tests-heavy COMMAND world-tests --gtest_filter=*Heavy.*)
        set_tests_properties(world-tests-heavy PROPERTIES LABELS "heavy" TIMEOUT 2400)
    endif()

    # Create benchmark executable if benchmarks found
    if(WORLD_BENCHMARK_SOURCES)
        find_package(benchmark CONFIG REQUIRED)
        add_executable(world-benchmarks ${WORLD_BENCHMARK_SOURCES})
        target_link_libraries(world-benchmarks
            PRIVATE
                world
                benchmark::benchmark
                benchmark::benchmark_main
        )
        # MSVC: rely on per-config defaults (/O2 in Release); forcing /O2 here
        # collides with Debug's /RTC1 (D8016).
        if(NOT MSVC)
            target_compile_options(world-benchmarks PRIVATE -O3)
        endif()
        add_test(NAME world-benchmarks COMMAND world-benchmarks)
    endif()
endif()

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
    worldgen/stages/GlacierStage.cpp
    worldgen/sampling/SphericalProjection.cpp
    worldgen/sampling/PlanetSampler.cpp
    worldgen/sampling/RiverNetwork2D.cpp
    worldgen/sampling/PondNetwork2D.cpp
    worldgen/sampling/SpawnSite.cpp
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
        # Fast/heavy split by gtest SUITE name against the SAME executable: any
        # suite whose name ends in "Heavy" is the heavy bucket. The filters use
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
        # PROCESSORS: even fast-bucket tests run PlanetGenerator pipelines whose
        # internal pool is hardware_concurrency - 1; declaring the footprint keeps
        # parallel ctest from co-scheduling other pool-spawning exes (they starve
        # each other on 4-core CI runners).
        set_tests_properties(world-tests PROPERTIES LABELS "fast" TIMEOUT 300 PROCESSORS 3)

        # The heavy bucket runs as gtest shards so `ctest -j -L heavy` spreads it
        # across cores (one entry serialized ~16 min Linux / ~27 min Windows on CI).
        # gtest distributes filtered tests round-robin by index, so the dominant
        # suites (PlateSimHeavy, TerrainStageHeavy: 13 tests each) split evenly with
        # no suite list to maintain. TIMEOUT is per-shard: worst observed CI shard
        # plus margin (was 2400 for the whole bucket).
        set(WORLD_HEAVY_SHARDS 4)
        math(EXPR _heavy_last_shard "${WORLD_HEAVY_SHARDS} - 1")
        foreach(shard RANGE ${_heavy_last_shard})
            add_test(NAME world-tests-heavy-${shard} COMMAND world-tests --gtest_filter=*Heavy.*)
            set_tests_properties(world-tests-heavy-${shard} PROPERTIES
                LABELS "heavy"
                TIMEOUT 1200
                ENVIRONMENT "GTEST_TOTAL_SHARDS=${WORLD_HEAVY_SHARDS};GTEST_SHARD_INDEX=${shard}")
        endforeach()
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

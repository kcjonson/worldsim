add_library(worldgen
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

target_include_directories(worldgen
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(worldgen PUBLIC cxx_std_20)

# Link dependencies
target_link_libraries(worldgen
    PUBLIC
        salal::foundation
)

# Tests
if(BUILD_TESTING)
    # Discover test files using naming pattern
    file(GLOB_RECURSE WORLDGEN_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.test.cpp"
    )

    # Discover benchmark files using naming pattern
    file(GLOB_RECURSE WORLDGEN_BENCHMARK_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.bench.cpp"
    )

    # Create test executable if tests found
    if(WORLDGEN_TEST_SOURCES)
        find_package(GTest CONFIG REQUIRED)
        add_executable(worldgen-tests ${WORLDGEN_TEST_SOURCES})
        target_link_libraries(worldgen-tests
            PRIVATE
                worldgen
                GTest::gtest
                GTest::gtest_main
        )
        if(NOT MSVC)
            target_compile_options(worldgen-tests PRIVATE -O0 -g)
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
        # Fast-bucket TIMEOUT calibration (2026-10-30, Debug):
        #   local wall time: ~20-25s
        #   Linux CI has run it at 3.6-4.5x local -> ~70-115s on CI
        #   300s gives >2.5x margin over a slow runner.
        # It crept past 300s on Linux CI when each fast test generated its own planet.
        # A fast test that needs a generated world reads its file's shared run
        # (observedRun, sharedStats) instead of running the pipeline itself.
        # If the fast wall time grows, remeasure and keep TIMEOUT at least 2.5x the
        # slow-runner estimate (local * 4.5).
        add_test(NAME worldgen-tests COMMAND worldgen-tests --gtest_filter=-*Heavy.*)
        # PROCESSORS: even fast-bucket tests run PlanetGenerator pipelines whose
        # internal pool is hardware_concurrency - 1; declaring the footprint keeps
        # parallel ctest from co-scheduling other pool-spawning exes (they starve
        # each other on 4-core CI runners).
        set_tests_properties(worldgen-tests PROPERTIES LABELS "fast" TIMEOUT 300 PROCESSORS 3)

        # The heavy bucket runs as gtest shards so `ctest -j -L heavy` spreads it
        # across cores (one entry serialized ~16 min Linux / ~27 min Windows on CI).
        # gtest distributes filtered tests round-robin by index, so the dominant
        # suites (PlateSimHeavy, TerrainStageHeavy: 13 tests each) split evenly with
        # no suite list to maintain. TIMEOUT is per-shard: worst observed CI shard
        # plus margin (was 2400 for the whole bucket).
        set(WORLDGEN_HEAVY_SHARDS 4)
        math(EXPR _heavy_last_shard "${WORLDGEN_HEAVY_SHARDS} - 1")
        foreach(shard RANGE ${_heavy_last_shard})
            add_test(NAME worldgen-tests-heavy-${shard} COMMAND worldgen-tests --gtest_filter=*Heavy.*)
            set_tests_properties(worldgen-tests-heavy-${shard} PROPERTIES
                LABELS "heavy"
                TIMEOUT 1200
                ENVIRONMENT "GTEST_TOTAL_SHARDS=${WORLDGEN_HEAVY_SHARDS};GTEST_SHARD_INDEX=${shard}")
        endforeach()
    endif()

    # Create benchmark executable if benchmarks found
    if(WORLDGEN_BENCHMARK_SOURCES)
        find_package(benchmark CONFIG REQUIRED)
        add_executable(worldgen-benchmarks ${WORLDGEN_BENCHMARK_SOURCES})
        target_link_libraries(worldgen-benchmarks
            PRIVATE
                world
                benchmark::benchmark
                benchmark::benchmark_main
        )
        # MSVC: rely on per-config defaults (/O2 in Release); forcing /O2 here
        # collides with Debug's /RTC1 (D8016).
        if(NOT MSVC)
            target_compile_options(worldgen-benchmarks PRIVATE -O3)
        endif()
        add_test(NAME worldgen-benchmarks COMMAND worldgen-benchmarks)
    endif()
endif()

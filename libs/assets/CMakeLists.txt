add_library(assets
    assets/ActionTypeRegistry.cpp
    assets/AssetBootstrap.cpp
    assets/AssetRegistry.cpp
    assets/AssetRenderer.cpp
    assets/SvgPathNodes.cpp
    assets/MotionEval.cpp
    assets/AssetValidator.cpp
    assets/ConstructionRegistry.cpp
    assets/AssetBatcher.cpp
    assets/ConfigValidator.cpp
    assets/PriorityConfig.cpp
    assets/RecipeRegistry.cpp
    assets/TaskChainRegistry.cpp
    assets/WorkTypeRegistry.cpp
    assets/lua/LuaEngine.cpp
    assets/lua/LuaGenerator.cpp
    assets/placement/DependencyGraph.cpp
    assets/placement/SpatialIndex.cpp
    assets/placement/PlacementExecutor.cpp
    assets/placement/BakedEntityMesh.cpp
    assets/placement/WorldDepthSort.cpp
)

target_include_directories(assets
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(assets PUBLIC cxx_std_20)

# Find pugixml
find_package(pugixml CONFIG REQUIRED)

# Find Lua and sol2
find_package(Lua REQUIRED)
find_path(SOL2_INCLUDE_DIRS "sol/sol.hpp")

# salal::renderer is PUBLIC: AssetRegistry tessellates each SVG into a renderer::TessellatedMesh
# (the vector module) and hands those out, and AssetRenderer draws an asset to a PNG.
# placement/ holds the CPU halves of the world view that placement needs at placement time
# (BakedEntityMesh, WorldDepthSort, RenderContext); world-view links them from here.
target_link_libraries(assets
    PUBLIC
        salal::foundation
        salal::renderer
        geometry
        world
    PRIVATE
        pugixml::pugixml
        ${LUA_LIBRARIES}
)

target_include_directories(assets
    PRIVATE
        ${LUA_INCLUDE_DIR}
        ${SOL2_INCLUDE_DIRS}
)

# Tests
if(BUILD_TESTING)
    file(GLOB_RECURSE ASSETS_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.test.cpp"
    )

    file(GLOB_RECURSE ASSETS_BENCHMARK_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.bench.cpp"
    )

    if(ASSETS_TEST_SOURCES)
        find_package(GTest CONFIG REQUIRED)
        add_executable(assets-tests ${ASSETS_TEST_SOURCES})
        # AssetRenderer.test.cpp draws through a hidden GL window with the game's base
        # shader, which world-view assembles.
        target_link_libraries(assets-tests
            PRIVATE
                assets
                world-view
                OpenGL::GL
                GLEW::GLEW
                glfw
                GTest::gtest
                GTest::gtest_main
        )
        if(MSVC)
            target_compile_options(assets-tests PRIVATE /Od)
        else()
            target_compile_options(assets-tests PRIVATE -O0 -g)
        endif()
        add_test(NAME assets-tests COMMAND assets-tests)
    endif()

    if(ASSETS_BENCHMARK_SOURCES)
        find_package(benchmark CONFIG REQUIRED)
        add_executable(assets-benchmarks ${ASSETS_BENCHMARK_SOURCES})
        target_link_libraries(assets-benchmarks
            PRIVATE
                assets
                benchmark::benchmark
                benchmark::benchmark_main
        )
        # MSVC: per-config defaults (/O2 in Release); forcing /O2 collides with
        # Debug's /RTC1 (D8016).
        if(NOT MSVC)
            target_compile_options(assets-benchmarks PRIVATE -O3)
        endif()
        add_test(NAME assets-benchmarks COMMAND assets-benchmarks)
    endif()
endif()

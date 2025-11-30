add_library(ui
    core/Placeholder.cpp
    core/RenderContext.cpp
    stb_image.cpp
    font/FontRenderer.cpp
    shapes/Shapes.cpp
    components/button/Button.cpp
    components/TextInput/TextInput.cpp
    focus/FocusManager.cpp
)

target_include_directories(ui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(ui PUBLIC cxx_std_20)

# Find FreeType
find_package(Freetype REQUIRED)

# Link dependencies
target_link_libraries(ui
    PUBLIC
        foundation
        renderer
        engine
        httplib::httplib
        nlohmann_json::nlohmann_json
        Freetype::Freetype
)

# Tests
if(BUILD_TESTING)
    # Discover test files using naming pattern
    file(GLOB_RECURSE UI_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.test.cpp"
    )

    # Discover benchmark files using naming pattern
    file(GLOB_RECURSE UI_BENCHMARK_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.bench.cpp"
    )

    # Create test executable if tests found
    if(UI_TEST_SOURCES)
        find_package(GTest CONFIG REQUIRED)
        add_executable(ui-tests ${UI_TEST_SOURCES})
        target_link_libraries(ui-tests
            PRIVATE
                ui
                GTest::gtest
                GTest::gtest_main
        )
        target_compile_options(ui-tests PRIVATE -O0 -g)
        add_test(NAME ui-tests COMMAND ui-tests)
    endif()

    # Create benchmark executable if benchmarks found
    if(UI_BENCHMARK_SOURCES)
        find_package(benchmark CONFIG REQUIRED)
        add_executable(ui-benchmarks ${UI_BENCHMARK_SOURCES})
        target_link_libraries(ui-benchmarks
            PRIVATE
                ui
                benchmark::benchmark
                benchmark::benchmark_main
        )
        target_compile_options(ui-benchmarks PRIVATE -O3)
        add_test(NAME ui-benchmarks COMMAND ui-benchmarks)
    endif()
endif()

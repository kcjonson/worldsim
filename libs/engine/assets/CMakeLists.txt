add_library(assets
    AssetRegistry.cpp
    generators/GrassBladeGenerator.cpp
)

target_include_directories(assets
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(assets PUBLIC cxx_std_20)

# Find pugixml
find_package(pugixml CONFIG REQUIRED)

# Link dependencies
target_link_libraries(assets
    PUBLIC
        foundation
        renderer
        pugixml::pugixml
)

# Tests
if(BUILD_TESTING)
    file(GLOB_RECURSE ASSETS_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.test.cpp"
    )

    if(ASSETS_TEST_SOURCES)
        find_package(GTest CONFIG REQUIRED)
        add_executable(assets-tests ${ASSETS_TEST_SOURCES})
        target_link_libraries(assets-tests
            PRIVATE
                assets
                GTest::gtest
                GTest::gtest_main
        )
        target_compile_options(assets-tests PRIVATE -O0 -g)
        add_test(NAME assets-tests COMMAND assets-tests)
    endif()
endif()

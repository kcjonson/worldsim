# Worldsim's design system on salal's ui: the Salvage theme as Design Tokens and its icon set, both
# compiled in by salal's build steps. tokens/ and icons/ are written by
# docs/ui-prototype/scripts/gen-cpp-theme.mjs and gen-icon-geometry.mjs; don't edit them by hand.
add_library(salvage STATIC)

target_compile_features(salvage PUBLIC cxx_std_20)

target_link_libraries(salvage
    PUBLIC
        salal::ui
)

salal_tokens(salvage tokens NAMESPACE salvage HEADER salvage/Tokens.h)
salal_icons(salvage icons NAMESPACE salvage HEADER salvage/Icons.h)

if(BUILD_TESTING)
    file(GLOB_RECURSE SALVAGE_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.test.cpp"
    )

    if(SALVAGE_TEST_SOURCES)
        find_package(GTest CONFIG REQUIRED)
        add_executable(salvage-tests ${SALVAGE_TEST_SOURCES})
        target_link_libraries(salvage-tests
            PRIVATE
                salvage
                GTest::gtest
                GTest::gtest_main
        )
        add_test(NAME salvage-tests COMMAND salvage-tests)
    endif()
endif()

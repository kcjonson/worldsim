# Worldsim's design system on salal's ui: worldsim's theme as Design Tokens and its icon set, both
# compiled in by salal's build steps, and worldsim's own components (Panel, Avatar). tokens/ and icons/
# are written by docs/ui-prototype/scripts/gen-cpp-theme.mjs and gen-icon-geometry.mjs; don't edit them
# by hand.
add_library(theme STATIC
    theme/Avatar.cpp
    theme/Panel.cpp
)

target_include_directories(theme
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_compile_features(theme PUBLIC cxx_std_20)

target_link_libraries(theme
    PUBLIC
        salal::ui
)

salal_tokens(theme tokens NAMESPACE theme HEADER theme/DesignTokens.h)
salal_icons(theme icons NAMESPACE theme HEADER theme/Icons.h)

if(BUILD_TESTING)
    file(GLOB_RECURSE THEME_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.test.cpp"
    )

    if(THEME_TEST_SOURCES)
        find_package(GTest CONFIG REQUIRED)
        add_executable(theme-tests ${THEME_TEST_SOURCES})
        target_link_libraries(theme-tests
            PRIVATE
                theme
                GTest::gtest
                GTest::gtest_main
        )
        add_test(NAME theme-tests COMMAND theme-tests)
    endif()
endif()

add_library(ui
    core/placeholder.cpp
    font/font_renderer.cpp
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
        httplib::httplib
        nlohmann_json::nlohmann_json
        Freetype::Freetype
)

# Tests
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

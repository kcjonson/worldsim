add_library(engine
    application/placeholder.cpp
)

target_include_directories(engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(engine PUBLIC cxx_std_20)

# Link dependencies
target_link_libraries(engine
    PUBLIC
        foundation
        renderer
        ui
        nlohmann_json::nlohmann_json
)

# Tests
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

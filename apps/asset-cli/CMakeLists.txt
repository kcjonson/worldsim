add_executable(asset-cli
    Main.cpp
)

target_include_directories(asset-cli
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(asset-cli PRIVATE cxx_std_20)

# assets pulls in renderer + foundation transitively; world-view holds the assembled
# base shader (BaseShader.h, base_shader::source()) this hands Primitives::init, on the
# hidden GLFW window it opens. nlohmann_json is for machine-readable output. No UI lib,
# no debug server.
target_link_libraries(asset-cli
    PRIVATE
        assets
        world-view
        salal::renderer
        salal::foundation
        OpenGL::GL
        GLEW::GLEW
        glfw
        nlohmann_json::nlohmann_json
)

# Asset definitions, SVGs, and Lua scripts next to the exe.
add_custom_command(TARGET asset-cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:asset-cli>/assets
    COMMENT "Copying assets to asset-cli directory"
)

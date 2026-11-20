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

# assets/ is staged beside the exe by stage_assets(asset-cli) in the root CMakeLists.txt.

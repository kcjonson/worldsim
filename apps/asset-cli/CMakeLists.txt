add_executable(asset-cli
    Main.cpp
)

target_include_directories(asset-cli
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(asset-cli PRIVATE cxx_std_20)

# assets pulls in renderer + foundation (and GLFW/GLEW/OpenGL) transitively.
# nlohmann_json is for machine-readable output. No UI lib, no debug server.
target_link_libraries(asset-cli
    PRIVATE
        assets
        renderer
        foundation
        nlohmann_json::nlohmann_json
)

# The render command needs the uber shader next to the exe.
add_dependencies(asset-cli renderer-shaders)
add_custom_command(TARGET asset-cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_BINARY_DIR}/shaders
    $<TARGET_FILE_DIR:asset-cli>/shaders
    COMMENT "Copying shaders to asset-cli directory"
)

# Asset definitions, SVGs, and Lua scripts next to the exe.
add_custom_command(TARGET asset-cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:asset-cli>/assets
    COMMENT "Copying assets to asset-cli directory"
)

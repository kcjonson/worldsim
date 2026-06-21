add_executable(asset-manager
    Main.cpp
    SceneTypes.cpp
    AssetThumbnail.cpp
    AssetRow.cpp
    AssetDetailView.cpp
    scenes/BrowserScene.cpp
)

target_include_directories(asset-manager
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(asset-manager PRIVATE cxx_std_20)

target_link_libraries(asset-manager
    PRIVATE
        engine
        ui
        renderer
        foundation
        assets
)

# Stage shaders, fonts, and assets next to the executable.
add_custom_command(TARGET asset-manager POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_BINARY_DIR}/shaders
    $<TARGET_FILE_DIR:asset-manager>/shaders
    COMMENT "Copying shaders to asset-manager directory"
)

add_custom_command(TARGET asset-manager POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/fonts
    $<TARGET_FILE_DIR:asset-manager>/fonts
    COMMENT "Copying fonts to asset-manager directory"
)

add_custom_command(TARGET asset-manager POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:asset-manager>/assets
    COMMENT "Copying assets to asset-manager directory"
)

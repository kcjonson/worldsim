add_executable(asset-manager
    Main.cpp
    SceneTypes.cpp
    AssetThumbnail.cpp
    AssetRow.cpp
    AssetDetailView.cpp
    GroundBakeView.cpp
    TileImageStrip.cpp
    scenes/BrowserScene.cpp
)

target_include_directories(asset-manager
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(asset-manager PRIVATE cxx_std_20)

# world-view holds the assembled base shader the App starts the renderer with.
target_link_libraries(asset-manager
    PRIVATE
        world-view
        theme
        salal::app
        salal::renderer
        salal::foundation
        assets
)

# Fonts and assets are staged beside the exe by the root CMakeLists (stage_fonts, stage_assets).

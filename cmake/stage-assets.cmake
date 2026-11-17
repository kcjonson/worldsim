# stage_assets(<app>)
# Mirror assets/ beside <app>'s exe, per config, through cmake/mirror-assets.cmake (per-subdir,
# so a file deleted inside a subdir of assets/ is pruned and a separately staged dir such as
# assets/planets/ is left alone; a whole top-level subdir deleted from assets/ is not pruned). <app>-assets is an ALL target with no output, so it runs on every build of the app
# and an asset-only edit restages with no exe relink; a mirror with nothing to copy is cheap.
function(stage_assets app)
    if(NOT TARGET ${app})
        message(FATAL_ERROR "stage_assets(${app}) needs the ${app} target; call it after add_executable")
    endif()
    add_custom_target(${app}-assets ALL
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/assets
            -DDEST_DIR=$<TARGET_FILE_DIR:${app}>/assets
            -P ${CMAKE_SOURCE_DIR}/cmake/mirror-assets.cmake
        COMMENT "Mirroring assets beside ${app}"
        VERBATIM
    )
    add_dependencies(${app} ${app}-assets)
endfunction()

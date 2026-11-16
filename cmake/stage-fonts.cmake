# stage_fonts(<app>)
# Mirror fonts/ beside <app>'s exe, per config. Needs the font-atlas target and SDF_ATLAS_OUTPUTS
# from the root CMakeLists.txt. The copy is keyed on a stamp, so it reruns when any file in fonts/
# changes (a regenerated atlas, a TTF, a licence) and does nothing otherwise, with no exe relink.
function(stage_fonts app)
    file(GLOB font_sources CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/fonts/*")
    set(stamp "${CMAKE_BINARY_DIR}/font-stamps/${app}-$<CONFIG>.stamp")
    add_custom_command(
        OUTPUT ${stamp}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/fonts
            $<TARGET_FILE_DIR:${app}>/fonts
        COMMAND ${CMAKE_COMMAND} -E touch ${stamp}
        DEPENDS ${font_sources} ${SDF_ATLAS_OUTPUTS}
        COMMENT "Staging fonts beside ${app}"
        VERBATIM
    )
    add_custom_target(${app}-fonts ALL DEPENDS ${stamp})
    add_dependencies(${app}-fonts font-atlas)
    add_dependencies(${app} ${app}-fonts)
endfunction()

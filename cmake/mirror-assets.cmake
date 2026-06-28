# mirror-assets.cmake
# Mirror each top-level subdir of SOURCE_DIR into DEST_DIR independently.
#
# Unlike mirroring SOURCE_DIR itself, this per-subdir approach leaves any extra
# subdirs in DEST_DIR (e.g. assets/planets/ staged by a separate build step)
# completely untouched. Only the subdirs that exist in SOURCE_DIR are synced,
# and within each of those, deletions are propagated (robocopy /MIR / rsync --delete).
#
# Required variables (pass via cmake -DSOURCE_DIR=... -DDEST_DIR=...):
#   SOURCE_DIR  — source assets root (e.g. <src>/assets)
#   DEST_DIR    — destination root  (e.g. <build>/Debug/assets)

if(NOT EXISTS "${SOURCE_DIR}")
    message(FATAL_ERROR "mirror-assets: SOURCE_DIR does not exist: ${SOURCE_DIR}")
endif()

file(GLOB source_subdirs LIST_DIRECTORIES true "${SOURCE_DIR}/*")

foreach(src_sub IN LISTS source_subdirs)
    get_filename_component(name "${src_sub}" NAME)
    set(dst_sub "${DEST_DIR}/${name}")

    if(IS_DIRECTORY "${src_sub}")
        if(CMAKE_HOST_WIN32)
            # robocopy exit codes: 0-7 are all success variants.
            # 8+ mean real errors (access denied, out of space, etc.).
            execute_process(
                COMMAND robocopy "${src_sub}" "${dst_sub}" /MIR /NJH /NJS /NFL /NDL
                RESULT_VARIABLE rc
            )
            if(rc GREATER_EQUAL 8)
                message(FATAL_ERROR "robocopy failed (exit ${rc}) mirroring assets/${name}")
            endif()
        else()
            execute_process(
                COMMAND rsync -a --delete "${src_sub}/" "${dst_sub}/"
                RESULT_VARIABLE rc
            )
            if(NOT rc EQUAL 0)
                message(FATAL_ERROR "rsync failed (exit ${rc}) mirroring assets/${name}")
            endif()
        endif()
    else()
        # Top-level files alongside subdirs: sync with copy_if_different.
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${src_sub}" "${dst_sub}"
        )
    endif()
endforeach()

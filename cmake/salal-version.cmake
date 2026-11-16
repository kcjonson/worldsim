# salal-version.cmake
# Writes salal's identity into a header for the ground bake cache key (GroundTileCache.cpp): git describe
# --tags --always --dirty of the salal checkout this build compiles against, and when it's dirty, 12 hex
# digits of a SHA-1 of its uncommitted diff, so two different edits of a sibling checkout never share a
# key. The header is rewritten only when that text changes, so what includes it recompiles only then.
# A git that fails fails the build: there is no version to key on without it.
#
# Required variables (pass via cmake -D<name>=...):
#   GIT        git executable
#   SALAL_DIR  salal's source dir (salal_SOURCE_DIR: the FetchContent clone, or FETCHCONTENT_SOURCE_DIR_SALAL)
#   OUTPUT     the header to write

foreach(required GIT SALAL_DIR OUTPUT)
    if(NOT ${required})
        message(FATAL_ERROR "salal-version: ${required} is not set")
    endif()
endforeach()

execute_process(
    COMMAND "${GIT}" -C "${SALAL_DIR}" describe --tags --always --dirty
    RESULT_VARIABLE result
    OUTPUT_VARIABLE version
    ERROR_VARIABLE error
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
)
if(NOT result EQUAL 0 OR version STREQUAL "")
    message(FATAL_ERROR
        "salal-version: git describe failed in ${SALAL_DIR} (${result}: ${error}). "
        "The ground bake cache key needs salal's version; salal must be a git checkout.")
endif()

get_filename_component(output_dir "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")

if(version MATCHES "-dirty$")
    # To a file, not a variable: a CMake string can't hold every byte a diff can.
    set(diff_file "${OUTPUT}.diff")
    execute_process(
        COMMAND "${GIT}" -C "${SALAL_DIR}" diff HEAD --binary --no-color --no-ext-diff --no-textconv
        RESULT_VARIABLE result
        OUTPUT_FILE "${diff_file}"
        ERROR_VARIABLE error
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    if(NOT result EQUAL 0)
        file(REMOVE "${diff_file}")
        message(FATAL_ERROR
            "salal-version: git diff failed in ${SALAL_DIR} (${result}: ${error}). "
            "The ground bake cache key needs a hash of salal's uncommitted changes.")
    endif()
    file(SHA1 "${diff_file}" diff_hash)
    file(REMOVE "${diff_file}")
    string(SUBSTRING "${diff_hash}" 0 12 diff_hash)
    string(APPEND version "-${diff_hash}")
endif()

string(REPLACE "\\" "\\\\" escaped "${version}")
string(REPLACE "\"" "\\\"" escaped "${escaped}")
set(content
"#pragma once

// Written by cmake/salal-version.cmake on every build: the salal checkout this build compiles against.

namespace worldview {
	inline constexpr char kSalalVersion[] = \"${escaped}\";
}
")

set(existing "")
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" existing)
endif()
if(NOT content STREQUAL existing)
    file(WRITE "${OUTPUT}" "${content}")
endif()

# worldsim_test_data(<test> <dir>...)
# The source directories a test reads at run time, relative to the repo root (assets, fonts). CI's
# test selection (scripts/ci/test-select.mjs) keys the test on their content, and finds them in the
# test's REQUIRED_FILES, which `ctest --show-only=json-v1` reports on every CMake version; ctest
# also reports the test Not Run if one is missing. Call it in the directory that added the test.

function(worldsim_test_data test)
    foreach(dir IN LISTS ARGN)
        if(NOT IS_DIRECTORY "${CMAKE_SOURCE_DIR}/${dir}")
            message(FATAL_ERROR "worldsim_test_data(${test}): ${dir} is not a directory under ${CMAKE_SOURCE_DIR}")
        endif()
        set_property(TEST ${test} APPEND PROPERTY REQUIRED_FILES "${CMAKE_SOURCE_DIR}/${dir}")
    endforeach()
endfunction()

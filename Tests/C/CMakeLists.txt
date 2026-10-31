# C tests for the standalone CoreFoundation build.
# SPDX-License-Identifier: Apache-2.0

if(MSVC)
    set(cf_test_blocks_flag /clang:-fblocks)
else()
    set(cf_test_blocks_flag -fblocks)
endif()

#--- smoke test ----------------------------------------------------------------

add_executable(cf_smoke smoke.c)
target_link_libraries(cf_smoke PRIVATE CoreFoundation::CoreFoundation)
target_compile_options(cf_smoke PRIVATE ${cf_test_blocks_flag})
# Code that uses blocks itself needs the Blocks runtime; CoreFoundation only
# carries it when the vendored copy was compiled in.
if(NOT CF_USE_VENDORED_BLOCKSRUNTIME)
    target_link_libraries(cf_smoke PRIVATE CF::BlocksRuntime)
endif()
add_test(NAME cf_smoke COMMAND cf_smoke)
set_tests_properties(cf_smoke PROPERTIES TIMEOUT 60 ENVIRONMENT "TZ=UTC")

#--- header checks -------------------------------------------------------------

# The umbrella header plus the headers it does not pull in, compiled the way a
# C consumer would (no prefix header, no CF_BUILDING_CF), and a check that the
# headers agree with the library on the object layout.
add_executable(cf_headers headers.c)
target_link_libraries(cf_headers PRIVATE CoreFoundation::CoreFoundation)
target_compile_options(cf_headers PRIVATE ${cf_test_blocks_flag} -Wall)
add_test(NAME cf_headers COMMAND cf_headers)

# Every installed public header must compile on its own, both with and without
# Blocks support. One generated translation unit per header; compile only.
file(GLOB cf_public_headers CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/Sources/CoreFoundation/include/*.h")
set(cf_header_tus_blocks)
set(cf_header_tus_noblocks)
foreach(header IN LISTS cf_public_headers)
    get_filename_component(name "${header}" NAME_WE)
    foreach(variant blocks noblocks)
        set(tu "${CMAKE_CURRENT_BINARY_DIR}/header-checks/${variant}/${name}.c")
        file(CONFIGURE OUTPUT "${tu}" CONTENT
            "/* generated: ${name}.h must be self-contained */\n#include <CoreFoundation/${name}.h>\n#include <CoreFoundation/${name}.h>\n")
        list(APPEND cf_header_tus_${variant} "${tu}")
    endforeach()
endforeach()

add_library(cf_header_check_blocks OBJECT ${cf_header_tus_blocks})
target_link_libraries(cf_header_check_blocks PRIVATE CoreFoundation::CoreFoundation)
target_compile_options(cf_header_check_blocks PRIVATE ${cf_test_blocks_flag} -Wall -Wextra -Wno-unused-parameter)

add_library(cf_header_check_noblocks OBJECT ${cf_header_tus_noblocks})
target_link_libraries(cf_header_check_noblocks PRIVATE CoreFoundation::CoreFoundation)
target_compile_options(cf_header_check_noblocks PRIVATE -Wall -Wextra -Wno-unused-parameter)

# Surface the compile-only checks in ctest as well, so a plain `ctest` run
# reports them (they are also built as part of `all`).
add_test(NAME cf_header_check
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target cf_header_check_blocks cf_header_check_noblocks)

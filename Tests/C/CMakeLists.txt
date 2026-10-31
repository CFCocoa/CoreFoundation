# C smoke tests for the standalone CoreFoundation build.
# SPDX-License-Identifier: Apache-2.0

add_executable(cf_smoke smoke.c)
target_link_libraries(cf_smoke PRIVATE CoreFoundation::CoreFoundation)
# Code that uses blocks itself needs the Blocks runtime; CoreFoundation only
# carries it when the vendored copy was compiled in.
if(NOT CF_USE_VENDORED_BLOCKSRUNTIME)
    target_link_libraries(cf_smoke PRIVATE CF::BlocksRuntime)
endif()
target_compile_options(cf_smoke PRIVATE $<IF:$<BOOL:${MSVC}>,/clang:-fblocks,-fblocks>)
add_test(NAME cf_smoke COMMAND cf_smoke)
set_tests_properties(cf_smoke PROPERTIES TIMEOUT 60 ENVIRONMENT "TZ=UTC")

# Every installed public header must compile on its own, as a C consumer
# would include it (no prefix header, no CF_BUILDING_CF).
add_executable(cf_headers headers.c)
target_link_libraries(cf_headers PRIVATE CoreFoundation::CoreFoundation)
target_compile_options(cf_headers PRIVATE $<IF:$<BOOL:${MSVC}>,/clang:-fblocks,-fblocks> -Wall)
add_test(NAME cf_headers COMMAND cf_headers)

##===----------------------------------------------------------------------===##
##
## Compiler settings shared by CoreFoundation, _CFXMLInterface and
## _CFURLSessionInterface. The flag list mirrors upstream's Package.swift and
## CMake build, minus everything that only makes sense for the Swift runtime.
##
## SPDX-License-Identifier: Apache-2.0
##
##===----------------------------------------------------------------------===##

include_guard(GLOBAL)
include(CheckSymbolExists)
include(CheckIncludeFile)

# Feature checks. SwiftPM cannot do these, which is why upstream compiles its
# own strlcpy/strlcat unconditionally; here we use libc's when present.
check_symbol_exists(strlcat "string.h" CF_HAVE_STRLCAT)
check_symbol_exists(strlcpy "string.h" CF_HAVE_STRLCPY)
check_symbol_exists(issetugid "unistd.h" CF_HAVE_ISSETUGID)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
    check_symbol_exists(sched_getaffinity "sched.h" CF_HAVE_SCHED_GETAFFINITY)
    unset(CMAKE_REQUIRED_DEFINITIONS)
endif()


# Applies the private compile definitions and options every CF C target needs.
function(cf_apply_common_c_settings target)
    # Resolved here rather than at include time: include_guard(GLOBAL) means
    # this file is read in one directory scope only, but the function is
    # called from several.
    get_filename_component(CF_PREFIX_HEADER
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../Sources/CoreFoundation/internalInclude/CoreFoundation_Prefix.h" ABSOLUTE)

    target_compile_definitions(${target} PRIVATE
        CF_BUILDING_CF
        HAVE_STRUCT_TIMESPEC
        $<$<CONFIG:Debug>:DEBUG>
        $<$<BOOL:${CF_HAVE_STRLCAT}>:HAVE_STRLCAT>
        $<$<BOOL:${CF_HAVE_STRLCPY}>:HAVE_STRLCPY>
        $<$<BOOL:${CF_HAVE_ISSETUGID}>:HAVE_ISSETUGID>
        $<$<BOOL:${CF_HAVE_SCHED_GETAFFINITY}>:HAVE_SCHED_GETAFFINITY>)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Android")
        target_compile_definitions(${target} PRIVATE _GNU_SOURCE)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
        target_compile_definitions(${target} PRIVATE
            _WASI_EMULATED_MMAN _WASI_EMULATED_SIGNAL _WASI_EMULATED_PROCESS_CLOCKS)
    endif()
    if(WIN32)
        # Only the dynamic CRT is supported, as upstream.
        target_compile_definitions(${target} PRIVATE _DLL $<$<BOOL:${BUILD_SHARED_LIBS}>:_WINDLL>)
    endif()

    # Runtime mode. The headers default to the C runtime, so consumers need no
    # definition in that case; the Swift mode must be visible to consumers.
    if(CF_SWIFT_RUNTIME)
        target_compile_definitions(${target} PUBLIC DEPLOYMENT_RUNTIME_SWIFT=1)
    else()
        target_compile_definitions(${target} PRIVATE DEPLOYMENT_RUNTIME_SWIFT=0)
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE
            /clang:-fblocks /EHsc
            "/FI${CF_PREFIX_HEADER}"
            $<$<BOOL:${CF_SWIFT_RUNTIME}>:/clang:-fcf-runtime-abi=swift>)
    else()
        target_compile_options(${target} PRIVATE
            -fblocks
            -fconstant-cfstrings
            -fdollars-in-identifiers
            -fno-common
            -Werror=implicit-function-declaration
            -Wno-shorten-64-to-32
            -Wno-deprecated-declarations
            -Wno-unreachable-code
            -Wno-conditional-uninitialized
            -Wno-unused-variable
            -Wno-unused-function
            -Wno-microsoft-enum-forward-reference
            -Wno-int-conversion
            -Wno-switch
            "SHELL:-include ${CF_PREFIX_HEADER}"
            $<$<BOOL:${CF_SWIFT_RUNTIME}>:-fcf-runtime-abi=swift>)
        if(NOT CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
            target_compile_options(${target} PRIVATE -fexceptions)
        endif()
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            # Better backtraces, as in the upstream build.
            target_compile_options(${target} PRIVATE -fno-omit-frame-pointer)
        endif()
        if(CF_ENABLE_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

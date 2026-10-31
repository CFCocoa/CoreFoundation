##===----------------------------------------------------------------------===##
##
## Locates CoreFoundation's external dependencies and wraps them in
## namespaced INTERFACE targets so the rest of the build (and the installed
## package config, which includes this same file) can refer to them uniformly:
##
##   CF::ICU            ICU common + i18n + data
##   CF::dispatch       libdispatch (swift-corelibs-libdispatch on non-Darwin)
##   CF::BlocksRuntime  the Blocks runtime, unless CF_USE_VENDORED_BLOCKSRUNTIME
##
## Inputs: CF_ICU_PROVIDER, CF_DISPATCH_ROOT, CF_BLOCKS_RUNTIME,
##         CF_DEPENDENCIES_REQUIRED (set by the installed config to make a
##         missing dependency a hard error with a consumer-oriented message).
##
## SPDX-License-Identifier: Apache-2.0
##
##===----------------------------------------------------------------------===##

include_guard(GLOBAL)

function(_cf_add_interface_wrapper name)
    if(NOT TARGET ${name})
        add_library(${name} INTERFACE IMPORTED GLOBAL)
    endif()
    if(ARGN)
        set_property(TARGET ${name} APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${ARGN})
    endif()
endfunction()

macro(cf_find_dependencies)
    if(NOT DEFINED CF_USE_VENDORED_BLOCKSRUNTIME)
        set(CF_USE_VENDORED_BLOCKSRUNTIME OFF)
    endif()
    #---------------------------------------------------------------- ICU
    if(NOT TARGET CF::ICU)
        if(CF_ICU_PROVIDER STREQUAL "foundation-icu")
            if(NOT TARGET _FoundationICU)
                message(FATAL_ERROR "CF_ICU_PROVIDER=foundation-icu but no _FoundationICU target exists")
            endif()
            _cf_add_interface_wrapper(CF::ICU _FoundationICU)
            set(CF_ICU_SUMMARY "swift-foundation-icu (${CF_FOUNDATION_ICU_DIR})")
        else()
            # ICU's own CMake module. 'data' is libicudata (libicudt on Windows).
            find_package(ICU REQUIRED COMPONENTS uc i18n data)
            _cf_add_interface_wrapper(CF::ICU ICU::uc ICU::i18n ICU::data)
            set(CF_ICU_INCLUDE_DIR "${ICU_INCLUDE_DIRS}")
            set(CF_ICU_SUMMARY "system ICU ${ICU_VERSION} (${ICU_INCLUDE_DIRS})")
        endif()
    endif()

    #---------------------------------------------------------------- dispatch
    if(NOT TARGET CF::dispatch)
        if(NOT TARGET dispatch)
            find_package(dispatch CONFIG QUIET)
        endif()
        if(TARGET dispatch)
            _cf_add_interface_wrapper(CF::dispatch dispatch)
            if(CF_DISPATCH_FROM_FETCHCONTENT)
                set(CF_DISPATCH_SUMMARY "swift-corelibs-libdispatch ${CF_LIBDISPATCH_GIT_TAG} (built in-tree)")
            else()
                get_target_property(_cf_dispatch_loc dispatch LOCATION)
                set(CF_DISPATCH_SUMMARY "CMake package (${_cf_dispatch_loc})")
            endif()
        else()
            # No package config: look for headers and library directly. The
            # hints cover a plain install prefix and the Swift toolchain layout
            # (/usr/lib/swift/dispatch, /usr/lib/swift/linux/libdispatch.so).
            find_path(CF_DISPATCH_INCLUDE_DIR
                NAMES dispatch/dispatch.h
                HINTS ${CF_DISPATCH_ROOT}/include ${CF_DISPATCH_ROOT}
                      /usr/lib/swift /usr/local/lib/swift
                      $ENV{SDKROOT}/usr/include)
            find_library(CF_DISPATCH_LIBRARY
                NAMES dispatch libdispatch
                HINTS ${CF_DISPATCH_ROOT}/lib ${CF_DISPATCH_ROOT}/lib64
                      /usr/lib/swift/linux /usr/local/lib/swift/linux
                      $ENV{SDKROOT}/usr/lib/swift/windows)
            if(CF_DISPATCH_INCLUDE_DIR AND CF_DISPATCH_LIBRARY)
                add_library(CF::dispatch UNKNOWN IMPORTED GLOBAL)
                set_target_properties(CF::dispatch PROPERTIES
                    IMPORTED_LOCATION "${CF_DISPATCH_LIBRARY}"
                    INTERFACE_INCLUDE_DIRECTORIES "${CF_DISPATCH_INCLUDE_DIR}")
                # Swift toolchain layout keeps Block.h next to dispatch/.
                if(EXISTS "${CF_DISPATCH_INCLUDE_DIR}/Block/Block.h")
                    set_property(TARGET CF::dispatch APPEND PROPERTY
                        INTERFACE_INCLUDE_DIRECTORIES "${CF_DISPATCH_INCLUDE_DIR}/Block")
                endif()
                set(CF_DISPATCH_SUMMARY "${CF_DISPATCH_LIBRARY}")
            else()
                message(FATAL_ERROR
                    "libdispatch was not found. CoreFoundation needs swift-corelibs-libdispatch "
                    "(package 'libdispatch-dev' on Debian; not packaged on Ubuntu). Options:\n"
                    "  - install it and point CMAKE_PREFIX_PATH or CF_DISPATCH_ROOT at its prefix\n"
                    "  - configure with -DCF_FETCH_LIBDISPATCH=ON to build it as part of this build")
            endif()
        endif()
    endif()

    #---------------------------------------------------------------- Blocks
    if(NOT TARGET CF::BlocksRuntime AND NOT CF_USE_VENDORED_BLOCKSRUNTIME)
        if(CF_BLOCKS_RUNTIME STREQUAL "vendored")
            set(CF_USE_VENDORED_BLOCKSRUNTIME ON)
            set(CF_BLOCKSRUNTIME_SUMMARY "vendored (Sources/CoreFoundation/BlockRuntime)")
        else()
            if(TARGET BlocksRuntime)
                # Provided by an in-tree swift-corelibs-libdispatch build.
                _cf_add_interface_wrapper(CF::BlocksRuntime BlocksRuntime)
                set(CF_BLOCKSRUNTIME_SUMMARY "from libdispatch build")
            else()
                find_package(BlocksRuntime QUIET)
                if(BlocksRuntime_FOUND)
                    _cf_add_interface_wrapper(CF::BlocksRuntime BlocksRuntime::BlocksRuntime)
                    set(CF_BLOCKSRUNTIME_SUMMARY "${BlocksRuntime_LIBRARY}")
                elseif(CF_BLOCKS_RUNTIME STREQUAL "system")
                    message(FATAL_ERROR
                        "No Blocks runtime found (package 'libblocksruntime-dev' on Debian/Ubuntu). "
                        "Install it or configure with -DCF_BLOCKS_RUNTIME=vendored.")
                else()
                    set(CF_USE_VENDORED_BLOCKSRUNTIME ON)
                    set(CF_BLOCKSRUNTIME_SUMMARY "vendored (no system BlocksRuntime found)")
                endif()
            endif()
        endif()
    endif()
endmacro()

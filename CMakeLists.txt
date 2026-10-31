##===----------------------------------------------------------------------===##
##
## CoreFoundation as a standalone C library.
##
## This build takes the CoreFoundation sources from swift-corelibs-foundation
## and builds them as a plain C library with no Swift toolchain involved.
## See Docs/StandaloneCBuildPlan.md for the design and Docs/UPSTREAM.md for
## how sources are synchronised from upstream.
##
## SPDX-License-Identifier: Apache-2.0
##
##===----------------------------------------------------------------------===##

cmake_minimum_required(VERSION 3.20)

project(CoreFoundation
    VERSION 1.0.0
    DESCRIPTION "Apple CoreFoundation, built as a standalone C library"
    HOMEPAGE_URL "https://github.com/CFCocoa/CoreFoundation"
    LANGUAGES C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CF_IS_TOP_LEVEL ON)
else()
    set(CF_IS_TOP_LEVEL OFF)
endif()

#-------------------------------------------------------------------------------
# Options
#-------------------------------------------------------------------------------

option(BUILD_SHARED_LIBS "Build CoreFoundation as a shared library" ON)
option(CF_BUILD_XML_INTERFACE
    "Build the _CFXMLInterface helper library (requires libxml2; only useful for Swift Foundation compatibility)" OFF)
option(CF_BUILD_URLSESSION_INTERFACE
    "Build the _CFURLSessionInterface helper library (requires libcurl; only useful for Swift Foundation compatibility)" OFF)
option(CF_SWIFT_RUNTIME
    "Build CoreFoundation for the Swift runtime instead of the C runtime. Unsupported; kept only so the Swift mode keeps compiling." OFF)
set(CF_ICU_PROVIDER "system" CACHE STRING
    "Which ICU to build against: 'system' (find_package(ICU)) or 'foundation-icu' (a swift-foundation-icu checkout, see CF_FOUNDATION_ICU_DIR)")
set_property(CACHE CF_ICU_PROVIDER PROPERTY STRINGS system foundation-icu)
set(CF_FOUNDATION_ICU_DIR "" CACHE PATH
    "Path to a swift-foundation-icu checkout, used when CF_ICU_PROVIDER=foundation-icu")
option(CF_FETCH_LIBDISPATCH
    "If no libdispatch is found, download and build swift-corelibs-libdispatch as part of this build" OFF)
set(CF_LIBDISPATCH_GIT_TAG "swift-6.1-RELEASE" CACHE STRING
    "swift-corelibs-libdispatch tag used by CF_FETCH_LIBDISPATCH")
set(CF_DISPATCH_ROOT "" CACHE PATH
    "Prefix of a libdispatch installation to use when no CMake package config for it is available")
set(CF_BLOCKS_RUNTIME "auto" CACHE STRING
    "Where the Blocks runtime comes from: 'auto' (system, else vendored), 'system' or 'vendored'")
set_property(CACHE CF_BLOCKS_RUNTIME PROPERTY STRINGS auto system vendored)
option(CF_BUILD_TESTS "Build the CoreFoundation C tests" ${CF_IS_TOP_LEVEL})
option(CF_INSTALL "Generate install rules" ${CF_IS_TOP_LEVEL})
option(CF_ENABLE_WERROR "Treat warnings as errors" OFF)

#-------------------------------------------------------------------------------
# Global settings
#-------------------------------------------------------------------------------

include(GNUInstallDirs)
include(CheckCCompilerFlag)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED YES)
set(CMAKE_C_EXTENSIONS YES)
set(CMAKE_POSITION_INDEPENDENT_CODE YES)

if(CF_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

if(CF_IS_TOP_LEVEL)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

# CoreFoundation is written against the Blocks language extension; only clang
# (and clang-cl) implement it.
if(MSVC)
    check_c_compiler_flag("/clang:-fblocks" CF_COMPILER_SUPPORTS_BLOCKS)
else()
    check_c_compiler_flag("-fblocks" CF_COMPILER_SUPPORTS_BLOCKS)
endif()
if(NOT CF_COMPILER_SUPPORTS_BLOCKS)
    message(FATAL_ERROR
        "CoreFoundation requires a C compiler with Blocks support (clang). "
        "Configure with -DCMAKE_C_COMPILER=clang.")
endif()

if(CF_ICU_PROVIDER STREQUAL "foundation-icu" AND NOT CF_FOUNDATION_ICU_DIR)
    message(FATAL_ERROR "CF_ICU_PROVIDER=foundation-icu requires CF_FOUNDATION_ICU_DIR to point at a swift-foundation-icu checkout")
endif()

#-------------------------------------------------------------------------------
# Dependencies
#-------------------------------------------------------------------------------

find_package(Threads REQUIRED)

# Optionally build swift-corelibs-libdispatch in-tree when the system has none
# (Ubuntu, for example, does not package it).
if(CF_FETCH_LIBDISPATCH AND NOT TARGET dispatch)
    find_package(dispatch CONFIG QUIET)
    if(NOT dispatch_FOUND)
        message(STATUS "libdispatch not found, fetching swift-corelibs-libdispatch ${CF_LIBDISPATCH_GIT_TAG}")
        include(FetchContent)
        set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(ENABLE_SWIFT OFF CACHE BOOL "" FORCE)
        set(INSTALL_PRIVATE_HEADERS ON CACHE BOOL "" FORCE)
        FetchContent_Declare(libdispatch
            GIT_REPOSITORY https://github.com/swiftlang/swift-corelibs-libdispatch.git
            GIT_TAG ${CF_LIBDISPATCH_GIT_TAG}
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(libdispatch)
        set(CF_DISPATCH_FROM_FETCHCONTENT ON)
    endif()
endif()

if(CF_ICU_PROVIDER STREQUAL "foundation-icu")
    add_subdirectory(${CF_FOUNDATION_ICU_DIR} ${CMAKE_CURRENT_BINARY_DIR}/_deps/foundation-icu EXCLUDE_FROM_ALL)
    if(NOT TARGET _FoundationICU)
        message(FATAL_ERROR "${CF_FOUNDATION_ICU_DIR} did not provide a _FoundationICU target")
    endif()
endif()

# Creates the CF::ICU, CF::dispatch and CF::BlocksRuntime targets (the latter
# is replaced by the vendored sources when CF_USE_VENDORED_BLOCKSRUNTIME is set).
include(CFFindDependencies)
cf_find_dependencies()

#-------------------------------------------------------------------------------
# Libraries, tests, install
#-------------------------------------------------------------------------------

add_subdirectory(Sources)

if(CF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests/C)
endif()

if(CF_INSTALL)
    include(CMakePackageConfigHelpers)
    set(CF_CMAKE_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/CoreFoundation)

    install(EXPORT CoreFoundationTargets
        NAMESPACE CoreFoundation::
        DESTINATION ${CF_CMAKE_INSTALL_DIR})

    configure_package_config_file(cmake/CoreFoundationConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/CoreFoundationConfig.cmake
        INSTALL_DESTINATION ${CF_CMAKE_INSTALL_DIR})
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/CoreFoundationConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/CoreFoundationConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/CoreFoundationConfigVersion.cmake
        cmake/CFFindDependencies.cmake
        cmake/FindBlocksRuntime.cmake
        DESTINATION ${CF_CMAKE_INSTALL_DIR})

    set(CF_PC_REQUIRES_PRIVATE "icu-uc icu-i18n")
    set(CF_PC_LIBS_PRIVATE "-ldispatch -lpthread -ldl -lm")
    if(NOT CF_USE_VENDORED_BLOCKSRUNTIME)
        string(APPEND CF_PC_LIBS_PRIVATE " -lBlocksRuntime")
    endif()
    configure_file(cmake/CoreFoundation.pc.in ${CMAKE_CURRENT_BINARY_DIR}/CoreFoundation.pc @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/CoreFoundation.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endif()

#-------------------------------------------------------------------------------
# Summary
#-------------------------------------------------------------------------------

if(CF_IS_TOP_LEVEL)
    message(STATUS "")
    message(STATUS "CoreFoundation ${PROJECT_VERSION}")
    if(BUILD_SHARED_LIBS)
        message(STATUS "  Library type:            shared")
    else()
        message(STATUS "  Library type:            static")
    endif()
    if(CF_SWIFT_RUNTIME)
        message(STATUS "  Runtime mode:            Swift (unsupported)")
    else()
        message(STATUS "  Runtime mode:            C")
    endif()
    message(STATUS "  ICU:                     ${CF_ICU_SUMMARY}")
    message(STATUS "  libdispatch:             ${CF_DISPATCH_SUMMARY}")
    message(STATUS "  BlocksRuntime:           ${CF_BLOCKSRUNTIME_SUMMARY}")
    message(STATUS "  _CFXMLInterface:         ${CF_BUILD_XML_INTERFACE}")
    message(STATUS "  _CFURLSessionInterface:  ${CF_BUILD_URLSESSION_INTERFACE}")
    message(STATUS "  Tests:                   ${CF_BUILD_TESTS}")
    message(STATUS "")
endif()

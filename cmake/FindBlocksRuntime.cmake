# FindBlocksRuntime
# -----------------
# Finds the Blocks runtime library (libBlocksRuntime) and its Block.h header,
# as installed by libblocksruntime-dev or by swift-corelibs-libdispatch.
#
# Result variables: BlocksRuntime_FOUND, BlocksRuntime_INCLUDE_DIR,
# BlocksRuntime_LIBRARY. Imported target: BlocksRuntime::BlocksRuntime.
#
# SPDX-License-Identifier: Apache-2.0

find_path(BlocksRuntime_INCLUDE_DIR
    NAMES Block.h
    HINTS ${BlocksRuntime_ROOT}/include
          /usr/lib/swift/Block /usr/local/lib/swift/Block)
find_library(BlocksRuntime_LIBRARY
    NAMES BlocksRuntime libBlocksRuntime
    HINTS ${BlocksRuntime_ROOT}/lib
          /usr/lib/swift/linux /usr/local/lib/swift/linux)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(BlocksRuntime
    REQUIRED_VARS BlocksRuntime_LIBRARY BlocksRuntime_INCLUDE_DIR)
mark_as_advanced(BlocksRuntime_LIBRARY BlocksRuntime_INCLUDE_DIR)

if(BlocksRuntime_FOUND AND NOT TARGET BlocksRuntime::BlocksRuntime)
    add_library(BlocksRuntime::BlocksRuntime UNKNOWN IMPORTED)
    set_target_properties(BlocksRuntime::BlocksRuntime PROPERTIES
        IMPORTED_LOCATION "${BlocksRuntime_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${BlocksRuntime_INCLUDE_DIR}")
endif()

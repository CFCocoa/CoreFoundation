# _CFURLSessionInterface: libcurl wrappers used by Swift's FoundationNetworking.
# Only useful for Swift Foundation compatibility; C code can use libcurl directly.
# SPDX-License-Identifier: Apache-2.0

include(CFCommonSettings)

find_package(CURL REQUIRED)

add_library(_CFURLSessionInterface CFURLSessionInterface.c)
add_library(CoreFoundation::_CFURLSessionInterface ALIAS _CFURLSessionInterface)
set_target_properties(_CFURLSessionInterface PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

target_include_directories(_CFURLSessionInterface
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/CFURLSessionInterface>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../CoreFoundation/internalInclude)

cf_apply_common_c_settings(_CFURLSessionInterface)
if(WIN32)
    target_compile_definitions(_CFURLSessionInterface PRIVATE CURL_STATICLIB)
endif()

target_link_libraries(_CFURLSessionInterface
    PUBLIC CoreFoundation
    PRIVATE CURL::libcurl CF::dispatch)

if(CF_INSTALL)
    install(TARGETS _CFURLSessionInterface
        EXPORT CoreFoundationTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/CFURLSessionInterface)
endif()

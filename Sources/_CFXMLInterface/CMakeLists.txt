# _CFXMLInterface: libxml2 helpers used by Swift's FoundationXML. In the C
# runtime mode the SAX callback bridge is compiled out, so this library is only
# useful for Swift Foundation compatibility.
# SPDX-License-Identifier: Apache-2.0

include(CFCommonSettings)

if(ANDROID)
    # LibXml2 looks for the Threads package; keep it from using -pthread on Android.
    set(CMAKE_HAVE_LIBC_PTHREAD YES)
endif()
find_package(LibXml2 REQUIRED)

add_library(_CFXMLInterface CFXMLInterface.c)
add_library(CoreFoundation::_CFXMLInterface ALIAS _CFXMLInterface)
set_target_properties(_CFXMLInterface PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

target_include_directories(_CFXMLInterface
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/CFXMLInterface>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../CoreFoundation/internalInclude)

cf_apply_common_c_settings(_CFXMLInterface)

target_link_libraries(_CFXMLInterface
    PUBLIC CoreFoundation
    PRIVATE LibXml2::LibXml2 CF::dispatch)
if(NOT CF_USE_VENDORED_BLOCKSRUNTIME)
    target_link_libraries(_CFXMLInterface PRIVATE CF::BlocksRuntime)
endif()

if(CF_INSTALL)
    install(TARGETS _CFXMLInterface
        EXPORT CoreFoundationTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/CFXMLInterface)
endif()

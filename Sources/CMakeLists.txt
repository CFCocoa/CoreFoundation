# SPDX-License-Identifier: Apache-2.0

add_subdirectory(CoreFoundation)
if(CF_BUILD_XML_INTERFACE)
    add_subdirectory(_CFXMLInterface)
endif()
if(CF_BUILD_URLSESSION_INTERFACE)
    add_subdirectory(_CFURLSessionInterface)
endif()

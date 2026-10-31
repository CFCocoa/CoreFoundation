##===----------------------------------------------------------------------===##
##
## CoreFoundation library target.
##
## SPDX-License-Identifier: Apache-2.0
##
##===----------------------------------------------------------------------===##

include(CFCommonSettings)
include(CFIcuShim)

add_library(CoreFoundation
    CFApplicationPreferences.c
    CFArray.c
    CFAttributedString.c
    CFBag.c
    CFBase.c
    CFBasicHash.c
    CFBigNumber.c
    CFBinaryHeap.c
    CFBinaryPList.c
    CFBitVector.c
    CFBuiltinConverters.c
    CFBundle_Binary.c
    CFBundle_DebugStrings.c
    CFBundle_Executable.c
    CFBundle_Grok.c
    CFBundle_InfoPlist.c
    CFBundle_Locale.c
    CFBundle_Main.c
    CFBundle_ResourceFork.c
    CFBundle_Resources.c
    CFBundle_SplitFileName.c
    CFBundle_Strings.c
    CFBundle_Tables.c
    CFBundle.c
    CFBurstTrie.c
    CFCalendar_Enumerate.c
    CFCalendar.c
    CFCharacterSet.c
    CFConcreteStreams.c
    CFData.c
    CFDate.c
    CFDateComponents.c
    CFDateFormatter.c
    CFDateInterval.c
    CFDateIntervalFormatter.c
    CFDictionary.c
    CFError.c
    CFFileUtilities.c
    CFICUConverters.c
    CFKnownLocations.c
    CFListFormatter.c
    CFLocale.c
    CFLocaleIdentifier.c
    CFLocaleKeys.c
    CFNumber.c
    CFNumberFormatter.c
    CFOldStylePList.c
    CFPlatform.c
    CFPlatformConverters.c
    CFPlugIn.c
    CFPreferences.c
    CFPropertyList.c
    CFRegularExpression.c
    CFRelativeDateTimeFormatter.c
    CFRunArray.c
    CFRunLoop.c
    CFRuntime.c
    CFSet.c
    CFSocket.c
    CFSocketStream.c
    CFSortFunctions.c
    CFStorage.c
    CFStream.c
    CFString.c
    CFStringEncodingConverter.c
    CFStringEncodingDatabase.c
    CFStringEncodings.c
    CFStringScanner.c
    CFStringTransform.c
    CFStringUtilities.c
    CFSystemDirectories.c
    CFTimeZone.c
    CFTimeZone_WindowsMapping.c
    CFTree.c
    CFUniChar.c
    CFUnicodeDecomposition.c
    CFUnicodePrecomposition.c
    CFURL.c
    CFURLAccess.c
    CFURLComponents_URIParser.c
    CFURLComponents.c
    CFUtilities.c
    CFUUID.c
    CFWindowsUtilities.c
    CFXMLPreferencesDomain.c
    uuid.c)
add_library(CoreFoundation::CoreFoundation ALIAS CoreFoundation)

set_target_properties(CoreFoundation PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    C_VISIBILITY_PRESET default)

# The headers live flat in include/ and include each other with quotes. So
# that consumers can write <CoreFoundation/CFBase.h> against the build tree as
# well as against an installed copy, mirror include/ as <build>/include/CoreFoundation.
set(CF_BUILD_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")
file(MAKE_DIRECTORY "${CF_BUILD_INCLUDE_DIR}")
if(CMAKE_HOST_WIN32)
    file(COPY include/ DESTINATION "${CF_BUILD_INCLUDE_DIR}/CoreFoundation")
elseif(NOT IS_SYMLINK "${CF_BUILD_INCLUDE_DIR}/CoreFoundation")
    file(CREATE_LINK "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CF_BUILD_INCLUDE_DIR}/CoreFoundation" SYMBOLIC)
endif()

target_include_directories(CoreFoundation
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CF_BUILD_INCLUDE_DIR}>
        # Lets consumers write either <CoreFoundation/CFBase.h> or "CFBase.h";
        # the headers themselves use the quoted form.
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/CoreFoundation>
    PRIVATE
        internalInclude)

cf_apply_common_c_settings(CoreFoundation)

if(WIN32 AND NOT BUILD_SHARED_LIBS)
    # Ensure __CFInitialize runs even when statically linked into an executable.
    target_compile_definitions(CoreFoundation PRIVATE CF_WINDOWS_EXECUTABLE_INITIALIZER)
endif()

# ICU: forward the sources' <_foundation_unicode/...> includes to the real ICU.
if(CF_ICU_PROVIDER STREQUAL "system")
    cf_generate_icu_shim(CoreFoundation "${CF_ICU_INCLUDE_DIR}")
endif()

# Blocks runtime: system library, or the vendored copy compiled into CF. The
# vendored runtime is plain C and is built as its own object library so it does
# not see CF's prefix header or compile settings.
if(CF_USE_VENDORED_BLOCKSRUNTIME)
    add_library(CFBlocksRuntime OBJECT
        BlockRuntime/data.c
        BlockRuntime/runtime.c)
    target_include_directories(CFBlocksRuntime PRIVATE BlockRuntime/include include)
    target_compile_options(CFBlocksRuntime PRIVATE -fblocks)
    target_sources(CoreFoundation PRIVATE $<TARGET_OBJECTS:CFBlocksRuntime>)
    target_include_directories(CoreFoundation PRIVATE BlockRuntime/include)
endif()

target_link_libraries(CoreFoundation
    PRIVATE
        CF::ICU
        CF::dispatch
        Threads::Threads
        ${CMAKE_DL_LIBS})
if(NOT CF_USE_VENDORED_BLOCKSRUNTIME)
    target_link_libraries(CoreFoundation PRIVATE CF::BlocksRuntime)
endif()

if(NOT WIN32)
    target_link_libraries(CoreFoundation PRIVATE m)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # clock_gettime lives in librt on glibc older than 2.17.
    include(CheckSymbolExists)
    check_symbol_exists(clock_gettime "time.h" CF_HAVE_CLOCK_GETTIME_IN_LIBC)
    if(NOT CF_HAVE_CLOCK_GETTIME_IN_LIBC)
        target_link_libraries(CoreFoundation PRIVATE rt)
    endif()
endif()
if(WIN32)
    # SHGetKnownFolderPath requires shell32; CoTaskMemFree requires ole32.
    target_link_libraries(CoreFoundation PRIVATE shell32 ole32 AdvAPI32 Secur32 User32 mincore pathcch)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    # CFLog calls __android_log_print, which lives in liblog.
    target_link_libraries(CoreFoundation PRIVATE log)
endif()

if(CF_INSTALL)
    install(TARGETS CoreFoundation
        EXPORT CoreFoundationTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/CoreFoundation
        FILES_MATCHING PATTERN "*.h" PATTERN "*.modulemap" PATTERN "*.map")
endif()

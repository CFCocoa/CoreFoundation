# Plan: CoreFoundation as a standalone C library (CMake)

Status: Phase 1 is implemented on this branch (see section 7). The findings in
section 1 describe the tree as it was before that work.

Goal: make `Sources/CoreFoundation` (plus, optionally, `_CFXMLInterface` and
`_CFURLSessionInterface`) buildable and installable as a plain C library with
CMake, with no Swift toolchain involved, so that other projects can
`find_package()` it, `FetchContent` it, or vendor it as a subdirectory.
Packaging for distributions is explicitly out of scope; consumers provide the
system dependencies (ICU, libdispatch, BlocksRuntime, optionally libxml2 and
curl) themselves.

This document records what the tree looks like today (as of upstream
`main` at `5674de5`, 2026-10-30), what was verified by prototype compiles in a
clean Ubuntu 24.04 container, the source changes that are needed, the CMake
design, and a phased roadmap.

---

## 1. Findings: state of the tree today

### 1.1 Layout and the current CMake build

* All of CF is flat under `Sources/CoreFoundation/` (86 `.c` files, public
  headers in `include/`, private headers in `internalInclude/`). The 5.10.1
  `*.subproj` layout is gone, but file-for-file the content is the same
  library (see 1.6).
* The root `CMakeLists.txt` is a *Foundation* (Swift) build: it declares
  `LANGUAGES C Swift`, `FetchContent`s `swift-foundation` and
  `swift-foundation-icu`, queries the Swift compiler for the module triple, and
  links `CoreFoundation` against `_FoundationICU` and `dispatch`. It cannot be
  configured without a Swift toolchain.
* `Sources/CoreFoundation/CMakeLists.txt` only builds a `STATIC` library, has
  no install rules for the shared case, and installs headers to
  `lib/swift/CoreFoundation` (a toolchain layout, not a C library layout).
* `Sources/_CFURLSessionInterface/CMakeLists.txt` references
  `${CMAKE_SOURCE_DIR}`; this breaks as soon as CF is pulled in via
  `add_subdirectory()`/`FetchContent`.
* There is no CI for CF (only two automerge workflows) and no C tests at all
  (`Tests/` is Swift only).

### 1.2 The Swift runtime mode is forced on, and it cannot work without Swift

Since `a9ffa779` (2024-05-07, "CoreFoundation modularization and packaging
improvements") both `include/CFAvailability.h` and
`internalInclude/CoreFoundation_Prefix.h` contain:

```c
#ifndef DEPLOYMENT_RUNTIME_SWIFT
#define DEPLOYMENT_RUNTIME_SWIFT 1
#endif
```

In 5.10.1 this define lived only in the Swift-only umbrella
(`Base.subproj/SwiftRuntime/CoreFoundation.h`); the plain
`Base.subproj/CoreFoundation.h` and the CMake option `CF_DEPLOYMENT_SWIFT`
(default `NO`, which defined `DEPLOYMENT_RUNTIME_C`) selected the C mode.
`DEPLOYMENT_RUNTIME_C` has zero mentions in the tree today.

In Swift mode the runtime is not usable from C, by design (`CFRuntime.c`):

| Function | Swift-mode behaviour |
|---|---|
| `CFRetain` / `CFRelease` | call `swift_retain` / `swift_release` unconditionally |
| `_CFRuntimeCreateInstance` | allocates via `swift_allocObject` with the isa from `__CFRuntimeObjCClassTable` |
| `__CFInitialize` | calls `__CFSwiftGetBaseClass()` and `__CFInitializeSwift()` (both provided by Swift Foundation) |
| `CFRuntimeBase` | 24 bytes (`_cfisa`, `_swift_rc`, `_cfinfoa`) instead of 16 |
| constant `CFSTR()` | laid out for `-fcf-runtime-abi=swift`, class pointer is `$s10Foundation19_NSCFConstantStringCN` |

The good news: every one of these sites still has its `#else` (C) branch, and
`CF_IS_SWIFT` / `CF_SWIFT_FUNCDISPATCHV` collapse to `0` / no-op when
`DEPLOYMENT_RUNTIME_SWIFT` is 0 (`CFInternal.h:856-870`). There are 91
`DEPLOYMENT_RUNTIME_SWIFT` mentions today vs 103 in 5.10.1; nothing of the C
path was deleted, it just stopped being exercised.

### 1.3 Prototype: the C mode is one small patch away from compiling

All 86 `.c` files were compiled with clang 18 on Ubuntu 24.04 using
`-DDEPLOYMENT_RUNTIME_SWIFT=0`, the flags from `Package.swift` minus
`-fcf-runtime-abi=swift`, system ICU 74 headers (mapped to the
`_foundation_unicode/` include prefix with a symlink shim), the
swift-corelibs-libdispatch headers, and `/usr/include/Block.h`.

Result:

| Pass | Files failing | Cause |
|---|---|---|
| A: as-is | 5 (`CFPlatform.c`, `CFRunLoop.c`, `CFRuntime.c`, `CFSocket.c`, `CFStream.c`) | `_CFThreadRef`, `_CFThreadSpecificKey`, `_CFIsMainThread`, `_CFThreadSetName` are declared in `ForSwiftFoundationOnly.h`, which `CFInternal.h` includes only `#if DEPLOYMENT_RUNTIME_SWIFT` |
| B: A + make that header visible | 1 (`CFRuntime.c:1719`) | `__kCFAllocatorTypeID_CONST` is undefined; should be `_kCFRuntimeIDCFAllocator`. This bug is already present in 5.10.1, so the C path was bit-rotted there too |
| C: B + that one-token fix | 0 | archive builds, 3.8 MB static library |

Unresolved symbols in the resulting archive, by family (`nm -u`):

| Family | Count | Notes |
|---|---|---|
| libdispatch | 21 | includes the SPI `_dispatch_get_main_queue_port_4CF`, `_dispatch_main_queue_callback_4CF`, `_dispatch_source_type_timer`, `libdispatch_init` (`dispatch/private.h`). Any swift-corelibs-libdispatch build provides these |
| ICU | 175 | all satisfied by stock ICU 74 (`u*_74`). Apple-ICU-only headers (`ualoc.h`, `uameasureformat.h`) are already guarded by `__HAS_APPLE_ICU__` / `__has_include` |
| BlocksRuntime | 6 | `_Block_copy`, `_Block_release`, `_Block_object_assign/dispose`, `_NSConcreteGlobalBlock`, `_NSConcreteStackBlock` |
| CF symbols moved out of C | 3 | `CFCopyUserName` and `_CFGetCurrentDirectory` are now `@_cdecl` functions in `Sources/Foundation/NSPathUtilities.swift` and `FileManager.swift`; `_CFThreadSetName` is defined inside the Swift-only block of `CFPlatform.c` |
| libc / pthread / dl / m / rt | rest | `epoll`, `eventfd`, `timerfd`, `dlopen`, `pow`, `clock_gettime`, ... |

`__CFConstantStringClassReference` is defined in C mode, so consumer
`CFSTR()` literals link. No libxml2 or curl symbol is referenced by CF proper.

Both interface libraries also compile in C mode against system
libxml2 2.9.14 and curl 8.5 headers (one benign `const` warning in
`CFURLSessionInterface.c:217`).

### 1.4 ABI hazard for consumers

Because the headers default to Swift mode, a consumer that includes
`CoreFoundation.h` without defining anything sees a different object layout
than the library was built with:

```
default flags:                 DEPLOYMENT_RUNTIME_SWIFT=1 sizeof(CFRuntimeBase)=24 offsetof(_cfinfoa)=16
-DDEPLOYMENT_RUNTIME_SWIFT=0:  DEPLOYMENT_RUNTIME_SWIFT=0 sizeof(CFRuntimeBase)=16 offsetof(_cfinfoa)=8
```

This matters for anyone using `CFRuntime.h` (custom CF types), `CFSTR()`
(constant string layout in `CFString.h`), and `CF_STRING_ENUM`-style macros in
`CFAvailability.h`. The installed headers must therefore default to the mode the
library was built in (see S1).

### 1.5 The two interface libraries are Swift import shims

* `_CFXMLInterface` (1.6k lines): its parser setup, external entity loader and
  every SAX callback route through `__CFSwiftXMLParserBridge`, and all of that
  is `#if DEPLOYMENT_RUNTIME_SWIFT`. In C mode what remains is a thin set of
  libxml2 wrappers with no callback mechanism. A C user would use libxml2
  directly.
* `_CFURLSessionInterface` (0.8k lines): by its own header comment, "wrappers /
  helpers to import libcurl into Swift". Usable from C, but adds nothing over
  curl itself.

Conclusion: build them only behind options that default to OFF, so libxml2 and
curl are never required by default (matches the request), and document them as
"for Swift Foundation compatibility".

### 1.6 What changed in CF since swift-5.10.1-RELEASE

190 commits touched CF. Public-header API surface (every `CF_EXPORT`
declaration) compared between the two trees: 2429 → 2422 declarations, 27
removed, 20 added.

Removed:

* `CFDateFormatterCreateISO8601Formatter` and the `CFISO8601DateFormatOptions`
  enum (`976bf1ae`, 2026-09-12, "Recore the ISO8601DateFormatter type on top of
  FoundationEssentials"). This is the one real "Swift has it, so delete from
  CF" loss of C API; the 5.10.1 implementation can be restored (S12).
* `_CFMainPThread` as a public declaration (still defined and exported from
  `CFRuntime.c`; the declaration moved to `ForSwiftFoundationOnly.h`).
* `CFMessagePortSetDispatchQueue`, `_CFBundleSetupXPCBootstrap*`: Mach/XPC
  only, never built on Linux.
* `_NSIsNS*`, `_NSTaggedPointerString*`, `_NS_getcwd`, `__CFSwiftBridge`,
  `__CFSwiftXMLParserBridge`, `__NSCFXMLBridge*`: Swift bridge plumbing moved
  between headers, not C API.

Removed source files: `CFMachPort.c`, `CFMachPort_Lifetime.c`,
`CFMessagePort.c`, `CFUserNotification.c` (all Darwin-only; the 5.10.1 CMake
already did not compile them on non-Darwin), the `.S` Unicode data assembly
files and their `.data`/`.mapping` blobs, `CoreFoundation.rc`, the Windows
Olson plists, `build.py`.

Added: `_CFCalendar*V` enumerate/compose helpers, `_CFPosixSpawn*` helpers,
`CFTimeZone_WindowsMapping.c`, the vendored `uuid.c`, the vendored
`BlockRuntime/` (used for WASI), `CFConstantKeys.h`, and the Unicode tables as
C arrays (`internalInclude/*.inc.h`).

Net effect: the C library is essentially intact. The regression is the build
system and the forced Swift mode, not the code. The move from `.S` data blobs
to `.inc.h` C arrays is actually a portability win (no `ASM` language, no
section tricks, no runtime data files).

### 1.7 Header hygiene issues found

* 7 public headers are not self-contained in C mode: `CFOverflow.h` (needs
  `<stddef.h>`), `CFLocking.h` (needs `CFBase.h`), `CFDateComponents.h` and
  `CFCalendarPriv.h` (need `CFCalendar.h`), `CFBundle_BinaryTypes.h` (needs
  `CFTargetConditionals.h`), `CFStringEncodingConverterPriv.h` and
  `CFStringEncodingDatabase.h` (use `CF_PRIVATE`, i.e. they are internal
  headers living in `include/`).
* `CoreFoundation.h` unconditionally includes `ForSwiftFoundationOnly.h`,
  which drags the Swift bridge structs into every C consumer.
* All includes are quoted (`#include "CFBase.h"`), never
  `<CoreFoundation/CFBase.h>`. Fine for installation into
  `include/CoreFoundation/` as long as consumers use the umbrella or add that
  directory to their include path; the package config should export both.
* `CF_EXPORT` expands to plain `extern` on ELF, so the library cannot be built
  with `-fvisibility=hidden` without first annotating exports (5.10.1 carried
  the same TODO).
* Vendored `uuid.c` defines unprefixed `uuid_generate*` etc. In a static
  library this will clash with a consumer that also links libuuid.

### 1.8 Dependency availability (checked on Ubuntu 24.04)

| Dependency | Ubuntu 24.04 package | Notes |
|---|---|---|
| ICU | `libicu-dev` (74.2) | works with stock ICU; `find_package(ICU COMPONENTS uc i18n data)` |
| libdispatch | **not packaged on Ubuntu 24.04** (`libdispatch-dev` exists on Debian) | swift-corelibs-libdispatch builds with plain CMake/clang and installs `dispatchConfig.cmake`; the CMake build should offer `CF_FETCH_LIBDISPATCH` as a fallback |
| BlocksRuntime | `libblocksruntime-dev` (`/usr/include/Block.h`) | libdispatch also ships one; CF has a vendored copy in `BlockRuntime/` that compiles on Linux |
| libxml2 | `libxml2-dev` | only for `_CFXMLInterface` |
| curl | `libcurl4-openssl-dev` | only for `_CFURLSessionInterface` |
| compiler | clang (Blocks required) | gcc has no Blocks support; `-fblocks` is mandatory |

---

## 2. Design decisions

1. **C runtime mode is the product.** The library is built with
   `DEPLOYMENT_RUNTIME_SWIFT=0` and without `-fcf-runtime-abi=swift`. The
   Swift mode is kept compiling behind `CF_SWIFT_RUNTIME` (default OFF) so
   that upstream syncs stay small; it is not a supported configuration.
2. **Installed headers must match the built mode.** The default in the headers
   is 0 (upstream's own Swift builds pass the define explicitly in both
   `Package.swift` and CMake, so they are unaffected). `CF_SWIFT_RUNTIME=ON`
   adds the define as a PUBLIC compile definition.
3. **Shared and static both supported**, `BUILD_SHARED_LIBS` controls it.
   Versioning starts at `1.0.0` with `SOVERSION 1`.
4. **No source rewrites for the ICU include prefix.** Sources use
   `<_foundation_unicode/*.h>`. A configure-time generator writes one-line
   forwarding headers into `<build>/icu-shim/_foundation_unicode/` for every
   header that exists in the ICU being used, so the sources'
   `__has_include` checks for Apple-only additions keep working.
   `CF_ICU_PROVIDER=foundation-icu` builds against a swift-foundation-icu
   checkout instead (untested here).
5. **Interface libraries are opt-in.** `CF_BUILD_XML_INTERFACE` and
   `CF_BUILD_URLSESSION_INTERFACE` default OFF.
6. **No upstreaming.** Upstream has moved on to Swift-only implementations
   and keeps removing C code that Swift duplicates; patches here are kept
   small so that syncing stays cheap, not because they are expected to land
   upstream.
7. **Repository trimmed to CF.** Only `Sources/CoreFoundation`,
   `Sources/_CFXMLInterface` and `Sources/_CFURLSessionInterface` are kept
   from upstream. Syncing uses path-limited `git format-patch`/`git am`, never
   `git merge` (see `Docs/UPSTREAM.md` and `Tools/sync-upstream.sh`).
8. **macOS is out of scope.** Programs on macOS use the system
   CoreFoundation; this library is for platforms that have none.
9. **The vendored `uuid.c` stays.** It is Apple's uuid library with the Swift
   team's Windows, WASI and Linux ports, not a copy of libuuid. Its symbols are
   given hidden visibility so they cannot interpose a libuuid loaded in the
   same process; `CFUUID.c`'s `_cf_uuid_*` wrappers remain the external API.

---

## 3. Source changes

All of these are in place on this branch unless marked otherwise. Each one is
a bug fix in the C path or a guard change; none alters Swift-mode behaviour.

| # | File(s) | Change | Why |
|---|---|---|---|
| S1 | `include/CFAvailability.h`, `internalInclude/CoreFoundation_Prefix.h` | `DEPLOYMENT_RUNTIME_SWIFT` defaults to 0 instead of 1. | 1.2, 1.4 |
| S2 | `internalInclude/CFInternal.h` | `ForSwiftFoundationOnly.h` is included in both runtime modes; it declares the thread, spawn and uuid helpers CF itself uses. | 1.3 pass A |
| S3 | `CFPlatform.c` | The block of platform helpers is no longer `#if DEPLOYMENT_RUNTIME_SWIFT`; only the thread-specific retain/release inside it is, via `__CFThreadSpecificRetain/Release` macros that map to `swift_retain/release` or `CFRetain/CFRelease`. | `_CFThreadSetName` etc. needed by `CFStream.c` |
| S4 | `CFRuntime.c` | `__kCFAllocatorTypeID_CONST` → `_kCFRuntimeIDCFAllocator` in `_CFRelease`. | compile error in the C path, also broken in 5.10.1 |
| S5 | `CFPlatform.c` | C implementations of `CFCopyUserName` (`getpwuid_r`, `GetUserNameW` on Windows) and `_CFGetCurrentDirectory` under `#if !DEPLOYMENT_RUNTIME_SWIFT`; Swift mode keeps the `@_cdecl` providers. | 3 unresolved symbols |
| S6 | `include/CoreFoundation.h` | Includes `ForSwiftFoundationOnly.h` only `#if DEPLOYMENT_RUNTIME_SWIFT`, so C consumers do not see the Swift bridge structs (and compile without `-fblocks`). | 1.7 |
| S7 | `include/CFCalendarPriv.h`, `include/ForSwiftFoundationOnly.h` | Block-typed declarations guarded by `#if __BLOCKS__`, as the rest of the public headers already are. | consumers without `-fblocks` |
| S8 | `internalInclude/uuid.h` | Vendored `uuid_*` functions get hidden visibility. | libuuid interposition |
| S9 | the other headers in 1.7 | Missing includes in `CFOverflow.h`, `CFLocking.h`, `CFDateComponents.h`, `CFBundle_BinaryTypes.h`; include guards for `CFPropertyList_Private.h` and `CFConstantKeys.h`; `CFStringEncodingConverterPriv.h` and `CFStringEncodingDatabase.h` moved to `internalInclude/`. | header self-containment, enforced by `Tests/C` |
| S10 | `include/CFBase.h` | `CF_EXPORT` with `visibility("default")` on ELF so the library can use `-fvisibility=hidden`. **Not done yet** (phase 3). | smaller export table |
| S11 | `include/CFDateFormatter.h`, `CFDateFormatter.c` | Restore `CFISO8601DateFormatOptions` and `CFDateFormatterCreateISO8601Formatter` from 5.10.1. **Not done yet** (phase 3). | only C API lost since 5.10.1 |
| S13 | `CFBasicHash.c`, `CFArray.c`, `CFTree.c`, `CFPropertyList.c` | Callbacks are invoked through the public collection callback signatures (`const void *`), and the internal sort comparators have exactly the `CFComparatorFunction` type, so `-fsanitize=function` reports nothing in CF. | clean sanitizer runs |
| S12 | `CFRuntime.c` | No change. Note that on Linux CF calls `libdispatch_init()` explicitly, which is why swift-corelibs-libdispatch (not Apple's upstream libdispatch) is required. | documentation |

---

## 4. CMake design

### 4.1 File layout (as implemented)

```
CMakeLists.txt                      # project(CoreFoundation VERSION 1.0.0 LANGUAGES C), options, deps, install
cmake/
  CFFindDependencies.cmake          # CF::ICU, CF::dispatch, CF::BlocksRuntime wrappers; also installed
  CFIcuShim.cmake                   # generates <build>/icu-shim/_foundation_unicode/*.h
  CFCommonSettings.cmake            # feature checks + the flag list shared by the three C targets
  FindBlocksRuntime.cmake           # finds Block.h / libBlocksRuntime; also installed
  CoreFoundationConfig.cmake.in     # package config; re-finds deps for static builds only
  CoreFoundation.pc.in
Sources/CMakeLists.txt
Sources/CoreFoundation/CMakeLists.txt
Sources/_CFXMLInterface/CMakeLists.txt
Sources/_CFURLSessionInterface/CMakeLists.txt
Tests/C/{CMakeLists.txt,smoke.c,headers.c}
Tools/sync-upstream.sh
Docs/{StandaloneCBuildPlan.md,UPSTREAM.md}
```

### 4.2 Options

| Option | Default | Effect |
|---|---|---|
| `BUILD_SHARED_LIBS` | ON | shared `libCoreFoundation.so.1` vs static |
| `CF_BUILD_XML_INTERFACE` | OFF | build `_CFXMLInterface`, requires LibXml2 |
| `CF_BUILD_URLSESSION_INTERFACE` | OFF | build `_CFURLSessionInterface`, requires CURL |
| `CF_SWIFT_RUNTIME` | OFF | `DEPLOYMENT_RUNTIME_SWIFT=1` + `-fcf-runtime-abi=swift`; unsupported |
| `CF_ICU_PROVIDER` | `system` | `system` uses `find_package(ICU)`; `foundation-icu` expects `CF_FOUNDATION_ICU_DIR` |
| `CF_FETCH_LIBDISPATCH` | OFF | `FetchContent` swift-corelibs-libdispatch (`CF_LIBDISPATCH_GIT_TAG`) when no `dispatch` package is found |
| `CF_DISPATCH_ROOT` | | prefix of a libdispatch install without a CMake package config; the Swift toolchain layout is also searched |
| `CF_BLOCKS_RUNTIME` | `auto` | `auto` (system, else vendored), `system`, `vendored` |
| `CF_BUILD_TESTS` | ON when top-level | adds the C tests to CTest |
| `CF_INSTALL` | ON when top-level | install rules and package config |
| `CF_ENABLE_WERROR` | OFF | |

### 4.3 Targets and usage

* Library target `CoreFoundation`, alias `CoreFoundation::CoreFoundation`.
  Output name `CoreFoundation`, `VERSION 1.0.0`, `SOVERSION 1`,
  `C_VISIBILITY_PRESET hidden` once S10 is in.
* Public include dirs: `$<BUILD_INTERFACE:.../include>`,
  `$<INSTALL_INTERFACE:include>` and `include/CoreFoundation` (so both
  `#include <CoreFoundation/CoreFoundation.h>` and the quoted internal style
  work).
* No public compile definitions in C mode (the headers default to it).
  `-fblocks` is not forced on consumers: every block-taking declaration in
  the public headers is guarded by `#if __BLOCKS__`, so a consumer compiled
  without Blocks simply does not see them. Consumers that write blocks link
  the Blocks runtime themselves.
* Private: `CF_BUILDING_CF`, `HAVE_STRUCT_TIMESPEC`, `_GNU_SOURCE` on
  Linux/Android, `HAVE_STRLCAT`/`HAVE_STRLCPY`/`HAVE_ISSETUGID`/
  `HAVE_SCHED_GETAFFINITY` from `check_symbol_exists`, the `-Wno-*` list,
  `-fconstant-cfstrings -fdollars-in-identifiers -fno-common -fexceptions`,
  prefix header `CoreFoundation_Prefix.h` via `target_precompile_headers` (or
  `-include` for non-Ninja generators).
* Link: `ICU::uc ICU::i18n ICU::data` (PRIVATE for shared, PUBLIC for static
  so transitive deps propagate), `dispatch`, `BlocksRuntime`,
  `Threads::Threads`, `${CMAKE_DL_LIBS}`, `m`, `rt` where present.
* Interface libraries, when enabled: link `CoreFoundation` PUBLIC plus
  `LibXml2::LibXml2` / `CURL::libcurl` PRIVATE.
* Install: `lib/libCoreFoundation.so*`, `include/CoreFoundation/*.h`
  (public headers only; `internalInclude/` is not installed),
  `include/CoreFoundation/module.modulemap`, `lib/cmake/CoreFoundation/`
  config + targets + version file, `lib/pkgconfig/CoreFoundation.pc`.

Consumer examples the README should carry:

```cmake
find_package(CoreFoundation 1.0 REQUIRED)
target_link_libraries(app PRIVATE CoreFoundation::CoreFoundation)
```

```cmake
include(FetchContent)
FetchContent_Declare(CoreFoundation GIT_REPOSITORY https://github.com/CFCocoa/CoreFoundation GIT_TAG v1.0.0)
set(CF_BUILD_TESTS OFF)
FetchContent_MakeAvailable(CoreFoundation)
target_link_libraries(app PRIVATE CoreFoundation::CoreFoundation)
```

### 4.4 Platform notes

* Linux (glibc, musl): primary target. Clang required (`-fblocks`).
* FreeBSD/OpenBSD: upstream keeps these building; keep the
  `CMAKE_SYSTEM_NAME` branches from the current root `CMakeLists.txt`
  (`-fexceptions` off on OpenBSD, `LINKER:-z,origin`).
* Android: link `log`; keep `CMAKE_HAVE_LIBC_PTHREAD`.
* Windows: `shell32 ole32`, `CF_WINDOWS_EXECUTABLE_INITIALIZER` for static,
  `_DLL`/`_WINDLL` as in 5.10.1. Second phase; needs a libdispatch build too.
* macOS: out of scope; programs there use the system CoreFoundation.
* WASI: no dispatch (`__HAS_DISPATCH__ 0`), vendored BlocksRuntime; keep the
  existing conditionals but do not test it in phase 1.

---

## 5. Testing and CI

* `Tests/C/smoke.c`: `__CFInitialize` via first CF call, `CFSTR` and
  `CFStringCreateWithCString`, `CFArray`/`CFDictionary`/`CFSet` with the
  standard callbacks, `CFNumber`/`CFDate`, `CFPropertyList` XML and binary
  round trip, `CFUUID`, `CFLocale`/`CFDateFormatter`/`CFNumberFormatter`
  (exercises ICU), `CFTimeZone` (exercises zoneinfo), `CFRunLoop` with a timer
  and a `CFRunLoopStop` (exercises dispatch main queue SPI), `CFBundleGetMainBundle`,
  `CFCopyUserName` and the current-directory URL (S5), `CFRetain`/`CFRelease`
  of a custom `CFRuntimeClass` (exercises S4 and the C allocator path).
* `Tests/C/headers/`: one TU per installed public header, compiled with
  `-fsyntax-only -Wall`, to keep S9 honest.
* Consumer tests: an `ExternalProject` that installs CF to a prefix and
  builds a tiny app with `find_package`; another that uses `FetchContent`.
* CI (GitHub Actions): Ubuntu 22.04 and 24.04 with `CF_FETCH_LIBDISPATCH=ON`,
  Debian container with `libdispatch-dev`, Fedora container, Alpine (musl)
  container; shared and static; one ASan+UBSan job; one job with
  `CF_BUILD_XML_INTERFACE=ON CF_BUILD_URLSESSION_INTERFACE=ON`. FreeBSD via a
  VM action later.

---

## 6. Upstream sync strategy

See `Docs/UPSTREAM.md`. The base commit is recorded there and
`Tools/sync-upstream.sh` imports upstream commits as patches limited to the
three source directories. Upstream churn in CF is modest (190 commits in
roughly 2.5 years, almost all small fixes), so conflicts will be rare and
confined to the regions touched by S1-S8.

---

## 7. Roadmap

Phase 0, groundwork: done.

Phase 1, "it builds and links": **implemented on this branch.** S1-S8, the
repository trimmed to CF, the new CMake build with ICU shim, dispatch and
BlocksRuntime detection, shared and static libraries, install rules, package
config and pkg-config file, `Tests/C/smoke.c` and `Tests/C/headers.c`, and a
GitHub Actions workflow.

Verified in a clean Ubuntu 24.04 container with clang 18, ICU 74, system
BlocksRuntime, libxml2 2.9, curl 8.5 and swift-corelibs-libdispatch
swift-6.1-RELEASE built in-tree through `CF_FETCH_LIBDISPATCH=ON`:

* shared Release, static Release with both interface libraries, Debug with the
  vendored Blocks runtime, and an AddressSanitizer + UndefinedBehaviorSanitizer
  Debug build all compile with zero warnings in CF proper, and
  `CF_SWIFT_RUNTIME=ON` still compiles and links the library;
* `ctest` passes in every configuration: `cf_smoke` (58 checks covering the
  runtime, collections, property lists, ICU formatters, time zones, URLs,
  UUIDs, preferences, the run loop and the restored helpers) and
  `cf_headers`, with no sanitizer findings;
* the shared library's undefined symbols are only libdispatch, ICU,
  BlocksRuntime and libc, and no `uuid_*` symbol is exported;
* install + `find_package(CoreFoundation)` from a separate project builds and
  runs a consumer against both the shared and the static install (the static
  consumer has no runtime dependency on CoreFoundation or libdispatch), and
  the pkg-config file resolves with and without `--static`.

The GitHub Actions workflow runs the shared, static and vendored-Blocks
configurations plus the Swift-mode compile check on every push; each job
fetches and builds libdispatch, builds, tests, installs and builds and runs a
consumer project in under a minute.

Sanitizer note: clang's `-fsanitize=undefined` includes `-fsanitize=function`
since clang 17. CF itself is clean under it after S13; libdispatch's own
vtable dispatch is not, so a sanitizer build that also instruments libdispatch
needs `-fno-sanitize=function` (the CI job does this).

Phase 2, "it is a good citizen": **implemented on this branch.** S9 and S13;
a generated compile check for every public header, with and without Blocks
(`Tests/C`, part of `all` and of `ctest`); CI extended with an ASan+UBSan job,
a FetchContent consumer job, Debian bookworm (packaged libdispatch), Fedora
(packaged libdispatch) and Alpine/musl (fetched libdispatch, vendored Blocks)
container jobs. Verified locally: shared, static with interface libraries,
vendored Blocks and sanitizer configurations pass all tests, and a consumer
that vendors CoreFoundation with FetchContent builds and runs.

Phase 3, "polish": S10 hidden visibility with an exports audit, S11 ISO8601
restore, Windows and FreeBSD jobs, release tagging, first upstream sync with
`Tools/sync-upstream.sh`.

---

## 8. Decisions taken

1. Repository trimmed to CF only.
2. `CF_SWIFT_RUNTIME` kept as a compile-only option.
3. Versioning starts at 1.0.0.
4. macOS is out of scope.
5. The vendored `uuid.c` stays, with hidden symbols.
6. Nothing is sent upstream.

# Plan: CoreFoundation as a standalone C library (CMake)

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
   Swift mode is kept compiling behind an option (`CF_SWIFT_RUNTIME`, default
   OFF) purely to keep the fork mergeable with upstream, not as a supported
   configuration.
2. **Installed headers must match the built mode.** The default in the headers
   flips to 0 (upstream Swift builds already pass the define explicitly in
   both `Package.swift` and CMake), and the install step also writes a
   `CFBuildConfig.h` (or a `#define` into the installed `CFAvailability.h`)
   that pins the mode the binary was built with.
3. **Shared and static both supported**, `BUILD_SHARED_LIBS` controls it. A
   `SOVERSION` is needed; propose `1338` from the 5.10.1 `project(VERSION)`,
   or restart at `1.0`.
4. **No source rewrites for the ICU include prefix.** Sources use
   `<_foundation_unicode/*.h>` (25 headers). Instead of sed-ing them to
   `<unicode/*.h>` (a diff that touches 14 files and fights every upstream
   sync), generate a shim directory at configure time: for each header name
   found by `grep` in the sources, write
   `${build}/icu-shim/_foundation_unicode/<name>.h` containing
   `#include <unicode/<name>.h>`, and add `icu-shim` as a private include dir.
   The same mechanism lets `CF_ICU_PROVIDER=foundation-icu` point at a
   swift-foundation-icu checkout if someone wants Apple ICU behaviour.
5. **Interface libraries are opt-in.** `CF_BUILD_XML_INTERFACE` and
   `CF_BUILD_URLSESSION_INTERFACE` default OFF; when ON they
   `find_package(LibXml2 REQUIRED)` / `find_package(CURL REQUIRED)`.
6. **Minimal, upstreamable source patches.** Every change in section 3 is
   either a bug fix in the C path or a guard change; none alters Swift-mode
   behaviour. Several are worth sending upstream, which shrinks the fork's
   delta over time.
7. **Repository shape.** Two options:
   * A: keep the full upstream tree and only add the CMake files and the
     patches. Upstream merges stay trivial, but the repo carries ~3 MB of
     Swift sources that this fork never builds.
   * B (recommended): trim to `Sources/CoreFoundation`,
     `Sources/_CFXMLInterface`, `Sources/_CFURLSessionInterface`, `LICENSE`,
     docs. Sync from upstream with path-filtered patches
     (`git format-patch <last>..upstream/main -- Sources/CoreFoundation
     Sources/_CF*` then `git am -3`), never with `git merge`, so the deleted
     Swift paths never produce modify/delete conflicts. A tiny
     `Tools/sync-upstream.sh` records the last synced upstream SHA.

---

## 3. Source changes required

Ordered by necessity. "Upstreamable" marks changes that make sense in
swift-corelibs-foundation itself.

| # | File(s) | Change | Why | Upstreamable |
|---|---|---|---|---|
| S1 | `include/CFAvailability.h`, `internalInclude/CoreFoundation_Prefix.h` | `#ifndef DEPLOYMENT_RUNTIME_SWIFT` → `#define DEPLOYMENT_RUNTIME_SWIFT 0` (or drop the fallback and let the build system define it). Install step pins the built mode. | 1.2, 1.4 | no (fork policy) |
| S2 | `internalInclude/CFInternal.h` | Include the thread/platform declarations unconditionally. Minimal form: include `ForSwiftFoundationOnly.h` outside the `#if DEPLOYMENT_RUNTIME_SWIFT` (proven sufficient by pass B). Cleaner form: move `_CFThreadRef`/`_CFThreadAttributes`/`_CFThreadSpecificKey`, `_CFIsMainThread`, `_CFMainPThread`, `_CFThread*`, `_CFEnviron`, `_CFOpenFile*`, `_CFReallocf`, `_CFPosixSpawn*`, `_cf_uuid_*`, `__CFHashDouble`, `CFSortIndexes` into a new `CFPlatformPriv.h` that both `CFInternal.h` and `ForSwiftFoundationOnly.h` include. | 1.3 pass A | yes (cleaner form) |
| S3 | `CFPlatform.c` lines 1648-2443 | Split the `#if DEPLOYMENT_RUNTIME_SWIFT` block. Thread-specific get/set keep `swift_retain`/`swift_release` under Swift and use `CFRetain`/`CFRelease` otherwise; `_CFThreadCreate`, `_CFThreadSetName`, `_CFThreadGetName`, `_CFEnviron`, `_CFOpenFile*`, `_CFReallocf`, `_CFPosixSpawn*` become unconditional. Required for `CFStream.c` to link (`_CFThreadSetName`). | 1.3 | yes |
| S4 | `CFRuntime.c:1719` | `__kCFAllocatorTypeID_CONST` → `_kCFRuntimeIDCFAllocator`. | compile error in C path, also broken in 5.10.1 | yes |
| S5 | `CFPlatform.c` | Restore C implementations guarded `#if !DEPLOYMENT_RUNTIME_SWIFT`: `CFCopyUserName` (use the `getpwuid_r` form from `5674de5`, Windows via `GetUserNameW`) and `_CFGetCurrentDirectory` (`getcwd`, Windows `GetCurrentDirectoryW`). Swift mode keeps the `@_cdecl` providers. | 3 unresolved symbols | yes |
| S6 | `include/CoreFoundation.h` | Include `ForSwiftFoundationOnly.h` only `#if DEPLOYMENT_RUNTIME_SWIFT`; include the new `CFPlatformPriv.h` (from S2) so `_CFThreadRef` etc. stay available to C consumers that used them. | 1.7 | yes |
| S7 | `Sources/_CFURLSessionInterface/CMakeLists.txt` | `${CMAKE_SOURCE_DIR}` → `${CMAKE_CURRENT_SOURCE_DIR}/../CoreFoundation`. | vendoring via `add_subdirectory` | yes |
| S8 | `uuid.c`, `internalInclude/uuid.h` | Give the vendored `uuid_*` functions hidden visibility (or `#define uuid_generate __CFuuid_generate` style renames in `uuid.h`). `CFUUID.c` already exposes `_cf_uuid_*` wrappers for external use. | libuuid clash in static builds | yes |
| S9 | the 7 headers in 1.7 | Add the missing includes; move `CFStringEncodingConverterPriv.h` and `CFStringEncodingDatabase.h` to `internalInclude/` (check no Swift importer dependency first; `grep` shows they are not in the module map umbrella list, but the umbrella is `CoreFoundation.h`, so verify). | header self-containment, enables a CI header check | yes |
| S10 | `include/CFBase.h` | On ELF/Mach-O define `CF_EXPORT` as `extern __attribute__((visibility("default")))` so the library can be built with `-fvisibility=hidden`. Internal `__CF*` symbols that Swift Foundation reaches for are already `CF_EXPORT`/`CF_CROSS_PLATFORM_EXPORT`. | smaller export table, fewer clashes | yes |
| S11 | `include/CFDateFormatter.h`, `CFDateFormatter.c` | Optionally restore `CFISO8601DateFormatOptions` and `CFDateFormatterCreateISO8601Formatter` from 5.10.1 under `#if !DEPLOYMENT_RUNTIME_SWIFT` (or unconditionally; the Swift side no longer calls it). | only C API lost since 5.10.1 | fork-only unless upstream agrees |
| S12 | `CFRuntime.c` (`__CFInitialize`) | Nothing to change, but note: on Linux CF calls `libdispatch_init()` explicitly before anything else (`__HAS_DISPATCH__ && !TARGET_OS_MAC`). This is why a swift-corelibs-libdispatch (not Apple's upstream libdispatch) is required; document it. | documentation | n/a |

Everything else (CFRunLoop `_counterpart`, `_CFSwiftRetain/Release`, the
bridge tables, `CFLog1`) is already correctly guarded.

---

## 4. CMake design

### 4.1 File layout (after trimming, option B)

```
CMakeLists.txt                      # project(CoreFoundation VERSION 1338.0.0 LANGUAGES C)
cmake/
  CoreFoundationConfig.cmake.in     # find_dependency(ICU ...), find_dependency(dispatch) ...
  CoreFoundation.pc.in
  FindBlocksRuntime.cmake           # borrowed from libdispatch (BSD/Apache compatible)
  FindLibDispatch.cmake             # CONFIG first, then find_path/find_library fallback
  CFIcuShim.cmake                   # generates icu-shim/_foundation_unicode/*.h
  CFCompilerFlags.cmake             # the flag list from Package.swift, minus Swift-specific ones
Sources/CoreFoundation/CMakeLists.txt
Sources/CoreFoundation/BlockRuntime/CMakeLists.txt   # already exists; reused for CF_USE_VENDORED_BLOCKSRUNTIME
Sources/_CFXMLInterface/CMakeLists.txt
Sources/_CFURLSessionInterface/CMakeLists.txt
Tests/C/                            # smoke tests, see section 5
Docs/
```

### 4.2 Options

| Option | Default | Effect |
|---|---|---|
| `BUILD_SHARED_LIBS` | ON | shared `libCoreFoundation.so.1338` vs static |
| `CF_BUILD_XML_INTERFACE` | OFF | build `_CFXMLInterface`, requires LibXml2 |
| `CF_BUILD_URLSESSION_INTERFACE` | OFF | build `_CFURLSessionInterface`, requires CURL |
| `CF_SWIFT_RUNTIME` | OFF | `DEPLOYMENT_RUNTIME_SWIFT=1` + `-fcf-runtime-abi=swift`; unsupported, kept for upstream parity |
| `CF_ICU_PROVIDER` | `system` | `system` uses `find_package(ICU)`; `foundation-icu` expects `CF_FOUNDATION_ICU_DIR` |
| `CF_FETCH_LIBDISPATCH` | OFF | `FetchContent` swift-corelibs-libdispatch when no `dispatch` package is found (Ubuntu case) |
| `CF_USE_VENDORED_BLOCKSRUNTIME` | `AUTO` | use `BlockRuntime/` object library when no system BlocksRuntime is found |
| `CF_BUILD_TESTS` | ON when top-level | adds the C smoke tests to CTest |
| `CF_INSTALL` | ON when top-level | install rules and package config |
| `CF_ENABLE_WERROR` | OFF | |

### 4.3 Targets and usage

* Library target `CoreFoundation`, alias `CoreFoundation::CoreFoundation`.
  Output name `CoreFoundation`, `SOVERSION 1338`, `C_VISIBILITY_PRESET hidden`
  once S10 is in.
* Public include dirs: `$<BUILD_INTERFACE:.../include>`,
  `$<INSTALL_INTERFACE:include>` and `include/CoreFoundation` (so both
  `#include <CoreFoundation/CoreFoundation.h>` and the quoted internal style
  work).
* Public compile definitions: `DEPLOYMENT_RUNTIME_SWIFT=0` (until S1 lands,
  then not needed). Do not force `-fblocks` on consumers: the block-taking
  APIs in the public headers (`CFRunLoop.h`, `CFStorage.h`,
  `CFCalendarPriv.h`, `ForFoundationOnly.h`) are all guarded by
  `#if __BLOCKS__`, so a consumer compiled without Blocks simply does not see
  them. Document that `-fblocks` is needed to use those few APIs.
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
find_package(CoreFoundation 1338 REQUIRED)
target_link_libraries(app PRIVATE CoreFoundation::CoreFoundation)
```

```cmake
include(FetchContent)
FetchContent_Declare(CoreFoundation GIT_REPOSITORY https://github.com/CFCocoa/CoreFoundation GIT_TAG v1338.0.0)
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
* macOS: a non-system CF conflicts with the OS CoreFoundation symbol-wise.
  Mark unsupported initially (or build only static with a prefixed install
  name) rather than promise it.
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

* Record the upstream SHA this fork is based on in `Docs/UPSTREAM.md`.
* `Tools/sync-upstream.sh`: fetch `swiftlang/swift-corelibs-foundation`,
  `git format-patch` the range restricted to the three `Sources/` dirs,
  `git am -3`, update the SHA. Upstream churn in CF is modest (190 commits in
  roughly 2.5 years, almost all small fixes), so conflicts will be rare and
  confined to the files touched by S1-S11.
* Send S2 (cleaner form), S3, S4, S5, S7, S8, S9 upstream as they are pure C
  path repairs; each one accepted removes a conflict source.

---

## 7. Roadmap

Phase 0, groundwork (this document): done. Decide on section 2 items 3 and 7
(SOVERSION, trim or not).

Phase 1, "it builds and links": S1-S5, S7; new root `CMakeLists.txt` with
ICU shim, dispatch and BlocksRuntime detection, shared/static, install and
package config; `Tests/C/smoke.c` passing on Ubuntu 24.04 (with fetched
libdispatch) and Debian (packaged libdispatch). Acceptance: `cmake -B build &&
cmake --build build && ctest --test-dir build` green from a clean container
with only `clang cmake ninja libicu-dev libblocksruntime-dev` installed.

Phase 2, "it is a good citizen": S6, S8, S9, interface libraries behind
options, pkg-config, header self-containment test, consumer tests, CI matrix
including static, musl and sanitizers, README rewrite for C consumers.

Phase 3, "polish": S10 hidden visibility with an exports audit, S11 ISO8601
restore, Windows and FreeBSD jobs, release tagging with `SOVERSION`
discipline, `sync-upstream.sh` exercised once against upstream `main`.

---

## 8. Open questions

1. Trim the repository to CF only (option B) or keep the Swift tree (A)?
   The plan assumes B.
2. Keep `CF_SWIFT_RUNTIME` as a compile-only option, or drop Swift mode
   outright and accept slightly larger sync diffs?
3. Library versioning: inherit `1338` from 5.10.1's `project(VERSION 1338)`,
   or start at `1.0.0`?
4. Is macOS in scope at all? If yes, as a static library with a renamed
   install name only.
5. Should the vendored `uuid.c` stay (S8 rename) or should CF require
   libuuid on Linux like 5.10.1's `static-module.map` suggests (`link "uuid"`)?

# CoreFoundation

Apple's CoreFoundation, taken from [swift-corelibs-foundation](https://github.com/swiftlang/swift-corelibs-foundation)
and built as a **standalone C library** with CMake. No Swift toolchain is
involved: this is `libCoreFoundation` for C and C++ programs on Linux and the
BSDs, the way the pre-Swift-6 CMake build used to provide it.

The code is the upstream `Sources/CoreFoundation` tree with a handful of small
patches that make the C runtime mode build again (see
[Docs/StandaloneCBuildPlan.md](Docs/StandaloneCBuildPlan.md) for the full
list and the reasoning). Upstream fixes are pulled in regularly; see
[Docs/UPSTREAM.md](Docs/UPSTREAM.md).

## Building

Requirements:

* CMake 3.20 or newer, and a generator such as Ninja.
* **clang.** CoreFoundation uses the Blocks language extension, which gcc does
  not implement.
* ICU (`libicu-dev` on Debian/Ubuntu, `libicu-devel` on Fedora).
* libdispatch from swift-corelibs-libdispatch (`libdispatch-dev` on Debian).
  Ubuntu does not package it; configure with `-DCF_FETCH_LIBDISPATCH=ON` to
  have it downloaded and built as part of this build, or point
  `CMAKE_PREFIX_PATH`/`CF_DISPATCH_ROOT` at an existing installation (a Swift
  toolchain's `usr/lib/swift` layout is recognised too).
* A Blocks runtime (`libblocksruntime-dev`). If none is found, the copy under
  `Sources/CoreFoundation/BlockRuntime` is compiled into the library.

```bash
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCF_FETCH_LIBDISPATCH=ON
```

```bash
cmake --build build
```

```bash
ctest --test-dir build
```

```bash
cmake --install build --prefix /opt/corefoundation
```

### Options

| Option | Default | Meaning |
|---|---|---|
| `BUILD_SHARED_LIBS` | `ON` | shared `libCoreFoundation.so.1` or static `libCoreFoundation.a` |
| `CF_FETCH_LIBDISPATCH` | `OFF` | build swift-corelibs-libdispatch in-tree when no libdispatch is found (`CF_LIBDISPATCH_GIT_TAG` selects the tag) |
| `CF_DISPATCH_ROOT` | | prefix of a libdispatch installation without a CMake package config |
| `CF_BLOCKS_RUNTIME` | `auto` | `auto`, `system` or `vendored` |
| `CF_ICU_PROVIDER` | `system` | `system` uses `find_package(ICU)`; `foundation-icu` builds against a swift-foundation-icu checkout at `CF_FOUNDATION_ICU_DIR` |
| `CF_BUILD_XML_INTERFACE` | `OFF` | also build `_CFXMLInterface` (needs libxml2). Only useful for Swift Foundation compatibility |
| `CF_BUILD_URLSESSION_INTERFACE` | `OFF` | also build `_CFURLSessionInterface` (needs libcurl). Only useful for Swift Foundation compatibility |
| `CF_BUILD_TESTS` | `ON` when top-level | build the C smoke tests |
| `CF_INSTALL` | `ON` when top-level | generate install rules |
| `CF_SWIFT_RUNTIME` | `OFF` | build for the Swift runtime instead of the C runtime. Unsupported here; it exists so the Swift mode keeps compiling |

## Using it

With an installed copy:

```cmake
find_package(CoreFoundation 1.0 REQUIRED)
target_link_libraries(app PRIVATE CoreFoundation::CoreFoundation)
```

Vendored into your own build:

```cmake
include(FetchContent)
FetchContent_Declare(CoreFoundation
    GIT_REPOSITORY https://github.com/CFCocoa/CoreFoundation
    GIT_TAG        main)
FetchContent_MakeAvailable(CoreFoundation)
target_link_libraries(app PRIVATE CoreFoundation::CoreFoundation)
```

A `CoreFoundation.pc` file is installed for pkg-config users.

```c
#include <CoreFoundation/CoreFoundation.h>

int main(void) {
    CFStringRef s = CFStringCreateWithFormat(NULL, NULL, CFSTR("%d bottles"), 99);
    CFShow(s);
    CFRelease(s);
    return 0;
}
```

The headers compile with any C compiler; the handful of APIs that take blocks
(`CFRunLoopPerformBlock`, `CFRunLoopTimerCreateWithHandler`, ...) appear only
when compiling with `-fblocks`. Code that writes blocks of its own must also
link the Blocks runtime (`-lBlocksRuntime`, or the `BlocksRuntime::BlocksRuntime`
target from `cmake/FindBlocksRuntime.cmake`); CoreFoundation only carries the
runtime when the vendored copy was compiled in.

## What is and is not here

* The public C API is the one documented by Apple, as implemented by
  swift-corelibs-foundation (feature level of roughly macOS Monterey). Mach
  ports, message ports and user notifications are not implemented, as upstream.
* macOS is not a target. Programs that need CoreFoundation on macOS should use
  the system framework.
* `_CFXMLInterface` and `_CFURLSessionInterface` are thin shims written for
  importing libxml2 and libcurl into Swift. They build here behind options, but
  C programs are better off using libxml2 and libcurl directly.

## License

Apache License 2.0 with the Swift runtime library exception, as upstream. See
[LICENSE](LICENSE).

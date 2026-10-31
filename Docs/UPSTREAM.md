# Upstream synchronisation

This repository carries only the C parts of swift-corelibs-foundation:

* `Sources/CoreFoundation`
* `Sources/_CFXMLInterface`
* `Sources/_CFURLSessionInterface`

Everything else that upstream ships (the Swift `Foundation`, `FoundationXML`,
`FoundationNetworking`, tests, `Package.swift`, the toolchain CMake build) was
removed, and the build system, docs and tests here are our own.

| | |
|---|---|
| Upstream repository | https://github.com/swiftlang/swift-corelibs-foundation |
| Upstream branch | `main` |
| Last synchronised upstream commit | `5674de51dda87a7336274f3fda52b4b729fdecf3` (2026-10-30, "Use getpwuid_r in CFCopyUserName for thread safety (#5617)") |

## How to sync

Never `git merge` upstream: the deleted Swift paths would turn every upstream
commit that touches them into a modify/delete conflict. Instead, import the
upstream commits as patches restricted to the three source directories:

```bash
Tools/sync-upstream.sh
```

The script fetches upstream `main`, produces `git format-patch` output for
`<last synced>..upstream/main` limited to the paths above, applies it with
`git am -3`, and prints the new commit to record in the table above. If a
patch conflicts it stops at that patch; resolve it, `git am --continue`, and
re-run the script to finish.

The local patches that can conflict are listed in
[StandaloneCBuildPlan.md](StandaloneCBuildPlan.md), section 3. Keep them as
small as they are; when upstream rewrites one of those regions, re-apply the
intent rather than the old text.

## Things to watch for after a sync

* New C files under `Sources/CoreFoundation` must be added to
  `Sources/CoreFoundation/CMakeLists.txt` (the source list is explicit).
* New `#include <_foundation_unicode/...>` headers are handled automatically
  by the ICU shim generator at configure time.
* Any new function that upstream moves from C to Swift (`@_cdecl` in a
  `.swift` file) leaves an unresolved symbol here. `nm -u` on the built
  library, filtered for non-libc/ICU/dispatch names, finds them; restore a C
  implementation under `#if !DEPLOYMENT_RUNTIME_SWIFT` as was done for
  `CFCopyUserName` and `_CFGetCurrentDirectory`.
* Run the full matrix before pushing: shared, static, vendored Blocks runtime,
  `CF_SWIFT_RUNTIME=ON` (compile only), and the tests.

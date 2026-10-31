#!/usr/bin/env bash
# Imports CoreFoundation changes from swift-corelibs-foundation as path-limited
# patches. See Docs/UPSTREAM.md.
set -euo pipefail

UPSTREAM_URL=${UPSTREAM_URL:-https://github.com/swiftlang/swift-corelibs-foundation.git}
UPSTREAM_BRANCH=${UPSTREAM_BRANCH:-main}
PATHS=(Sources/CoreFoundation Sources/_CFXMLInterface Sources/_CFURLSessionInterface)

cd "$(git rev-parse --show-toplevel)"

last=$(sed -n 's/^| Last synchronised upstream commit | `\([0-9a-f]\{40\}\)`.*/\1/p' Docs/UPSTREAM.md)
if [[ -z "$last" ]]; then
    echo "could not read the last synchronised commit from Docs/UPSTREAM.md" >&2
    exit 1
fi

git remote get-url upstream >/dev/null 2>&1 || git remote add upstream "$UPSTREAM_URL"
git fetch upstream "$UPSTREAM_BRANCH"
new=$(git rev-parse "upstream/$UPSTREAM_BRANCH")

if [[ "$new" == "$last" ]]; then
    echo "already up to date with upstream/$UPSTREAM_BRANCH ($new)"
    exit 0
fi

count=$(git rev-list --count "$last..$new" -- "${PATHS[@]}")
echo "importing $count upstream commits touching ${PATHS[*]}"
if [[ "$count" -eq 0 ]]; then
    echo "nothing to import; update Docs/UPSTREAM.md to $new"
    exit 0
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
git format-patch --quiet -o "$tmp" "$last..$new" -- "${PATHS[@]}"
git am -3 "$tmp"/*.patch

sed -i.bak "s/^| Last synchronised upstream commit | \`[0-9a-f]\{40\}\`.*/| Last synchronised upstream commit | \`$new\` ($(git log -1 --format='%ad, \"%s\"' --date=short "$new")) |/" Docs/UPSTREAM.md
rm -f Docs/UPSTREAM.md.bak
git add Docs/UPSTREAM.md
git commit -q -m "Sync CoreFoundation with upstream $new"
echo "synchronised with $new"

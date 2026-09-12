#!/bin/bash
# Fan the plugin ABI contract out to the rostered plugins: vendored headers,
# cmake helpers, the CMake CI workflow and .gitattributes.
#
#   scripts/update-api-headers.sh ../retrovert_api
#
# Only plugins/* directories that already vendor include/retrovert are touched,
# so work-in-progress directories that are not submodules are left alone. Every
# plugin listed in .gitmodules must be reached, so an uninitialized submodule is
# an error rather than a quietly partial fan-out.

set -uo pipefail

# pretracker and uade carry their own build.yml with MSVC matrix rows. ci/build.yml
# is the template for the other 27 and must not delete that coverage, so the
# fan-out skips them; they track the template by hand.
ci_exceptions="pretracker uade"

self="$(basename "$0")"
here="$(cd "$(dirname "$0")" && pwd)"
root="$(dirname "$here")"

if [ $# -ne 1 ]; then
    echo "usage: $self <retrovert_api checkout>" >&2
    exit 2
fi

api="$(cd "$1" 2>/dev/null && pwd)" || { echo "$self: $1 is not a directory" >&2; exit 1; }
if [ ! -d "$api/include/retrovert" ]; then
    echo "$self: $api is not a retrovert_api checkout (no include/retrovert)" >&2
    exit 1
fi

updated=""
skipped=""
for dir in "$root"/plugins/*/; do
    name="$(basename "$dir")"
    [ -d "$dir/include/retrovert" ] || continue

    # a header dropped from the contract has to disappear from the plugins too
    rm -f "$dir/include/retrovert"/*.h
    cp "$api/include/retrovert"/*.h "$dir/include/retrovert/"
    if [ -d "$dir/cmake" ]; then
        cp "$api/cmake"/*.cmake "$dir/cmake/"
    fi
    cp "$here/ci/.gitattributes-template" "$dir/.gitattributes"

    if [[ " $ci_exceptions " == *" $name "* ]]; then
        skipped="$skipped $name"
    else
        mkdir -p "$dir/.github/workflows"
        cp "$here/ci/build.yml" "$dir/.github/workflows/build.yml"
    fi

    updated="$updated $name"
    echo "updated $name"
done

missing=""
while read -r path; do
    name="${path#plugins/}"
    [ -n "$name" ] || continue
    [[ " $updated " == *" $name "* ]] || missing="$missing $name"
done < <(git config -f "$root/.gitmodules" --get-regexp '^submodule\..*\.path$' | awk '{print $2}')

echo "$(wc -w <<<"$updated") plugin(s) updated from $api"
if [ -n "$skipped" ]; then
    echo "build.yml kept (local MSVC variant):$skipped"
fi
if [ -n "$missing" ]; then
    echo "$self: rostered but not reached (uninitialized submodule?):$missing" >&2
    exit 1
fi
exit 0

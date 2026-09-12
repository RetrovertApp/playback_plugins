#!/bin/bash
# Tests for scripts/update-api-headers.sh and scripts/roll-harness.sh against a
# throwaway aggregate: two plugin submodules with local bare remotes, one
# unrostered directory, and a stand-in api checkout.

set -uo pipefail

source_scripts="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@test
export GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@test
git_q() { git -c protocol.file.allow=always -c advice.detachedHead=false "$@" >/dev/null 2>&1; }

failures=0
check() {
    local what="$1"; shift
    if "$@"; then
        echo "  ok   $what"
    else
        echo "  FAIL $what"
        failures=$((failures + 1))
    fi
}
same() { [ "$1" = "$2" ] || { echo "       expected '$2', got '$1'" >&2; return 1; }; }

USES_OLD="RetrovertApp/retrovert-build-harness/.github/workflows/build-plugin.yml@v9"
USES_NEW="RetrovertApp/retrovert/.github/workflows/build-plugin.yml@harness/v10"
USES_V11="RetrovertApp/retrovert/.github/workflows/build-plugin.yml@harness/v11"

make_plugin() {
    local name="$1"
    local src="$work/src/$name"
    git_q init --bare --initial-branch=master "$work/remotes/$name.git"
    mkdir -p "$src/include/retrovert" "$src/cmake" "$src/.github/workflows"
    echo stale > "$src/include/retrovert/playback.h"
    echo stale > "$src/cmake/PlaybackPlugin.cmake"
    echo stale > "$src/.github/workflows/build.yml"
    cat > "$src/.github/workflows/release-build.yml" <<YML
name: Release artifacts
jobs:
  build:
    uses: $USES_OLD
    secrets: inherit
YML
    git_q init --initial-branch=master "$src"
    git_q -C "$src" add -A
    git_q -C "$src" commit -m init
    git_q -C "$src" remote add origin "$work/remotes/$name.git"
    git_q -C "$src" push -u origin master
}

# --- fixture ----------------------------------------------------------------
mkdir -p "$work/remotes" "$work/src"
make_plugin openmpt
make_plugin uade

api="$work/api"
mkdir -p "$api/include/retrovert" "$api/cmake"
echo fresh > "$api/include/retrovert/playback.h"
echo fresh > "$api/include/retrovert/log.h"
echo fresh > "$api/cmake/PlaybackPlugin.cmake"

agg="$work/agg"
mkdir -p "$agg/scripts"
cp "$source_scripts"/*.sh "$agg/scripts/"
cp -r "$source_scripts/ci" "$agg/scripts/ci"
scripts="$agg/scripts"
# the scripts resolve the aggregate from their own location, so they must run
# from the fixture copy and never from the checkout
for s in update-api-headers.sh roll-harness.sh ci/build.yml; do
    [ -e "$scripts/$s" ] || { echo "fixture is missing $s" >&2; exit 1; }
done
git_q init --initial-branch=main "$agg"
for name in openmpt uade; do
    git_q -C "$agg" submodule add "$work/remotes/$name.git" "plugins/$name"
done
mkdir -p "$agg/plugins/wip"            # unrostered: no include/retrovert
echo nothing > "$agg/plugins/wip/README.md"
git_q -C "$agg" add -A
git_q -C "$agg" commit -m init

# --- update-api-headers.sh --------------------------------------------------
echo "update-api-headers.sh"
out="$("$scripts/update-api-headers.sh" "$api" 2>&1)"; rc=$?
check "exits 0" same "$rc" 0
check "copies headers" grep -qx fresh "$agg/plugins/openmpt/include/retrovert/playback.h"
check "adds new headers" test -f "$agg/plugins/openmpt/include/retrovert/log.h"
check "copies cmake" grep -qx fresh "$agg/plugins/openmpt/cmake/PlaybackPlugin.cmake"
check "copies the CI template" \
    cmp -s "$scripts/ci/build.yml" "$agg/plugins/openmpt/.github/workflows/build.yml"
check "keeps a local CI variant" grep -qx stale "$agg/plugins/uade/.github/workflows/build.yml"
check "names the skipped plugin" grep -q uade <<<"$out"
check "skips unrostered directories" test ! -d "$agg/plugins/wip/include"
check "leaves release-build.yml alone" \
    grep -qF "$USES_OLD" "$agg/plugins/openmpt/.github/workflows/release-build.yml"

# land the fan-out so the plugins are clean again, then re-run: a second pass
# over an up-to-date plugin must leave nothing behind.
for name in openmpt uade; do
    git_q -C "$agg/plugins/$name" add -A
    git_q -C "$agg/plugins/$name" commit -m "vendor the api"
    git_q -C "$agg/plugins/$name" push origin master
done
git_q -C "$agg" add -A
git_q -C "$agg" commit -m "bump pins"
# the real aggregate checks its submodules out detached, and a stale local
# master is the norm there; mirror that before the roll tests
for name in openmpt uade; do
    git_q -C "$agg/plugins/$name" checkout --detach HEAD
    git_q -C "$agg/plugins/$name" branch -f master HEAD~1
done

"$scripts/update-api-headers.sh" "$api" >/dev/null 2>&1
check "is idempotent" same "$(git -C "$agg/plugins/openmpt" status --porcelain | wc -l)" 0

check "copies .gitattributes" \
    cmp -s "$scripts/ci/.gitattributes-template" "$agg/plugins/openmpt/.gitattributes"

# a header dropped from the contract must not survive in the plugins
mv "$api/include/retrovert/log.h" "$work/log.h"
"$scripts/update-api-headers.sh" "$api" >/dev/null 2>&1
check "drops headers removed from the contract" \
    test ! -f "$agg/plugins/openmpt/include/retrovert/log.h"
mv "$work/log.h" "$api/include/retrovert/log.h"
"$scripts/update-api-headers.sh" "$api" >/dev/null 2>&1

# an uninitialized submodule must not pass as a silent partial fan-out
mv "$agg/plugins/uade/include" "$work/held-include"
out="$("$scripts/update-api-headers.sh" "$api" 2>&1)"; rc=$?
check "fails on a plugin missing from the fan-out" test "$rc" -ne 0
check "names the plugin it could not reach" grep -q "uade" <<<"$out"
mv "$work/held-include" "$agg/plugins/uade/include"

out="$("$scripts/update-api-headers.sh" 2>&1)"; rc=$?
check "rejects a missing argument" same "$rc" 2
out="$("$scripts/update-api-headers.sh" "$work/nope" 2>&1)"; rc=$?
check "rejects a bad api path" test "$rc" -ne 0

# --- roll-harness.sh, dry run ----------------------------------------------
echo "roll-harness.sh (dry run)"
before="$(git -C "$agg/plugins/openmpt" rev-parse HEAD)"
out="$("$scripts/roll-harness.sh" "$USES_NEW" 2>&1)"; rc=$?
check "exits 0" same "$rc" 0
check "lists both plugins" same "$(grep -c -- '->' <<<"$out")" 2
check "reports the count" grep -qE '2 plugin' <<<"$out"
check "says nothing was pushed" grep -qi 'dry run' <<<"$out"
check "edits no file" grep -qF "$USES_OLD" "$agg/plugins/openmpt/.github/workflows/release-build.yml"
check "makes no commit" same "$(git -C "$agg/plugins/openmpt" rev-parse HEAD)" "$before"

# a plugin whose harness line cannot be read must stop the roll, not be skipped
mv "$agg/plugins/uade/.github/workflows/release-build.yml" "$work/held.yml"
grep -v 'uses:' "$work/held.yml" > "$agg/plugins/uade/.github/workflows/release-build.yml"
out="$("$scripts/roll-harness.sh" "$USES_NEW" 2>&1)"; rc=$?
check "exits non-zero on an unreadable harness line" test "$rc" -ne 0
check "says which plugin it could not read" \
    grep -q "uade: expected one harness uses: line, found 0" <<<"$out"
cp "$work/held.yml" "$agg/plugins/uade/.github/workflows/release-build.yml"
git_q -C "$agg/plugins/uade" checkout -- .github/workflows/release-build.yml

# an uninitialized submodule must stop the roll, not shrink it
mv "$agg/plugins/uade/include" "$work/held-include"
out="$("$scripts/roll-harness.sh" "$USES_NEW" 2>&1)"; rc=$?
check "exits non-zero when a rostered plugin is unreachable" test "$rc" -ne 0
check "names the unreachable plugin" grep -q "uade: not on disk" <<<"$out"
mv "$work/held-include" "$agg/plugins/uade/include"

out="$("$scripts/roll-harness.sh" "not-a-workflow-ref" 2>&1)"; rc=$?
check "rejects a malformed target" same "$rc" 2
out="$("$scripts/roll-harness.sh" 2>&1)"; rc=$?
check "rejects a missing argument" same "$rc" 2

# --- roll-harness.sh, apply ------------------------------------------------
echo "roll-harness.sh (apply)"
out="$("$scripts/roll-harness.sh" "$USES_NEW" --apply --no-verify-target 2>&1)"; rc=$?
check "exits 0" same "$rc" 0
check "rewrites the uses: line" \
    grep -qF "$USES_NEW" "$agg/plugins/openmpt/.github/workflows/release-build.yml"
check "keeps the rest of the file" \
    grep -qF "secrets: inherit" "$agg/plugins/openmpt/.github/workflows/release-build.yml"
check "commits in the submodule" \
    test "$(git -C "$agg/plugins/openmpt" rev-parse HEAD)" != "$before"
check "leaves the submodule clean" \
    same "$(git -C "$agg/plugins/openmpt" status --porcelain | wc -l)" 0
check "lands the commit on master" \
    same "$(git -C "$agg/plugins/openmpt" rev-parse --abbrev-ref HEAD)" master
check "pushes to the remote" same \
    "$(git -C "$work/remotes/openmpt.git" rev-parse master)" \
    "$(git -C "$agg/plugins/openmpt" rev-parse HEAD)"
check "bumps the pin" same \
    "$(git -C "$agg" rev-parse ":plugins/openmpt")" \
    "$(git -C "$agg/plugins/openmpt" rev-parse HEAD)"
check "commits the pin bump" same "$(git -C "$agg" status --porcelain -- plugins | wc -l)" 0
check "does not push the aggregate" grep -qi 'not pushed\|unpushed' <<<"$out"

out="$("$scripts/roll-harness.sh" "$USES_NEW" --apply --no-verify-target 2>&1)"; rc=$?
check "is idempotent" same "$rc" 0
check "reports nothing to do" grep -qiE 'up to date|0 plugin' <<<"$out"

# --- roll-harness.sh refuses to roll a drifted or dirty pin ----------------
echo "roll-harness.sh (guards)"
# a commit on the plugin's master that the aggregate's pin does not have
drift="$work/drift"
git_q clone "$work/remotes/openmpt.git" "$drift"
echo drift > "$drift/drift.txt"
git_q -C "$drift" add -A
git_q -C "$drift" commit -m drift
git_q -C "$drift" push origin master \
    || { echo "  FAIL could not stage the drift fixture"; failures=$((failures + 1)); }
out="$("$scripts/roll-harness.sh" "$USES_OLD" --apply --no-verify-target 2>&1)"; rc=$?
check "exits non-zero on drift" test "$rc" -ne 0
check "says why it refused" grep -q "openmpt: pin is not origin/master" <<<"$out"
check "leaves the drifted plugin unrolled" \
    grep -qF "$USES_NEW" "$agg/plugins/openmpt/.github/workflows/release-build.yml"
check "still rolls the clean plugin" \
    grep -qF "$USES_OLD" "$agg/plugins/uade/.github/workflows/release-build.yml"

# a refused plugin whose HEAD has moved off the recorded pin must keep its pin,
# even while another plugin in the same run rolls and bumps its own
git_q -C "$agg/plugins/openmpt" fetch origin master
git_q -C "$agg/plugins/openmpt" switch --detach origin/master
git_q -C "$agg" add -- plugins/openmpt
git_q -C "$agg" commit -m "take the drifted commit"
pin_before="$(git -C "$agg" rev-parse ":plugins/uade")"
git_q -C "$agg/plugins/uade" switch --detach origin/master
echo local > "$agg/plugins/uade/local.txt"
git_q -C "$agg/plugins/uade" add -A
git_q -C "$agg/plugins/uade" commit -m "local work"
out="$("$scripts/roll-harness.sh" "$USES_V11" --apply --no-verify-target 2>&1)"; rc=$?
check "refuses a plugin whose HEAD left the pin" test "$rc" -ne 0
check "still rolls the healthy plugin alongside it" \
    grep -qF "$USES_V11" "$agg/plugins/openmpt/.github/workflows/release-build.yml"
check "does not commit a refused plugin's pin" \
    same "$(git -C "$agg" rev-parse ":plugins/uade")" "$pin_before"
git_q -C "$agg/plugins/uade" switch --detach "$pin_before"

# local commits on a plugin's master must never be discarded by the roll
git_q -C "$agg/plugins/uade" branch -f master origin/master
git_q -C "$agg/plugins/uade" switch master
echo unpushed > "$agg/plugins/uade/unpushed.txt"
git_q -C "$agg/plugins/uade" add -A
git_q -C "$agg/plugins/uade" commit -m "unpushed work"
unpushed="$(git -C "$agg/plugins/uade" rev-parse master)"
git_q -C "$agg/plugins/uade" switch --detach origin/master
out="$("$scripts/roll-harness.sh" "$USES_NEW" --apply --no-verify-target 2>&1)"; rc=$?
check "refuses a plugin with unpushed master commits" test "$rc" -ne 0
check "keeps the unpushed commits" \
    same "$(git -C "$agg/plugins/uade" rev-parse master)" "$unpushed"
git_q -C "$agg/plugins/uade" branch -f master origin/master

echo scratch > "$agg/plugins/uade/dirty.txt"
out="$("$scripts/roll-harness.sh" "$USES_NEW" --apply --no-verify-target 2>&1)"; rc=$?
check "exits non-zero on a dirty submodule" test "$rc" -ne 0
check "says the worktree is dirty" grep -q "uade: worktree is dirty" <<<"$out"
check "leaves the dirty plugin unrolled" \
    grep -qF "$USES_OLD" "$agg/plugins/uade/.github/workflows/release-build.yml"

echo
if [ "$failures" -eq 0 ]; then
    echo "all checks passed"
else
    echo "$failures check(s) failed"
    exit 1
fi

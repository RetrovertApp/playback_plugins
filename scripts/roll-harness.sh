#!/bin/bash
# Roll every rostered plugin onto a new build-harness workflow, then bump the
# aggregate's pins. Dry run by default; --apply commits and pushes each plugin.
#
#   scripts/roll-harness.sh RetrovertApp/retrovert/.github/workflows/build-plugin.yml@harness/v10
#   scripts/roll-harness.sh ... --apply
#   scripts/roll-harness.sh ... --apply --no-verify-target   # ref does not exist yet
#
# A plugin is rolled only when its worktree is clean, its pin is exactly
# origin/master, and its local master holds nothing origin/master does not, so a
# roll never drags unreviewed commits into the roster or discards local ones. Only
# plugins that were actually rolled have their pin bumped, and the aggregate's pin
# commit is never pushed.
#
# Every remote is proved writable before any repository is touched, because the
# commit precedes the push and a push that cannot authenticate would otherwise
# leave the roll half applied. A roll that still half applies -- the rewrite is
# committed, the push does not land -- is finished by re-running: the rewritten
# workflow already names the target, so the plugin is resumed rather than skipped.

set -uo pipefail

self="$(basename "$0")"
root="$(cd "$(dirname "$0")/.." && pwd)"
uses_re='[A-Za-z0-9._-]+/[A-Za-z0-9._-]+/\.github/workflows/[A-Za-z0-9._-]+\.yml@[A-Za-z0-9._/-]+'

usage() {
    echo "usage: $self <owner/repo/.github/workflows/file.yml@ref> [--apply] [--no-verify-target]" >&2
    exit 2
}

target=""
apply=0
verify=1
for arg in "$@"; do
    case "$arg" in
        --apply) apply=1 ;;
        --no-verify-target) verify=0 ;;
        -*) usage ;;
        *) [ -n "$target" ] && usage; target="$arg" ;;
    esac
done
[ -n "$target" ] || usage
[[ "$target" =~ ^$uses_re$ ]] || usage

ref="${target##*@}"
spec="${target%@*}"
repo="${spec%%/.github/*}"
workflow_path=".github/${spec#*/.github/}"

# pushing a workflow ref that does not resolve breaks the release build of every
# plugin at once, so confirm it exists before touching 29 repositories
if [ "$apply" -eq 1 ] && [ "$verify" -eq 1 ]; then
    if ! command -v gh >/dev/null; then
        echo "$self: gh not found, cannot confirm $target exists; pass --no-verify-target to roll anyway" >&2
        exit 1
    fi
    if ! gh api "repos/$repo/contents/$workflow_path?ref=$ref" --silent 2>/dev/null; then
        echo "$self: $workflow_path does not exist on $repo@$ref" >&2
        exit 1
    fi
fi

if [ "$apply" -eq 1 ]; then
    unwritable=""
    for dir in "$root"/plugins/*/; do
        [ -d "$dir/include/retrovert" ] || continue
        [ -f "$dir.github/workflows/release-build.yml" ] || continue
        # a throwaway branch name, so this proves write access rather than
        # whether this particular plugin's master could fast-forward today
        GIT_TERMINAL_PROMPT=0 git -C "$dir" push --quiet --dry-run origin \
            "HEAD:refs/heads/__roll_preflight__" 2>/dev/null \
            || unwritable="$unwritable $(basename "$dir")"
    done
    if [ -n "$unwritable" ]; then
        echo "$self: cannot push to:$unwritable" >&2
        echo "$self: nothing was modified; fix those remotes and re-run" >&2
        exit 1
    fi
fi

edits=0
rolled=""
refused=""

for dir in "$root"/plugins/*/; do
    name="$(basename "$dir")"
    workflow="$dir.github/workflows/release-build.yml"
    [ -d "$dir/include/retrovert" ] || continue
    [ -f "$workflow" ] || continue

    mapfile -t found < <(grep -E '^[[:space:]]*uses:' "$workflow" | grep -oE "$uses_re")
    if [ "${#found[@]}" -ne 1 ]; then
        echo "$name: expected one harness uses: line, found ${#found[@]}" >&2
        refused="$refused $name"
        continue
    fi
    old="${found[0]}"
    if [ "$old" = "$target" ]; then
        [ "$apply" -eq 1 ] || continue
        git -C "$dir" fetch --quiet origin master || continue
        [ "$(git -C "$dir" rev-parse HEAD)" = "$(git -C "$dir" rev-parse origin/master)" ] && continue
        # ahead of the remote with the rewrite already committed: an unfinished
        # roll. Diverged history is someone else's business, so leave it alone.
        if ! git -C "$dir" merge-base --is-ancestor origin/master HEAD; then
            echo "$name: rewritten but diverged from origin/master, not rolled" >&2
            refused="$refused $name"
            continue
        fi
        echo "$name: rewritten but never pushed, finishing"
        if git -C "$dir" push --quiet origin HEAD:master; then
            rolled="$rolled plugins/$name"
        else
            echo "$name: push failed" >&2
            refused="$refused $name"
        fi
        continue
    fi

    edits=$((edits + 1))
    echo "$name: $old -> $target"
    [ "$apply" -eq 1 ] || continue

    if [ -n "$(git -C "$dir" status --porcelain)" ]; then
        echo "$name: worktree is dirty, not rolled" >&2
        refused="$refused $name"
        continue
    fi
    if ! git -C "$dir" fetch --quiet origin master; then
        echo "$name: cannot fetch origin master, not rolled" >&2
        refused="$refused $name"
        continue
    fi
    if [ "$(git -C "$dir" rev-parse HEAD)" != "$(git -C "$dir" rev-parse origin/master)" ]; then
        echo "$name: pin is not origin/master, not rolled" >&2
        refused="$refused $name"
        continue
    fi
    if git -C "$dir" rev-parse --verify --quiet master >/dev/null \
        && ! git -C "$dir" merge-base --is-ancestor master origin/master; then
        echo "$name: local master holds commits origin/master does not, not rolled" >&2
        refused="$refused $name"
        continue
    fi

    git -C "$dir" switch --quiet -C master origin/master \
        && sed -i "s|$old|$target|" "$workflow" \
        && git -C "$dir" commit --quiet -m "Roll the release harness to $ref" \
            -- .github/workflows/release-build.yml \
        && git -C "$dir" push --quiet origin master \
        && rolled="$rolled plugins/$name" \
        || { echo "$name: roll failed" >&2; refused="$refused $name"; }
done

# a rostered plugin that is not on disk would otherwise shrink the roll in silence
while read -r path; do
    name="${path#plugins/}"
    [ -n "$name" ] || continue
    if [ ! -d "$root/$path/include/retrovert" ] || [ ! -f "$root/$path/.github/workflows/release-build.yml" ]; then
        echo "$name: not on disk (uninitialized submodule?)" >&2
        refused="$refused $name"
    fi
done < <(git config -f "$root/.gitmodules" --get-regexp '^submodule\..*\.path$' | awk '{print $2}')

if [ "$apply" -eq 0 ]; then
    echo "$edits plugin(s) would be rolled to $ref (dry run, nothing committed or pushed)"
elif [ -n "$rolled" ]; then
    # only the plugins that were actually rolled and pushed, so a refused plugin
    # whose HEAD moved locally cannot reach the roster
    # shellcheck disable=SC2086
    git -C "$root" add -- $rolled
    git -C "$root" commit --quiet -m "Roll the roster onto harness $ref" -- $rolled
    echo "$(wc -w <<<"$rolled") plugin(s) rolled and pushed; aggregate pin bump committed but not pushed"
else
    echo "0 plugin(s) rolled"
fi

if [ -n "$refused" ]; then
    echo "$self: not rolled:$refused" >&2
    exit 1
fi
exit 0

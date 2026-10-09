#!/usr/bin/env bash
# Commit and push every repository this project owns, innermost first.
#
# The tree is a superproject over submodules that are themselves
# superprojects, and the pinned commit *is* the dependency (DEPENDENCIES.md).
# So the order is not a convenience, it is a correctness requirement:
#
#   1. cmake_modules, scene_rdl2, moonray, mcrt_denoise, moonshine
#   2. external/openmoonray        - records the commits from step 1
#   3. the superproject            - records the commit from step 2
#
# A parent must be pushed *after* its children, or it points at commits that
# only exist on this machine and nobody else can check the tree out.
#
# Every repository is handled on its own and confirmed separately, so an https
# remote that asks for credentials asks once per repository and the run can be
# interrupted and resumed at any point - rerunning skips whatever is already
# done.
#
# Usage:
#   ./scripts/publish-all.sh ["message"] [--dry-run] [--yes] [--no-push]
#   ./scripts/publish-all.sh -m "message" ...
#
#   message    one commit message, used for EVERY repository - the commits are
#              the same change set recorded at different levels of the tree, so
#              a message per level would only make the history harder to read.
#              Asked for once at the start when it is not given on the command
#              line (default: "update macOS").
#   --dry-run  show what would happen, touch nothing
#   --yes      do not ask before each commit/push (still stops on any error)
#   --no-push  commit only
#
# Not touched: external/anari-sdk and external/vcpkg are unmodified upstream
# checkouts, and external/pynari is a fork we do not currently change. They are
# reported if they are dirty, never committed.

set -uo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

message=""
dry_run=0
assume_yes=0
do_push=1

while [ $# -gt 0 ]; do
    case "$1" in
        -m) message="${2:-}"; shift 2 ;;
        --dry-run) dry_run=1; shift ;;
        --yes|-y) assume_yes=1; shift ;;
        --no-push) do_push=0; shift ;;
        -h|--help)
            # Print the leading comment block, so the help can never go stale
            # against a fixed line range.
            awk 'NR>1 && /^#/ {sub(/^# ?/, ""); print; next} NR>1 {exit}' \
                "${BASH_SOURCE[0]}"
            exit 0 ;;
        -*) echo "unknown option: $1" >&2; exit 2 ;;
        *)
            if [ -n "$message" ]; then
                echo "more than one commit message given: '$message' and '$1'" >&2
                exit 2
            fi
            message="$1"; shift ;;
    esac
done

readonly default_message="update macOS"

# path : branch  - in publication order, children before parents.
repos=(
    "external/openmoonray/cmake_modules:windows-port"
    "external/openmoonray/moonray/scene_rdl2:windows-port"
    "external/openmoonray/moonray/moonray:windows-port"
    "external/openmoonray/moonray/mcrt_denoise:windows-port"
    "external/openmoonray/moonray/moonshine:windows-port"
    "external/openmoonray:windows-port"
    ".:main"
)

# Repositories we deliberately never commit into.
readonly_repos=(external/anari-sdk external/vcpkg external/pynari)

bold=$'\033[1m'; dim=$'\033[2m'; red=$'\033[31m'; green=$'\033[32m'
yellow=$'\033[33m'; reset=$'\033[0m'
[ -t 1 ] || { bold=""; dim=""; red=""; green=""; yellow=""; reset=""; }

failed=0

say()  { printf '%s\n' "$*"; }
note() { printf '%s%s%s\n' "$dim" "$*" "$reset"; }
warn() { printf '%s%s%s\n' "$yellow" "$*" "$reset"; }
err()  { printf '%s%s%s\n' "$red" "$*" "$reset" >&2; }
ok()   { printf '%s%s%s\n' "$green" "$*" "$reset"; }

confirm() {
    [ "$assume_yes" -eq 1 ] && return 0
    if [ ! -r /dev/tty ]; then
        err "no controlling terminal to ask on - rerun interactively, or pass --yes"
        return 1
    fi
    local answer
    printf '%s [y/N] ' "$1"
    read -r answer </dev/tty || return 1
    case "$answer" in [yY]|[yY][eE][sS]) return 0 ;; *) return 1 ;; esac
}

run() {
    if [ "$dry_run" -eq 1 ]; then
        note "  would run: $*"
        return 0
    fi
    "$@"
}

# `git status --porcelain` lists a submodule whose *worktree* is dirty as well
# as one whose recorded commit moved. Only the latter is something the parent
# can commit, and a parent must not be committed while a child is still dirty -
# that is what produces a pin nobody can resolve.
child_still_dirty() {
    local path="$1" line
    while IFS= read -r line; do
        # " m path" = submodule worktree dirty; " M path" = recorded commit moved
        case "$line" in
            " m "*|"?m "*) return 0 ;;
        esac
    done < <(git -C "$path" status --porcelain 2>/dev/null)
    return 1
}

publish_repo() {
    local path="$1" branch="$2" name
    name="$path"
    [ "$name" = "." ] && name="openmoonray-anari (superproject)"

    printf '\n%s=== %s ===%s\n' "$bold" "$name" "$reset"

    if [ ! -e "$path/.git" ]; then
        warn "  not a checkout - skipped"
        return 0
    fi

    local remote
    remote="$(git -C "$path" remote get-url origin 2>/dev/null)" || remote="(none)"
    note "  origin: $remote"
    note "  branch: $branch"

    # --- refuse to publish a parent whose children are not finished ---------
    if child_still_dirty "$path"; then
        err "  a submodule inside this repository still has uncommitted changes:"
        git -C "$path" status --porcelain | sed 's/^/    /'
        err "  finish that repository first (this script goes children-first)."
        failed=1
        return 1
    fi

    # --- commit -------------------------------------------------------------
    if [ -n "$(git -C "$path" status --porcelain)" ]; then
        say "  changes to commit:"
        git -C "$path" status --short | sed 's/^/    /'

        if confirm "  commit these changes?"; then
            # -A so a changed .gitmodules and moved submodule pins are staged
            # together with the file edits; they describe one state.
            run git -C "$path" add -A || { failed=1; return 1; }
            run git -C "$path" commit -m "$message" || { failed=1; return 1; }
            if [ "$dry_run" -eq 1 ]; then
                # Nothing was committed, so the parents below are still
                # reported against the current pins rather than the ones this
                # commit would create.
                note "  (dry run: parent pins below are shown unchanged)"
            else
                ok "  committed"
            fi
        else
            warn "  commit skipped - nothing will be pushed for this repository"
            return 0
        fi
    else
        note "  working tree clean"
    fi

    [ "$do_push" -eq 1 ] || { note "  --no-push: not pushing"; return 0; }

    # --- push ---------------------------------------------------------------
    # Submodules are checked out detached, which is normal; push the commit at
    # HEAD to the named branch rather than trying to attach a local branch.
    local head remote_ref ahead
    head="$(git -C "$path" rev-parse HEAD)"
    remote_ref="refs/remotes/origin/$branch"

    if git -C "$path" rev-parse --verify --quiet "$remote_ref" >/dev/null; then
        if [ "$(git -C "$path" rev-parse "$remote_ref")" = "$head" ]; then
            note "  origin/$branch is already at this commit - nothing to push"
            return 0
        fi
        # A non-fast-forward here means the branch moved elsewhere; pushing
        # would either be rejected or (with --force) drop someone's work.
        if ! git -C "$path" merge-base --is-ancestor "$remote_ref" HEAD; then
            err "  origin/$branch is NOT an ancestor of HEAD - this would not"
            err "  fast-forward. Fetch and rebase/merge before publishing."
            failed=1
            return 1
        fi
        ahead="$(git -C "$path" rev-list --count "$remote_ref"..HEAD)"
        say "  $ahead commit(s) to push:"
        git -C "$path" log --oneline "$remote_ref"..HEAD | sed 's/^/    /'
    else
        warn "  no origin/$branch known locally (never fetched?) - will push HEAD to it"
    fi

    if confirm "  push to origin/$branch?"; then
        note "  (an https remote will ask for credentials now)"
        # No stdin redirect: git's credential prompt opens the controlling
        # terminal on its own, and redirecting here breaks a non-tty run.
        run git -C "$path" push origin "HEAD:refs/heads/$branch" \
            || { err "  push failed"; failed=1; return 1; }
        [ "$dry_run" -eq 0 ] && ok "  pushed"
    else
        warn "  push skipped"
    fi
}

printf '%spublish-all%s  root: %s\n' "$bold" "$reset" "$repo_root"
[ "$dry_run" -eq 1 ] && warn "dry run - nothing will be changed"

# Ask for the message once, and only when there is in fact something to
# commit somewhere in the tree.
if [ -z "$message" ]; then
    anything_dirty=0
    for entry in "${repos[@]}"; do
        path="${entry%%:*}"
        [ -e "$path/.git" ] || continue
        if [ -n "$(git -C "$path" status --porcelain 2>/dev/null)" ]; then
            anything_dirty=1
            break
        fi
    done
    if [ "$anything_dirty" -eq 1 ] && [ "$assume_yes" -eq 0 ] && [ -r /dev/tty ]; then
        printf 'commit message for every repository [%s]: ' "$default_message"
        read -r message </dev/tty || message=""
    fi
    [ -z "$message" ] && message="$default_message"
fi
note "commit message: $message"

for entry in "${repos[@]}"; do
    publish_repo "${entry%%:*}" "${entry##*:}" || break
done

# --- report the repositories this script never writes to --------------------
for path in "${readonly_repos[@]}"; do
    [ -e "$path/.git" ] || continue
    if [ -n "$(git -C "$path" status --porcelain 2>/dev/null)" ]; then
        printf '\n'
        warn "$path has local changes and is NOT published by this script"
        git -C "$path" status --short | sed 's/^/    /'
    fi
done

printf '\n'
if [ "$failed" -ne 0 ]; then
    err "publish-all stopped with errors - nothing after the failing repository was touched."
    exit 1
fi

# --- final check: no dangling pins ------------------------------------------
# Every recorded submodule commit has to exist on its remote, or a fresh clone
# cannot be checked out. Verifying that is cheap and it is exactly the mistake
# this ordering exists to prevent.
if [ "$dry_run" -eq 0 ] && [ "$do_push" -eq 1 ]; then
    dangling=0
    for entry in "${repos[@]}"; do
        path="${entry%%:*}"; branch="${entry##*:}"
        [ "$path" = "." ] && continue
        [ -e "$path/.git" ] || continue
        head="$(git -C "$path" rev-parse HEAD)"
        if git -C "$path" rev-parse --verify --quiet "refs/remotes/origin/$branch" >/dev/null \
           && ! git -C "$path" merge-base --is-ancestor "$head" "refs/remotes/origin/$branch"; then
            err "$path: HEAD ($(git -C "$path" rev-parse --short HEAD)) is not on origin/$branch"
            dangling=1
        fi
    done
    if [ "$dangling" -ne 0 ]; then
        err "some recorded pins are not published - a fresh clone would fail."
        exit 1
    fi
fi

ok "publish-all done."
say "Verify with:  git submodule status --recursive | grep -v '^ '"

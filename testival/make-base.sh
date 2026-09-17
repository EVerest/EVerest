#!/usr/bin/env bash
# Build testival/<event>-base from testival/manifest: upstream PRs stacked in manifest order as
# a linear history with one empty marker commit per entry. Each build is tagged
# testival/<event>-base-N.
#
#   testival/make-base.sh [--prev <ref>] [--worktree <dir>] [--dry-run]
#   testival/make-base.sh --finish     after resolving a conflict and `git rebase --continue`
#
# Runs in its own worktree (default: a sibling directory of the checkout); the checkout that
# runs it is untouched. Nothing is pushed.
set -euo pipefail

PREV=""
WT=""
DRY=0
FINISH=0
while [ $# -gt 0 ]; do
  case $1 in
    --prev) PREV=$2; shift 2 ;;
    --worktree) WT=$2; shift 2 ;;
    --dry-run) DRY=1; shift ;;
    --finish) FINISH=1; shift ;;
    *) echo "unknown arg $1" >&2; exit 2 ;;
  esac
done

ROOT=$(git rev-parse --show-toplevel)
MANIFEST=$ROOT/testival/manifest
EVENT=$(awk '$1=="event"{print $2}' "$MANIFEST")
[ -n "$EVENT" ] || { echo "manifest has no event line" >&2; exit 1; }
BASE_BRANCH=testival/$EVENT-base
TAG_PREFIX=testival/$EVENT-base-
WT=${WT:-$ROOT-testival-build}
TODO=$(mktemp)
trap 'rm -f "$TODO"' EXIT
log() { printf '%s\n' "$*" >&2; }

next_number() {
  local n
  n=$(git tag -l "$TAG_PREFIX*" | sed 's#.*-##' | sort -n | tail -1)
  echo $(( ${n:-0} + 1 ))
}

finish() {
  cd "$WT"
  [ -d "$(git rev-parse --git-path rebase-merge)" ] && { log "rebase still in progress in $WT"; exit 1; }
  local pending n
  pending=$(git rev-parse --git-path testival-pending)
  [ -f "$pending" ] || { log "no pending build in $WT"; exit 1; }
  n=$(cat "$pending"); rm -f "$pending"
  git tag "$TAG_PREFIX$n" HEAD
  git branch -f "$BASE_BRANCH" HEAD
  log "$BASE_BRANCH is now $TAG_PREFIX$n at $(git rev-parse --short HEAD)"
  log "table of contents: git log --format=%s $BASE_BRANCH | grep '^==='"
  log "push with: git push --force-with-lease origin $BASE_BRANCH $TAG_PREFIX$n"
  log "then rebase the fixes: testival/rebase-fixes.sh"
}
[ "$FINISH" = 1 ] && { finish; exit 0; }

[ -n "$PREV" ] || PREV=$(git rev-parse --verify -q "refs/heads/$BASE_BRANCH" || true)

# --- fetch everything the manifest names -------------------------------------------
FETCH=(+refs/heads/main:refs/testival/main)
while read -r kind a b _; do
  case $kind in
    pr) FETCH+=("+refs/pull/$a/head:refs/testival/pr/$a") ;;
    branch) FETCH+=("+refs/heads/$b:refs/testival/branch/$a") ;;
  esac
done < <(grep -vE '^\s*(#|$)' "$MANIFEST")
log "fetching ${#FETCH[@]} refs"
git fetch --quiet origin "${FETCH[@]}"

# --- helpers -----------------------------------------------------------------------
# commits of $2 not already (by patch-id) on $1, oldest first, no merges
own_commits() { git rev-list --reverse --no-merges --cherry-pick --right-only "$1...$2"; }

marker() { printf 'exec git commit --allow-empty -q -m %q\n' "=== end $1 ===" >> "$TODO"; }

emit_picks() {
  local n=0 c
  while read -r c; do
    [ -n "$c" ] || continue
    echo "pick $c $(git log -1 --format=%s "$c" | cut -c1-70)" >> "$TODO"; n=$((n+1))
  done
  echo "$n"
}

# range of a named local block in the previous build: between the preceding marker and its own
prev_block() {
  local end prev
  [ -n "$PREV" ] || return 1
  end=$(git log --format=%H -1 --fixed-strings --grep="=== end local $1 " "$PREV") || true
  [ -n "$end" ] || return 1
  prev=$(git log --format=%H -1 --grep='^=== ' "$end~1")
  echo "$prev..$end~1"
}

# --- build the todo ----------------------------------------------------------------
MAIN=""
parent=""
while read -r kind a b _; do
  case $kind in
    event) ;;
    main) MAIN=$a ;;
    stack) parent=refs/testival/main; echo "# ---- stack $a ----" >> "$TODO" ;;
    pr|branch)
      if [ "$kind" = pr ]; then ref=refs/testival/pr/$a; else ref=refs/testival/branch/$a; fi
      n=$(own_commits "$parent" "$ref" | emit_picks)
      marker "$kind $a @ $(git rev-parse --short "$ref") $(git log -1 --format=%s "$ref" | cut -c1-60) ($n commits)"
      parent=$ref ;;
    local)
      range=$(prev_block "$a" || true)
      if [ -n "$range" ]; then log "local $a: from previous build ($range)"
      else range=$b; [ -n "$range" ] || { log "local $a: no previous build and no range"; exit 1; }; fi
      n=$(git rev-list --reverse --no-merges "$range" | emit_picks)
      marker "local $a ($n commits)" ;;
    *) log "bad manifest line: $kind $a $b"; exit 1 ;;
  esac
done < <(grep -vE '^\s*(#|$)' "$MANIFEST")
[ -n "$MAIN" ] || { log "manifest has no main line"; exit 1; }

if [ "$DRY" = 1 ]; then cat "$TODO"; exit 0; fi

# --- run it in the build worktree ---------------------------------------------------
N=$(next_number)
[ -d "$WT" ] || git worktree add --detach "$WT" "$MAIN"
cd "$WT"
git rebase --abort 2>/dev/null || true
git checkout -q --detach "$MAIN"
mkdir -p testival && cp "$MANIFEST" "$ROOT/testival/make-base.sh" "$ROOT/testival/rebase-fixes.sh" testival/
git add testival && git commit -q -m "=== testival $EVENT base-$N ($(date +%Y-%m-%d)) ==="
echo "$N" > "$(git rev-parse --git-path testival-pending)"
git config rerere.enabled true
git config rerere.autoupdate true
# rebase -i onto HEAD with a replaced todo: picks arbitrary commits in manifest order
if GIT_SEQUENCE_EDITOR="cp $TODO" git rebase -i --empty=drop --no-autosquash HEAD; then
  finish
else
  log
  log "rebase stopped in $WT. Resolve, then:"
  log "  git -C $WT rebase --continue"
  log "  $ROOT/testival/make-base.sh --finish"
  exit 1
fi

#!/usr/bin/env bash
# Rebase the checked-out testival/<event>-fixes branch onto a new base and tag it
# testival/<event>-fixes-N. Event name from testival/manifest.
#
#   testival/rebase-fixes.sh [<new-base>]     default: testival/<event>-base
#
# The fixes branch is everything after the last "=== ... ===" marker commit, so the old base
# needs no bookkeeping. Run it in the checkout that has the fixes branch checked out, clean tree.
set -euo pipefail
EVENT=$(awk '$1=="event"{print $2}' "$(git rev-parse --show-toplevel)/testival/manifest")
[ -n "$EVENT" ] || { echo "manifest has no event line" >&2; exit 1; }
NEW_BASE=${1:-testival/$EVENT-base}
FIXES=testival/$EVENT-fixes
TAG_PREFIX=testival/$EVENT-fixes-

[ "$(git rev-parse --abbrev-ref HEAD)" = "$FIXES" ] || { echo "check out $FIXES first" >&2; exit 1; }
git diff --quiet && git diff --cached --quiet || { echo "working tree not clean" >&2; exit 1; }

OLD_BASE=$(git log --format=%H -1 --grep='^=== ' HEAD)
n=$(git tag -l "$TAG_PREFIX*" | sed 's#.*-##' | sort -n | tail -1); N=$(( ${n:-0} + 1 ))
echo "rebasing $(git rev-list --count "$OLD_BASE..HEAD") fixes from $(git rev-parse --short "$OLD_BASE") onto $NEW_BASE" >&2
if git -c rerere.enabled=true -c rerere.autoupdate=true rebase --onto "$NEW_BASE" "$OLD_BASE"; then
  git tag "$TAG_PREFIX$N" HEAD
  echo "$FIXES is now $TAG_PREFIX$N at $(git rev-parse --short HEAD)" >&2
  echo "push with: git push --force-with-lease origin $FIXES $TAG_PREFIX$N" >&2
else
  echo "resolve, git rebase --continue, then: git tag $TAG_PREFIX$N" >&2; exit 1
fi

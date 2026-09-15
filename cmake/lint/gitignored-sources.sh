#!/bin/sh
# Fails if a source file the build needs is ignored by git.
#
# Usage: gitignored-sources.sh <source-root>
#
# .gitignore's leading `*build*` matches every Rust module's build.rs, so
# `git add -A` on a new module produces a clean commit with the module's build
# script missing, and it then builds for nobody else. The path that does not
# exist is checked too, because the trap only bites files that are still new.
set -eu

root="${1:-.}"
cd "$root"

# --no-index, because a tracked file is reported as not ignored however
# aggressively the patterns match it, and this has to catch the next new file.
paths=$({
    find modules lib -name build.rs -not -path '*/target/*'
    echo modules/EVSE/RsNewModule/build.rs
})

set +e
ignored=$(printf '%s\n' "$paths" | git check-ignore --no-index --stdin)
status=$?
set -e

case "$status" in
    0)
        echo "error: git ignores source files the build needs:" >&2
        printf '%s\n' "$ignored" | git check-ignore --no-index --stdin -v >&2
        exit 1
        ;;
    1)
        echo "ok: no build script is hidden by .gitignore"
        ;;
    *)
        echo "error: git check-ignore could not answer (exit $status)" >&2
        exit 1
        ;;
esac

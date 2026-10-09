#!/bin/sh
# Asserts that a Rust module binary carries a relocatable runpath.
#
# Usage: rust-module-runpath.sh <binary>
#
# Cargo emits no runpath of its own and install(PROGRAMS) does no RPATH
# rewriting, so a Rust module without this cannot find libframework.so once
# installed. It links, installs and runs from the development run scripts
# regardless, which is why the check has to be explicit.
set -eu

binary="$1"
runpath=$(readelf -d "$binary" | sed -n 's/.*Library runpath: \[\(.*\)\]/\1/p')

case "$runpath" in
    '$ORIGIN'*) ;;
    '') echo "error: $binary has no RUNPATH" >&2; exit 1 ;;
    *)  echo "error: $binary has a non-relocatable RUNPATH: $runpath" >&2; exit 1 ;;
esac

echo "ok: $binary runpath is $runpath"

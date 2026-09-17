#!/bin/sh

ninja -C "$EXT_MOUNT/build" install/strip
retVal=$?

if [ $retVal -ne 0 ]; then
    echo "Installation failed with return code $retVal"
    exit $retVal
fi

# The Rust modules are the only ones whose runpath nothing else checks: cargo
# emits none and install(PROGRAMS) does no RPATH rewriting, so a regression here
# is invisible until a module is started outside the development run scripts.
m="$EXT_MOUNT/dist/libexec/everest/modules/RsExample/RsExample"
readelf -d "$m" | grep -q 'RUNPATH.*\$ORIGIN' || {
    echo "No relocatable RUNPATH on installed Rust module"
    exit 1
}
if ldd "$m" | grep -q 'not found'; then
    echo "Installed Rust module has unresolved libraries"
    ldd "$m"
    exit 1
fi
env -u LD_LIBRARY_PATH "$m" --help > /dev/null || {
    echo "Installed Rust module does not run"
    exit 1
}

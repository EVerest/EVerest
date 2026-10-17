#!/bin/sh

ninja -C "$EXT_MOUNT/build" install/strip
retVal=$?

if [ $retVal -ne 0 ]; then
    echo "Installation failed with return code $retVal"
    exit $retVal
fi

# C++ modules get their runpath from CMake's install-time RPATH rewriting.
# Rust ones get none: cargo emits no runpath and install(PROGRAMS) rewrites
# nothing. A regression shows up only when a module is started outside the
# generated development run scripts, which no other job does.
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

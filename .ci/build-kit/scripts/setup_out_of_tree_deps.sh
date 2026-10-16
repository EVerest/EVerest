#!/bin/sh

# System libraries everest-chargebyte expects to be installed besides what EDM fetches.
# Debian only has libgpiod 1.x; sigslot is the version everest-core's dependencies.yaml pins.
PREFIX="$EXT_MOUNT/oot-deps"
SRC="$EXT_MOUNT/oot-deps-src"

fetch() {
    curl -sSfL -o "$SRC/$2" "$1" \
        && echo "$3  $SRC/$2" | sha256sum -c - \
        && python3 -m tarfile -e "$SRC/$2" "$SRC"
}

mkdir -p "$PREFIX" "$SRC" \
    && fetch https://mirrors.edge.kernel.org/pub/software/libs/libgpiod/libgpiod-2.0.1.tar.xz libgpiod-2.0.1.tar.xz \
        b5367d28d045b36007a4ffd42cceda4c358737ef4f2ce22b0c1d05ec57a38392 \
    && fetch https://public.pengutronix.de/software/libsocketcan/libsocketcan-0.0.12.tar.bz2 libsocketcan-0.0.12.tar.bz2 \
        be8280124707701935e6294d366e2474158b758fa4b2e3cae571d5b256d2fe34 \
    && fetch https://github.com/palacaze/sigslot/archive/refs/tags/v1.2.3.tar.gz sigslot-1.2.3.tar.gz \
        36c30a88f05c8f8e6b4800d8f9c7c006cf61164695bf5fd91a8b80ed4e1d96af \
    && (cd "$SRC/libgpiod-2.0.1" && ./configure --prefix="$PREFIX" --enable-bindings-cxx && make -j"$(nproc)" install) \
    && (cd "$SRC/libsocketcan-0.0.12" && ./configure --prefix="$PREFIX" && make -j"$(nproc)" install) \
    && cmake -S "$SRC/sigslot-1.2.3" -B "$SRC/sigslot-build" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DSIGSLOT_COMPILE_EXAMPLES=OFF -DSIGSLOT_COMPILE_TESTS=OFF \
    && cmake --install "$SRC/sigslot-build"
retVal=$?
if [ $retVal -ne 0 ]; then
    echo "Building the out-of-tree dependencies failed with return code $retVal"
    exit $retVal
fi

#!/bin/sh
# libiso15118 with the EV half switched off: the configuration a consumer that only implements a
# charger uses. Nothing else in this repository builds that way, so without this job the OFF path
# rots and the EV half leaks back into the unconditional part of the target unnoticed.
#
# Only the library target is built. Modules are not: none of them is what this job is about, and
# the EV-side ones do not exist in this configuration.

set -e

cmake \
    -B "$EXT_MOUNT/build-secc-only" \
    -S "$EXT_MOUNT/source" \
    -G Ninja \
    -DEVC_ENABLE_CCACHE=1 \
    -DISO15118_2_GENERATE_AND_INSTALL_CERTIFICATES=OFF \
    -DEVEREST_BUILD_APPLICATIONS=OFF \
    -DBUILD_TESTING=OFF \
    -DISO15118_BUILD_EV_SIDE=OFF

ninja -C "$EXT_MOUNT/build-secc-only" iso15118

ARCHIVE="$EXT_MOUNT/build-secc-only/lib/everest/iso15118/src/iso15118/libiso15118.a"

# Namespaces are the check rather than file names: everything behind ISO15118_BUILD_EV_SIDE lives
# in iso15118::ev, iso15118::message_2 or iso15118::message_din, and a charger calls none of it.
leaked=$(nm -C --defined-only "$ARCHIVE" | grep -E 'iso15118::(ev|message_2|message_din)::' || true)
if [ -n "$leaked" ]; then
    echo "EV-side symbols in a charger-only libiso15118.a:"
    echo "$leaked" | head -40
    exit 1
fi

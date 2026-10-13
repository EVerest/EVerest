LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE.txt;md5=86d3f3a95c324c9479bd8986968f4327"

SRC_URI = "git://github.com/EVerest/linux_libnfc-nci.git;branch=everest;protocol=https \
           "

inherit cmake pkgconfig

S = "${WORKDIR}/git"

SRCREV = "c76c54428de1064c35b9de6b221adad5e6f6a9a1"

DEPENDS = "\
    everest-cmake \
"

PACKAGECONFIG ??= ""
PACKAGECONFIG[libgpiod] = "-DLIBNFCNCI_LIBGPIOD=ON,-DLIBNFCNCI_LIBGPIOD=OFF,libgpiod"

EXTRA_OECMAKE += "-DDISABLE_EDM=ON"

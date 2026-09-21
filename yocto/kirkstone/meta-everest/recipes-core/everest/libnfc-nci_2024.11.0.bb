LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE.txt;md5=86d3f3a95c324c9479bd8986968f4327"

SRC_URI = "git://github.com/EVerest/linux_libnfc-nci.git;branch=everest;protocol=https \
           "

inherit cmake

S = "${WORKDIR}/git"

SRCREV = "8b2eee89f46f8465bae1a459f834d27054901cd1"

DEPENDS = "\
    everest-cmake \
"

PACKAGECONFIG ??= ""
PACKAGECONFIG[libgpiod] = "-DLIBNFCNCI_LIBGPIOD=ON,-DLIBNFCNCI_LIBGPIOD=OFF,libgpiod"

EXTRA_OECMAKE += "-DDISABLE_EDM=ON"

# we need the configs from everest-core, so remove the default configs here
do_install:append() {
    rm ${D}${sysconfdir}/everest/libnfc_config/libnfc-nci.conf
    rm ${D}${sysconfdir}/everest/libnfc_config/libnfc-nxp.conf
}

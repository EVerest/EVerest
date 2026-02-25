LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE.txt;md5=86d3f3a95c324c9479bd8986968f4327"

SRC_URI = "git://github.com/EVerest/linux_libnfc-nci.git;branch=everest;protocol=https \
           "

inherit cmake

S = "${WORKDIR}/git"

SRCREV = "65276f9221cd2a50dbe4bc22e34879387b5800be"

DEPENDS = "\
    everest-cmake \
"

EXTRA_OECMAKE += "-DDISABLE_EDM=ON"

# we need the configs from everest-core, so remove the default configs here
do_install:append() {
    rm ${D}${sysconfdir}/everest/libnfc_config/libnfc-nci.conf
    rm ${D}${sysconfdir}/everest/libnfc_config/libnfc-nxp.conf
}

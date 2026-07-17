LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=86d3f3a95c324c9479bd8986968f4327"

SRC_URI = "git://github.com/EVerest/everest-cmake.git;branch=main;protocol=https"

SRCREV = "29fdd49cb9db7c4867f8e6aac6d810e79ad5b2a6"

S = "${WORKDIR}/git"

do_install() {
    install -d ${D}/usr/lib/cmake/everest-cmake
    cp -a --no-preserve=ownership ${S}/*.cmake ${D}/usr/lib/cmake/everest-cmake/
    cp -a --no-preserve=ownership ${S}/3rd_party ${D}/usr/lib/cmake/everest-cmake/
    cp -a --no-preserve=ownership ${S}/assets ${D}/usr/lib/cmake/everest-cmake/
    cp -a --no-preserve=ownership ${S}/golang-support ${D}/usr/lib/cmake/everest-cmake/
    cp -a --no-preserve=ownership ${S}/protobuf-helpers ${D}/usr/lib/cmake/everest-cmake/
}

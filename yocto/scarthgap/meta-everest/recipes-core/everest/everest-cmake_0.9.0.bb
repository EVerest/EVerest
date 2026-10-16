LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=86d3f3a95c324c9479bd8986968f4327"

SRC_URI = "git://github.com/EVerest/everest-cmake.git;branch=main;protocol=https"

SRCREV = "22049a66a67ced7863a94d8310dd27c31e12618b"

S = "${WORKDIR}/git"

do_install() {
    install -d ${D}/usr/lib/cmake/everest-cmake
    cp -a --no-preserve=ownership ${S}/*.cmake ${D}/usr/lib/cmake/everest-cmake/
    cp -a --no-preserve=ownership ${S}/3rd_party ${D}/usr/lib/cmake/everest-cmake/
    cp -a --no-preserve=ownership ${S}/assets ${D}/usr/lib/cmake/everest-cmake/
    cp -a --no-preserve=ownership ${S}/golang-support ${D}/usr/lib/cmake/everest-cmake/
    cp -a --no-preserve=ownership ${S}/protobuf-helpers ${D}/usr/lib/cmake/everest-cmake/
}

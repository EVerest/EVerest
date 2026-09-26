LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE.txt;md5=86d3f3a95c324c9479bd8986968f4327"

SRC_URI = "git://github.com/EVerest/linux_libnfc-nci.git;branch=everest;protocol=https \
           "

inherit cmake

SRCREV = "4cc291796b6f3f89df1647ee91008869bdae3659"

DEPENDS = "\
    everest-cmake \
"

EXTRA_OECMAKE += "-DDISABLE_EDM=ON"

INSANE_SKIP:${PN}-dev += "buildpaths"

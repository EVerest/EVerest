SUMMARY = "JSON-RPC 2.0 framework for modern C++ (json-rpc-cxx)"
HOMEPAGE = "https://github.com/jsonrpcx/json-rpc-cxx"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=23722aabb609187e801a18422ee3abb7"

SRC_URI = "git://github.com/jsonrpcx/json-rpc-cxx.git;protocol=https;branch=master \
           file://json-rpc-cxxConfig.cmake \
          "
SRCREV = "a0e195b575d62cb07016321ac9cd7e1b9e048fe5"

inherit cmake

S = "${WORKDIR}/git"

RDEPENDS:${PN}-dev = "nlohmann-json-dev"

EXTRA_OECMAKE = "-DCOMPILE_TESTS=OFF -DCOMPILE_EXAMPLES=OFF -DCODE_COVERAGE=OFF"

do_install:append() {
    install -d ${D}${libdir}/cmake/json-rpc-cxx
    install -p -m 0644 ${WORKDIR}/json-rpc-cxxConfig.cmake \
        ${D}${libdir}/cmake/json-rpc-cxx/
}

FILES:${PN}-dev += "${libdir}/cmake/json-rpc-cxx"

SUMMARY = "JSON schema validator for JSON for Modern C++"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=c441d022da1b1663c70181a32225d006"

SRC_URI = "git://github.com/pboettch/json-schema-validator;branch=main;protocol=https \
          "

SRCREV = "9abddfa905bb4889cc4ae55f87adcaa71dd3bc23"

S = "${WORKDIR}/git"

DEPENDS += "nlohmann-json"

inherit cmake
EXTRA_OECMAKE = "-DCMAKE_POSITION_INDEPENDENT_CODE=ON -DJSON_VALIDATOR_BUILD_EXAMPLES=OFF -DJSON_VALIDATOR_BUILD_TESTS=OFF -DJSON_VALIDATOR_INSTALL=ON -DJSON_VALIDATOR_BUILD_SHARED_LIBS=ON"

TARGET_CFLAGS += "-Wno-narrowing"

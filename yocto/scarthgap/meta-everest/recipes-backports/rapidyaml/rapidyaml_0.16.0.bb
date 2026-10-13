SUMMARY = "Rapid YAML"
AUTHOR = "Joao Paulo Magalhaes"
HOMEPAGE = "https://github.com/biojppm/rapidyaml.git"
SECTION = "libs"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE.txt;md5=11a6f80850f6993383976130ad87005e"

SRC_URI = "gitsm://github.com/biojppm/rapidyaml.git;branch=master;protocol=https"

inherit cmake

S = "${WORKDIR}/git"
PV = "v0.16.0"

SRCREV = "f8ac8dd50f4f7916579d55a05ebf9c6488e52670"

EXTRA_OECMAKE += "-DBUILD_SHARED_LIBS=ON -DRYML_WITH_LEGACY_OPERATORS=ON"

FILES:${PN}-dev += "${datadir}/ryml"

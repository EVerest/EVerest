FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# ev-dev-tools carries a pure PEP 621 pyproject.toml and no setup.py. kirkstone
# ships python3-setuptools 59.5.0, but setuptools only reads the [project] table
# from 61.0.0 on, so setuptools.build_meta finds no metadata and builds an empty
# UNKNOWN-0.0.0 wheel. No ev-cli lands in the (native) sysroot and everest-core's
# do_configure fails on find_program(EV_CLI).
#
# pyproject-setup.py reads pyproject.toml at build time and passes it to setup(),
# so it follows upstream changes. Fields it does not understand fail the build
# rather than produce an incomplete wheel.
SRC_URI += "file://pyproject-setup.py"

DEPENDS += "python3-tomli-native"

do_configure:prepend() {
    install -m 0644 ${UNPACKDIR}/pyproject-setup.py ${PEP517_SOURCE_PATH}/setup.py
}

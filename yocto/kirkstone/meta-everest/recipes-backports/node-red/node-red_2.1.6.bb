DESCRIPTION = "Node-RED"
HOMEPAGE = "http://nodered.org"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=d6f37569f5013072e9490d2194d10ae6"

inherit npm

PR = "r0"

SRC_URI = "\
    git://github.com/node-red/node-red.git;protocol=https;branch=master \
    npmsw://${THISDIR}/${BPN}/npm-shrinkwrap.json \
    file://${BPN}.service \
    file://Fixup-dependencies-for-newer-npm-versions.patch \
"

SRCREV = "173e75175eb1c40e7b11c8da4bccba8f2eb22937"

S = "${WORKDIR}/git/packages/node_modules/${BPN}"

EXTRA_OENPM = "--offline=false --proxy=false"

do_install:append() {
    # Service
    install -d ${D}${systemd_unitdir}/system/
    install -m 0644 ${WORKDIR}/${BPN}.service ${D}${systemd_unitdir}/system/

    # Remove hardware specific files
    rm ${D}/${bindir}/${BPN}-pi
    rm -rf ${D}/${libdir}/node_modules/${BPN}/bin
}

inherit systemd

SYSTEMD_AUTO_ENABLE = "enable"
SYSTEMD_SERVICE:${PN} = "${BPN}.service"

FILES:${PN} += "\
    ${systemd_unitdir} \
"

INSANE_SKIP:${PN} += "staticdev"
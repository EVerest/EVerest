# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
#
SUMMARY = "Mosquitto configuration for EVerest"
DESCRIPTION = "Runs mosquitto with /etc/mosquitto/everest.conf: the EVerest internal \
topics only over a Unix domain socket, all other topics also over TCP on localhost."
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/Apache-2.0;md5=89aea4e17d99a7cacdbeed46a0096b10"

SRC_URI = " \
    file://everest.conf \
    file://everest-external.acl \
    file://10-everest.conf \
"

S = "${UNPACKDIR}"

# the init script of mosquitto does not create /run/mosquitto
inherit features_check
REQUIRED_DISTRO_FEATURES = "systemd"

# provides the user mosquitto that owns the ACL
DEPENDS = "mosquitto"

do_install() {
    install -d ${D}${sysconfdir}/mosquitto
    install -m 0644 ${UNPACKDIR}/everest.conf ${D}${sysconfdir}/mosquitto/
    # mosquitto 2.1 warns about, and announces refusing, files it does not own
    install -m 0600 -o mosquitto -g mosquitto ${UNPACKDIR}/everest-external.acl ${D}${sysconfdir}/mosquitto/

    install -d ${D}${systemd_system_unitdir}/mosquitto.service.d
    sed -e 's,@SBINDIR@,${sbindir},g' -e 's,@SYSCONFDIR@,${sysconfdir},g' \
        ${UNPACKDIR}/10-everest.conf > ${D}${systemd_system_unitdir}/mosquitto.service.d/10-everest.conf
    chmod 0644 ${D}${systemd_system_unitdir}/mosquitto.service.d/10-everest.conf
}

FILES:${PN} = " \
    ${sysconfdir}/mosquitto/everest.conf \
    ${sysconfdir}/mosquitto/everest-external.acl \
    ${systemd_system_unitdir}/mosquitto.service.d \
"
CONFFILES:${PN} = " \
    ${sysconfdir}/mosquitto/everest.conf \
    ${sysconfdir}/mosquitto/everest-external.acl \
"

RDEPENDS:${PN} = "mosquitto"

# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
#
# Grant the Linux capabilities listed in the manifest.yaml of each installed module as file
# capabilities on the module binary. This happens in pkg_postinst when the root filesystem is
# created, installing the package on a running target does not apply them.

EVEREST_CAPABILITIES_SCRIPT = "${datadir}/everest/scripts/set_module_capabilities.py"
EVEREST_MODULES_DIR ?= "${libexecdir}/everest/modules"

PACKAGE_WRITE_DEPS += "libcap-native python3-native python3-pyyaml-native"

FILES:${PN} += "${EVEREST_CAPABILITIES_SCRIPT}"

do_install:append() {
    install -Dm 0755 ${S}/applications/utils/scripts/set_module_capabilities.py ${D}${EVEREST_CAPABILITIES_SCRIPT}
}

pkg_postinst:${PN}:append() {
    if [ -n "$D" ]; then
        nativepython3 $D${EVEREST_CAPABILITIES_SCRIPT} --modules-dir $D${EVEREST_MODULES_DIR}
    else
        echo "${PN}: module file capabilities are only applied at rootfs creation time"
    fi
}

SUMMARY = "OpenJDK - open-source reference implementation of the Java SE Platform"
LICENSE = "GPL-2.0-only"

LIC_FILES_CHKSUM = "file://LICENSE;md5=3e0b59f8fac05c3c03d4a26bbda13f8f"

SRC_URI = "git://github.com/openjdk/jdk;branch=master;name=target-jdk;protocol=https \
           https://github.com/adoptium/temurin25-binaries/releases/download/jdk-25.0.2%2B10/OpenJDK25U-jdk_x64_linux_hotspot_25.0.2_10.tar.gz;name=boot-jdk \
           file://0001-Fix-CC-CXX-env-var-handling.patch \
           "
SRC_URI[boot-jdk.sha256sum] = "987387933b64b9833846dee373b640440d3e1fd48a04804ec01a6dbf718e8ab8"
SRCREV_target-jdk = "176422b885d2d045dd44b61b7fcdcb01be2d00a7"
PV = "17-ga"

S = "${WORKDIR}/git"
B = "${WORKDIR}/build"

inherit autotools update-alternatives

OPENJDK_INSTALL_PREFIX = "/usr/lib/jvm/java-25-openjdk-${TARGET_ARCH}"
OPENJDK_BOOT_JDK_DIR = "${WORKDIR}/jdk-25.0.2+10/"


mangle_environement_vars() {
    unset CFLAGS
    unset CXXFLAGS
    unset LDFLAGS
}

TARGET_CFLAGS +=" -Wno-nonnull -Wno-maybe-uninitialized"

do_configure () {
    rm -rf ${B}
    mkdir -p ${B}
    cd ${B}

    mangle_environement_vars

    sh ${S}/configure \
        --prefix=${OPENJDK_INSTALL_PREFIX} \
        --enable-headless-only \
        --with-jvm-variants=server \
        --with-native-debug-symbols=none \
        --with-debug-level=release \
        --with-sysroot=${STAGING_DIR_TARGET} \
        --openjdk-target=${TARGET_SYS} \
        --with-boot-jdk=${OPENJDK_BOOT_JDK_DIR} \
        --with-extra-cflags="${TARGET_CFLAGS}" \
        --with-extra-ldflags="${TARGET_LDFLAGS}"
}

do_compile[network] = "1"

do_compile () {
    mangle_environement_vars
    # FIXME (aw): can we somehow use ${PARALLEL_MAKE} here?
    # Unfortunately it's of the form '-j N', but we would need JOBS=N
    make images
}

do_install () {
    rm -rf ${B}/images/jdk/demo
    install -d ${D}${OPENJDK_INSTALL_PREFIX}
    cp -a --no-preserve=ownership ${B}/images/jdk/* ${D}${OPENJDK_INSTALL_PREFIX}/
}

ALTERNATIVE:${PN}-jre = "java"
ALTERNATIVE_TARGET[java] = "${OPENJDK_INSTALL_PREFIX}/bin/java"

PACKAGE_DEBUG_SPLIT_STYLE = "debug-without-src"
PACKAGE_BEFORE_PN = "${PN}-jre"
FILES:${PN}-src = "${OPENJDK_INSTALL_PREFIX}/lib/src.zip"
FILES:${PN}-doc = "${OPENJDK_INSTALL_PREFIX}/man/*"
FILES:${PN}-jre = "\
                   ${OPENJDK_INSTALL_PREFIX}/bin/java \
                   ${OPENJDK_INSTALL_PREFIX}/bin/jpackage \
                   ${OPENJDK_INSTALL_PREFIX}/bin/keytool \
                   ${OPENJDK_INSTALL_PREFIX}/conf/* \
                   ${OPENJDK_INSTALL_PREFIX}/legal/* \
                   ${OPENJDK_INSTALL_PREFIX}/lib/* \
                   ${OPENJDK_INSTALL_PREFIX}/release \
                   "
FILES:${PN} = "\
               ${OPENJDK_INSTALL_PREFIX}/bin/* \
               ${OPENJDK_INSTALL_PREFIX}/jmods/* \
               ${OPENJDK_INSTALL_PREFIX}/include/* \
               "

DEPENDS += "\
    alsa-lib \
    cups \
    fontconfig \
    libxi \
    libx11 \
    libxrender \
    libxrandr \
    libsm \
    libxt \
    libxtst \
    libxext \
    libice \
    xorgproto \
"

DEPENDS += "\
    make-native \
    unzip-native \
    zip-native \
"

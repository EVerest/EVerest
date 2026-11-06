LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=86d3f3a95c324c9479bd8986968f4327"

FILESEXTRAPATHS:prepend := "${EVEREST_CORE_PARENT_PATH}:"
# With EXTERNALSRC the recipe builds from the source tree itself, so copying it
# only costs time and fails when something writes to the tree during the copy.
SRC_URI = "${@'' if d.getVar('EXTERNALSRC') else 'file://' + d.getVar('EVEREST_CORE_REPONAME') + ';unpack=0'}"

S = "${UNPACKDIR}/${EVEREST_CORE_REPONAME}"

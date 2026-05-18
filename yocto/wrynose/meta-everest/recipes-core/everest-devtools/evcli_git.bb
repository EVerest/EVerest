LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=86d3f3a95c324c9479bd8986968f4327"

# ev-cli move to everest-core, include the version from there in this recipe
require ../everest/everest-core_git.inc

inherit python_setuptools_build_meta

PEP517_SOURCE_PATH = "${S}/applications/utils/ev-dev-tools"

RDEPENDS:${PN} = " \
    pip-stringcase \
    python3-jsonschema \
    python3-pyyaml \
    python3-jinja2 \
"

BBCLASSEXTEND = "native"
BBCLASSEXTEND:append = " nativesdk"

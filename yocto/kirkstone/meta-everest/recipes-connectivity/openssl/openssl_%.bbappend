SRC_URI += "https://raw.githubusercontent.com/EVerest/everest-core/4c7b9f5f15a8adce2a38113926c09fe8ba486b21/lib/staging/tls/openssl-3.0.8-feat-updates-to-support-status_request_v2.patch;apply=yes;downloadfilename=everest-openssl.patch;name=everest-openssl-patch \
           "
SRC_URI[everest-openssl-patch.sha256sum] = "17626c6efd9568d761f219712b8d840026a5d2de54d8b75c86f32332af1c35e4"
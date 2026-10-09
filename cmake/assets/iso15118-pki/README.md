<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Pionix GmbH and Contributors to EVerest -->

Vendored from EVerest/ext-switchev-iso15118, commit `1b3ea741ea7954bbe4288742446f33653ecb7849`, path `iso15118/shared/pki/` (Apache-2.0, the license of this repository; see the root `LICENSE`).

The script retains its original author header; only an SPDX line was added. The
OpenSSL configs are unchanged. CMake stages this directory in the build tree and
runs the script there with `-v iso-2 -i <everest-core>` when
`ISO15118_2_GENERATE_AND_INSTALL_CERTIFICATES` is enabled, edm is not disabled, and
Ev15118 is included in the build. This keeps intermediate
files in the build tree and installs the generated development PKI from
`config/certs`. The tooling itself is not installed.

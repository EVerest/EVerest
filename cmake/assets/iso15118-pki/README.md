<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright Pionix GmbH and Contributors to EVerest -->

Vendored from EVerest/ext-switchev-iso15118, commit `1b3ea741ea7954bbe4288742446f33653ecb7849`, path `iso15118/shared/pki/` (Apache-2.0, the license of this repository; see the root `LICENSE`).

The script retains its original author header. It adds an SPDX line, stops at the
first failing step with a non-zero exit, quotes the install paths, and drops the
outputs that only Josev read: the EVCC truststore and keystore and the OEM PKCS#12
chain they were built from. The OpenSSL configs are unchanged. The default run needs only
OpenSSL; the optional Keysight output (`-k`) also builds Java keystores and needs
`keytool` from a Java runtime, which the script checks before generating anything.
It clears the Keysight output (`pki-ext`) before writing it, so `-k` can run twice.

`cmake/iso15118-dev-pki.cmake` stages this directory in the build tree, runs the
script there with `-v iso-2 -i <everest-core>`, and checks that every file listed in
`expected-files.txt` exists and is not empty. Configure runs it when all three hold:

- `ISO15118_2_GENERATE_AND_INSTALL_CERTIFICATES` is ON (the default);
- edm is enabled, so packaging builds with `DISABLE_EDM=ON` (Yocto) never ship the
  development private keys;
- at least one of Ev15118, EvseV2G and Evse15118D20 is built.

Configure fails when the script fails or a listed file is missing, and reports the
reason when it skips the generation. Intermediate files stay in the build tree; the
generated development PKI is installed from `config/certs`. The tooling itself is not
installed. The `dev_pki_generation` test (label `dev-pki`) runs the same generation
into the build tree, and `dev_pki_generation_fails` checks that a failing `openssl`
call fails it and names the missing files.

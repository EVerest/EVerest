# Tests

Building tests:

```sh
$ cd EVerest
$ mkdir build
$ cd build
$ cmake -GNinja -DEVEREST_CORE_BUILD_TESTING=ON ..
$ ninja install
```

`touch release.json` may be needed if it hasn't been created
(then re-run `ninja install`).

## Unit tests

- `./tls_test` and `./patched_test`
- automatically runs `pki.sh`
- run from the directory containing the executable

## Test PKI

Two PKI generators ship alongside the tests:

- `pki.sh` produces the default P-256 chains used by the majority of the tests.
- `pki-multi-curve.sh <curve>` produces an independent server and client
  chain (root CA -> intermediate CA -> leaf) on the requested NIST curve so
  signature-algorithm pinning can be exercised per curve. Supported curves
  are `P-256`, `P-384` and `P-521`; output lands in `pki-<curve>/` next to
  the test binaries. The CMake custom commands invoke this script for each
  curve at build time, so the `pki-P-256/`, `pki-P-384/` and `pki-P-521/`
  directories are created automatically.

## Standalone server

- Run `pki.sh` to build the test certificates and keys
- use openssl_s_client to make test connections
- run from the directory containing the executable

### Standalone TLS server

Tests the Server class in isolation.

- `./tls_server`
- connects to IPv4 and IPv6
- only one connection at a time
- gracefully terminates after 30 seconds
- `valgrind` can be used to check memory allocations (should be none)
- requires client certificate and supports `status_request` extension
- s_client echos back what is typed

```sh
openssl s_client -connect localhost:8444 -verify 2 -CAfile server_root_cert.pem -cert client_cert.pem -cert_chain client_chain.pem -key client_priv.pem -verify_return_error -verify_hostname evse.pionix.de -status
```

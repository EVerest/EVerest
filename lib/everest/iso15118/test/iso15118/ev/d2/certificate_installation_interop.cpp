// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Interop guard for the CertificateInstallationRes signature.
//
// Every other test of this path signs the response with the same encoder it then verifies
// with, so it agrees with itself whatever bytes that encoder produces. This one does not:
// the response it reads was captured off the wire from a session against the reference EVCC
// stack, and its four Reference digests were computed by an independent EXI codec. If our
// fragment encoding drifts from the standard again, or a caller fills the eMAID fragment
// the wrong way, this fails and nothing else will.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <unistd.h>

#include <everest/tls/openssl_util.hpp>

#include <iso15118/ev/detail/d2/crypto.hpp>

using namespace iso15118;

namespace {

// The trusted V2G root of the capture, tests/ocpp_tests/test_sets/everest-aux/certs/ca/v2g.
constexpr const char* V2G_ROOT_PEM = "-----BEGIN CERTIFICATE-----\n"
                                     "MIIBxTCCAWqgAwIBAgICMDkwCgYIKoZIzj0EAwIwSDESMBAGA1UEAwwJVjJHUm9v\n"
                                     "dENBMRAwDgYDVQQKDAdFVmVyZXN0MQswCQYDVQQGEwJERTETMBEGCgmSJomT8ixk\n"
                                     "ARkWA1YyRzAgFw0yMzA5MjYwNzM4MzRaGA8zMDIzMDEyNzA3MzgzNFowSDESMBAG\n"
                                     "A1UEAwwJVjJHUm9vdENBMRAwDgYDVQQKDAdFVmVyZXN0MQswCQYDVQQGEwJERTET\n"
                                     "MBEGCgmSJomT8ixkARkWA1YyRzBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABJjZ\n"
                                     "qKsQaffrsSSRTQE57gcpjuxtkKluOMbQWHmpBHgK7coPhm/xlmfDn/rRmQ0fvEqi\n"
                                     "zx/oDCt8yAObxSTyj3CjQjBAMA8GA1UdEwEB/wQFMAMBAf8wDgYDVR0PAQH/BAQD\n"
                                     "AgEGMB0GA1UdDgQWBBRnxqnie55mMxFdFY0Ht6WzBfPjPjAKBggqhkjOPQQDAgNJ\n"
                                     "ADBGAiEAzmGWz+ES3AskIzWkpyLReF5uumL3P9M6oGbuWQNI7oUCIQCxMh9YfpQ9\n"
                                     "ODORWoaQhzzcGylXRfW0Vo+KbGSUIM5UJQ==\n"
                                     "-----END CERTIFICATE-----\n";

// One CertificateInstallationRes, 4143 bytes of EXI, base64. eMAID id4 = UKSWI123456789A.
// The OCPP suite checks the same capture:
// tests/ocpp_tests/test_sets/everest-aux/exi/certificate_installation_res.base64.
std::string read_response_b64() {
    std::ifstream in(CERTIFICATE_INSTALLATION_RES_B64);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

SCENARIO("EVCC verifies a CertificateInstallationRes signed by another implementation") {

    GIVEN("a response captured off the wire and the V2G root that signed its CPS chain") {
        const auto response_b64 = read_response_b64();
        REQUIRE_FALSE(response_b64.empty());
        const auto response = openssl::base64_decode(response_b64.c_str(), response_b64.size());
        REQUIRE(response.size() == 4143);

        // Per-process name and a checked write: a shared path someone else owns would otherwise
        // fail silently and leave the verification running against their file.
        const auto root_path =
            std::filesystem::temp_directory_path() / ("ev_d2_interop_v2g_root_" + std::to_string(::getpid()) + ".pem");
        {
            std::ofstream out(root_path);
            out << V2G_ROOT_PEM;
            REQUIRE(out.good());
        }

        THEN("the signature over all four signed elements verifies") {
            REQUIRE(ev::d2::crypto::verify_certificate_installation_res(response, root_path.string()));
        }

        std::filesystem::remove(root_path);
    }
}

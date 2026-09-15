// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Interop guard for the CertificateInstallationRes signature.
//
// Every other test of this path signs the response with the same encoder it then verifies
// with, so it agrees with itself whatever bytes that encoder produces. This one does not:
// the response below was captured off the wire from a session against the reference EVCC
// stack, and its four Reference digests were computed by an independent EXI codec. If our
// fragment encoding drifts from the standard again, or a caller fills the eMAID fragment
// the wrong way, this fails and nothing else will.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

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
constexpr const char* RESPONSE_B64 =
    "gJgCKEU2LX/ropJKiVodHRwOi8vd3d3LnczLm9yZy9UUi9jYW5vbmljYWwtZXhpL0NWh0dHA6Ly93d3cudzMub3JnLzIwMDE\n"
    "vMDQveG1sZHNpZy1tb3JlI2VjZHNhLXNoYTI1NkQMRtLIYgStDo6OB0Xl7u7u5c7mZc3uTOXqikXsbC3N7c0sbC2FrK8NJek\n"
    "KWh0dHA6Ly93d3cudzMub3JnLzIwMDEvMDQveG1sZW5jI3NoYTI1NkIKDkFx+5xeFduxMWndL6x63rLqdXVtEXkfm2WP1Tus\n"
    "fzBAxG0shkBK0Ojo4HReXu7u7lzuZlze5M5eqKRexsLc3tzSxsLYWsrw0l6QpaHR0cDovL3d3dy53My5vcmcvMjAwMS8wNC9\n"
    "4bWxlbmMjc2hhMjU2QgLb8dUepS6KaKITsoShJ4TD4LB5erl4BYZ/9HqJII6M0EDEbSyGYErQ6OjgdF5e7u7uXO5mXN7kzl6\n"
    "opF7Gwtze3NLGwthayvDSXpClodHRwOi8vd3d3LnczLm9yZy8yMDAxLzA0L3htbGVuYyNzaGEyNTZCC818h/n046JxEku478\n"
    "Hy+bQdIzvbjhoTZD3XRcXOc1xwQMRtLIaAStDo6OB0Xl7u7u5c7mZc3uTOXqikXsbC3N7c0sbC2FrK8NJekKWh0dHA6Ly93d\n"
    "3cudzMub3JnLzIwMDEvMDQveG1sZW5jI3NoYTI1NkIF/HhNyPO33/Df+huv9J2SV8tWMLaMuQAseoDKUE962WEoCyAiMuDZh\n"
    "a09Ld7fOvu7VyZIwdnfDJg8gHMlzvR8Jwgexq6EDVoJvBYCU1O+euOg6lvvFWr9r0pTHQetO7jagcgYALlAzCCAeEwggGGoA\n"
    "MCAQICAjBHMAoGCCqGSM49BAMCMEkxEzARBgNVBAMMClByb3ZTdWJDQTIxEDAOBgNVBAoMB0VWZXJlc3QxCzAJBgNVBAYTAk\n"
    "RFMRMwEQYKCZImiZPyLGQBGRYDQ1BTMB4XDTIzMDkyNjA3MzgzNFoXDTQ4MDUxNzA3MzgzNFowRzERMA8GA1UEAwwIQ1BTIE\n"
    "xlYWYxEDAOBgNVBAoMB0VWZXJlc3QxCzAJBgNVBAYTAkRFMRMwEQYKCZImiZPyLGQBGRYDQ1BTMFkwEwYHKoZIzj0CAQYIKo\n"
    "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\n";

std::vector<uint8_t> decode_base64(const std::string& in) {
    static constexpr char TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<uint8_t> out;
    uint32_t acc = 0;
    int bits = 0;
    for (const char c : in) {
        if (c == '=' or c == '\n') {
            continue;
        }
        const auto* p = std::char_traits<char>::find(TABLE, 64, c);
        REQUIRE(p != nullptr);
        acc = (acc << 6) | static_cast<uint32_t>(p - TABLE);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
        }
    }
    return out;
}

} // namespace

SCENARIO("EVCC verifies a CertificateInstallationRes signed by another implementation") {

    GIVEN("a response captured off the wire and the V2G root that signed its CPS chain") {
        const auto response = decode_base64(RESPONSE_B64);
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

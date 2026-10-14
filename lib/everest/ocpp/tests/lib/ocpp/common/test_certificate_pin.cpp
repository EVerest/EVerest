// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <ocpp/common/certificate_pin.hpp>

namespace ocpp {
namespace {

// Two self-signed roots generated for this test. The hash data below was computed independently of
// the implementation, from the definitions in OCPP 2.0.1 part 2 section 2.6: SHA-256 over the DER
// encoded issuer name, SHA-256 over the value of the subject public key BIT STRING, and the serial
// number in lowercase hex with leading zeros stripped.
constexpr const char* ROOT_A = "-----BEGIN CERTIFICATE-----\n"
                               "MIIC/TCCAeWgAwIBAgICbcEwDQYJKoZIhvcNAQELBQAwFjEUMBIGA1UEAwwLVGVz\n"
                               "dCBSb290IGEwIBcNMjYwOTEwMDUwOTQyWhgPMjEyNjA4MTcwNTA5NDJaMBYxFDAS\n"
                               "BgNVBAMMC1Rlc3QgUm9vdCBhMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKC\n"
                               "AQEAsDJ2bXYtew3F2x3K0ssL2XYL3x7ZRyCfh02xUfFDunVE3mfaFuJSP0g2HDua\n"
                               "fV6BjRVpH+MmbPSwN/yWbQWfBd1bwjOaZgkUC4xzFH1A5C4GlMdrNPwg1hXQXzal\n"
                               "wTn9BzND2GswrvkCExLvBoiKtprFKfmrAwXmeDz8RfoKi1Glw0fsYPAXR77Sosbs\n"
                               "PqHhtbqhyuokoeNg/BHC9mJ10qAebMIep3tqMF033THASPEibdOoBUcpPUPcemxt\n"
                               "dwbKXgJspmhZ/52IJAjM78+g9a9UD2Ma5A/ST3oOJ1IR+aW6wVNDIWuydT35e/hF\n"
                               "nqTD9iKuNMzoNvgFM4gBh7mcCQIDAQABo1MwUTAdBgNVHQ4EFgQU04ERB3XwFp8Q\n"
                               "DO38NrBm63v1IwgwHwYDVR0jBBgwFoAU04ERB3XwFp8QDO38NrBm63v1IwgwDwYD\n"
                               "VR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEAL58h0pyOjKIRBGRDSaQu\n"
                               "dr8LBDlPe9vazWD6yBe3aM6j61fV2gieoDekb624bok5/mTmdnrYJF4pNEIz5cBk\n"
                               "ydeeKPn0KJ/72FHupTwNVTlG+7LCNOcMYvhrpFO90TzU+8lxrLISfHyzdTh23MzT\n"
                               "i4cU8JyDLruhPfKCxO9apFUHzZv4Z3Wvf3YfU3GjsQXkoXlFw3twJtBLAAVP7DwU\n"
                               "DHWOAKI8rAL19cw3m3kRFiWwsbHp7D3/2qQOwntCuGJzCR43DWS3wYtzmiMxhUam\n"
                               "9T1V6xe7uIzXpiqWvNYXQPK44z1glmKYKzkFg3/VoyVcNEDRNfwLwOYYLv2UMkKZ\n"
                               "pQ==\n"
                               "-----END CERTIFICATE-----\n";

constexpr const char* ROOT_B = "-----BEGIN CERTIFICATE-----\n"
                               "MIIC/TCCAeWgAwIBAgICbdUwDQYJKoZIhvcNAQELBQAwFjEUMBIGA1UEAwwLVGVz\n"
                               "dCBSb290IGIwIBcNMjYwOTEwMDUwOTQyWhgPMjEyNjA4MTcwNTA5NDJaMBYxFDAS\n"
                               "BgNVBAMMC1Rlc3QgUm9vdCBiMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKC\n"
                               "AQEA2637/t8B0Jt8yqKH5I0x6fk0m0+tIt3Uz518LKi/eGgX353lOfPzMTl9cOVC\n"
                               "ozAlQ9PseFlWCi/Nse1yNYFA575Fr1VyFxJAd2ojBaBn1pqA3chLLXG+4SLrx3TR\n"
                               "HsYmiJoSPWoe9ASq4WymUdBnVCz5TdMYU1UqUImiSKY1BAz95sUAhcJoNVEWdVRg\n"
                               "et1Z4U+6AH1K6us0sey6mKO/bgUN3itrQWcXCTtOC63mE8nqhPjLA4IbQsOLQuXK\n"
                               "DJ04mlRipoBpindoznPT3oz+Mtqk2ZGnXGDg6J35EGHU57K3p8Wc5Ipu3RqjIZH6\n"
                               "xm2IavsgP0WxTwPRqnCEQujNFwIDAQABo1MwUTAdBgNVHQ4EFgQUDWOIRv+quznr\n"
                               "A00tWviKPPaCKYAwHwYDVR0jBBgwFoAUDWOIRv+quznrA00tWviKPPaCKYAwDwYD\n"
                               "VR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEAN1s8FN7fUyV0oNk2uVDU\n"
                               "EK8HdyUk4Q4t1xLqEkGTD8/lD0SoyH4BI63rVpAuVI3pMPht+4vvkddy5x0lCOW/\n"
                               "yv8p9Fbecb8y8he5ydsqfavQ+kyP/H8OszUn1ho8xbVywAANNKt5UxHboIlnTrE4\n"
                               "kwQg6y6k+aoRNVDUNVcYtgGiwky+ft/Rd13g8CPT3QtmGTSX6WeqaTD4zp9lWbB9\n"
                               "tAu3pTNUbQ04eK0ytOF3rJlWOWLfl8rEZsc+ewA8eahVNcfgR5hgJIjdJcGv4A1+\n"
                               "dkUcTPkvG6tc4ca1b3w9LGeiiotgfMisx5nhXjxd6X8q9QIBJ7mH9BxPjQgOOiug\n"
                               "TQ==\n"
                               "-----END CERTIFICATE-----\n";

constexpr const char* ROOT_A_NAME_HASH = "b543c0b83aa209a4a74334e7437d5d14f688c96e98a2c2a316db830bfc7247a7";
constexpr const char* ROOT_A_KEY_HASH = "a2ba983d43efaf8a84834ef2cdad04d843be559afbb36013072eb7bd34673220";
constexpr const char* ROOT_A_SERIAL = "6dc1";

constexpr const char* ROOT_B_NAME_HASH = "89fafa1c961738c2a3594c818670a5e9ac5a8f3c2f407fb0f83a33aa61797398";
constexpr const char* ROOT_B_KEY_HASH = "fa6aff61197768a00bbca778ef694acaaaf0a5c50ed113a7cbab037afeedf829";
constexpr const char* ROOT_B_SERIAL = "6dd5";

CertificateHashDataType hash_data(const char* name_hash, const char* key_hash, const char* serial,
                                  const HashAlgorithmEnumType algorithm = HashAlgorithmEnumType::SHA256) {
    CertificateHashDataType data;
    data.hashAlgorithm = algorithm;
    data.issuerNameHash = name_hash;
    data.issuerKeyHash = key_hash;
    data.serialNumber = serial;
    return data;
}

TEST(CertificatePinTest, ACertificateMatchesItsOwnHashData) {
    EXPECT_TRUE(certificate_matches(ROOT_A, hash_data(ROOT_A_NAME_HASH, ROOT_A_KEY_HASH, ROOT_A_SERIAL)));
    EXPECT_TRUE(certificate_matches(ROOT_B, hash_data(ROOT_B_NAME_HASH, ROOT_B_KEY_HASH, ROOT_B_SERIAL)));
}

TEST(CertificatePinTest, ADifferentCertificateDoesNotMatch) {
    // The case the feature exists for: a connection pointed at the wrong system presents a
    // certificate signed by a root that is in the trust store but is not the one configured.
    EXPECT_FALSE(certificate_matches(ROOT_B, hash_data(ROOT_A_NAME_HASH, ROOT_A_KEY_HASH, ROOT_A_SERIAL)));
    EXPECT_FALSE(certificate_matches(ROOT_A, hash_data(ROOT_B_NAME_HASH, ROOT_B_KEY_HASH, ROOT_B_SERIAL)));
}

TEST(CertificatePinTest, EveryFieldHasToMatch) {
    EXPECT_FALSE(certificate_matches(ROOT_A, hash_data(ROOT_B_NAME_HASH, ROOT_A_KEY_HASH, ROOT_A_SERIAL)));
    EXPECT_FALSE(certificate_matches(ROOT_A, hash_data(ROOT_A_NAME_HASH, ROOT_B_KEY_HASH, ROOT_A_SERIAL)));
    EXPECT_FALSE(certificate_matches(ROOT_A, hash_data(ROOT_A_NAME_HASH, ROOT_A_KEY_HASH, ROOT_B_SERIAL)));
}

TEST(CertificatePinTest, AnAlgorithmTheStationDoesNotUseNeverMatches) {
    // Fails closed rather than quietly accepting: the charging station identifies certificates by
    // SHA-256, so hash data naming anything else cannot describe a certificate it holds.
    EXPECT_FALSE(certificate_matches(
        ROOT_A, hash_data(ROOT_A_NAME_HASH, ROOT_A_KEY_HASH, ROOT_A_SERIAL, HashAlgorithmEnumType::SHA512)));
}

TEST(CertificatePinTest, SomethingThatIsNotACertificateNeverMatches) {
    EXPECT_FALSE(certificate_matches("", hash_data(ROOT_A_NAME_HASH, ROOT_A_KEY_HASH, ROOT_A_SERIAL)));
    EXPECT_FALSE(certificate_matches("not a certificate", hash_data(ROOT_A_NAME_HASH, ROOT_A_KEY_HASH, ROOT_A_SERIAL)));
}

} // namespace
} // namespace ocpp

// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>

#include <everest/opcp/environment.hpp>
#include <everest/opcp/identifiers.hpp>
#include <everest/opcp/types.hpp>

using namespace opcp;

TEST(Types, iso_version_parsing_and_info) {
    EXPECT_EQ(iso_version_from_string("2"), IsoVersion::ISO15118_2);
    EXPECT_EQ(iso_version_from_string("ISO15118-20"), IsoVersion::ISO15118_20);
    EXPECT_EQ(iso_version_from_string(" 15118-20 "), IsoVersion::ISO15118_20);
    EXPECT_FALSE(iso_version_from_string("both").has_value());

    const auto& iso2 = info(IsoVersion::ISO15118_2);
    EXPECT_STREQ(iso2.est_segment, "ISO15118-2");
    EXPECT_STREQ(iso2.cacerts_algorithm, "secp256r1");
    EXPECT_EQ(iso2.leaf_type, evse_security::LeafCertificateType::V2G);

    const auto& iso20 = info(IsoVersion::ISO15118_20);
    EXPECT_STREQ(iso20.cacerts_algorithm, "secp521r1");
    EXPECT_STREQ(iso20.vra_iso_version, "15118-20");
    EXPECT_EQ(iso20.leaf_type, evse_security::LeafCertificateType::V2G20);
}

TEST(Types, root_type_lists) {
    std::vector<std::string> rejected;
    const auto types = parse_root_type_list("v2g, MO,oem,bogus,v2g", rejected);
    ASSERT_EQ(types.size(), 3u);
    EXPECT_EQ(types[0], RootType::V2G);
    EXPECT_EQ(types[1], RootType::MO);
    EXPECT_EQ(types[2], RootType::OEM);
    ASSERT_EQ(rejected.size(), 1u);
    EXPECT_EQ(rejected[0], "bogus");

    EXPECT_EQ(ca_certificate_type_for(RootType::V2G), evse_security::CaCertificateType::V2G);
    EXPECT_EQ(ca_certificate_type_for(RootType::MO), evse_security::CaCertificateType::MO);
    EXPECT_FALSE(ca_certificate_type_for(RootType::OEM).has_value());
}

TEST(Environment, hubject_presets_use_verified_paths) {
    const auto qa = Environment::preset("hubject-eu-qa");
    ASSERT_TRUE(qa.has_value());
    EXPECT_EQ(qa->auth_url, "https://auth.eu.plugncharge.hubject.com/oauth/token");
    EXPECT_EQ(qa->audience, "https://eu.plugncharge-qa.hubject.com");
    EXPECT_EQ(qa->simpleenroll_url(IsoVersion::ISO15118_2),
              "https://eu.plugncharge-qa.hubject.com/.well-known/cpo/simpleenroll");
    EXPECT_EQ(qa->simpleenroll_url(IsoVersion::ISO15118_20),
              "https://eu.plugncharge-qa.hubject.com/.well-known/cpo/simpleenroll/ISO15118-20");
    EXPECT_EQ(qa->simplereenroll_url(IsoVersion::ISO15118_2),
              "https://eu.plugncharge-qa.hubject.com/.well-known/cpo/simplereenroll");
    EXPECT_EQ(qa->cacerts_url(IsoVersion::ISO15118_20),
              "https://eu.plugncharge-qa.hubject.com/cpo/cacerts/ISO15118-20/secp521r1/");
    EXPECT_EQ(qa->end_entities_url(), "https://eu.plugncharge-qa.hubject.com/v1/vra/cpo/endEntities");
    EXPECT_EQ(qa->root_certs_url(), "https://eu.plugncharge-qa.hubject.com/v1/root/rootCerts");
    EXPECT_FALSE(qa->is_production());

    const auto prod = Environment::preset("hubject-us-prod");
    ASSERT_TRUE(prod.has_value());
    EXPECT_EQ(prod->api_base_url, "https://us.plugncharge.hubject.com");
    EXPECT_TRUE(prod->is_production());

    EXPECT_FALSE(Environment::preset("hubject-mars-qa").has_value());
    EXPECT_EQ(Environment::preset_names().size(), 7u);
}

TEST(Environment, custom_uses_spec_paths_and_overrides) {
    auto env = Environment::preset("custom").value();
    EXPECT_FALSE(env.validate_and_complete().empty()) << "URLs missing must be reported";
    env.api_base_url = "https://pki.example.org/";
    env.auth_url = "https://auth.example.org/oauth/token";
    env.ca = "acme-cpo";
    EXPECT_TRUE(env.validate_and_complete().empty());
    EXPECT_EQ(env.est_base_url, "https://pki.example.org/");
    EXPECT_EQ(env.audience, "https://pki.example.org/");
    EXPECT_EQ(env.simpleenroll_url(IsoVersion::ISO15118_2),
              "https://pki.example.org/.well-known/est/acme-cpo/simpleenroll/ISO15118-2");
    EXPECT_EQ(env.cacerts_url(IsoVersion::ISO15118_2),
              "https://pki.example.org/acme-cpo/cacerts/ISO15118-2/secp256r1/");

    env.est_base_url = "https://est.example.org";
    EXPECT_EQ(env.simplereenroll_url(IsoVersion::ISO15118_20),
              "https://est.example.org/.well-known/est/acme-cpo/simplereenroll/ISO15118-20");
    EXPECT_EQ(env.root_certs_url(), "https://pki.example.org/v1/root/rootCerts");
}

TEST(Identifiers, evseid_and_seccid) {
    EXPECT_TRUE(is_valid_evseid("DE*ICE*E45B*78C"));
    EXPECT_TRUE(is_valid_evseid("DEICEE45B78C"));
    EXPECT_TRUE(is_valid_evseid("DE*PNX*E12345*1"));
    EXPECT_FALSE(is_valid_evseid("DE*ICE*S45B"));
    EXPECT_FALSE(is_valid_evseid("D*ICE*E45B"));

    EXPECT_TRUE(is_valid_seccid("DE-ICE-S-00003C4D557878675645330967543476-2"));
    EXPECT_TRUE(is_valid_seccid("PI-ONX-S-MICROMEGA00000000000000000000000-7"));
    EXPECT_TRUE(is_valid_seccid("DEICES00003C4D5578786756453309675434762"));
    EXPECT_FALSE(is_valid_seccid("DE-ICE-E-00003C4D557878675645330967543476-2"));
    EXPECT_FALSE(is_valid_seccid("DE*ICE*E45B*78C"));

    EXPECT_EQ(operator_id_of("DE*ICE*E45B*78C"), "ICE");
    EXPECT_EQ(operator_id_of("PI-ONX-S-MICROMEGA00000000000000000000000-7"), "ONX");
    EXPECT_EQ(operator_id_of("DEICEE45B78C"), "ICE");
    EXPECT_EQ(operator_id_of("x"), "");
}

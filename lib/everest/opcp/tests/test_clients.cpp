// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>

#include <everest/opcp/est_client.hpp>
#include <everest/opcp/oauth2.hpp>
#include <everest/opcp/pkcs.hpp>
#include <everest/opcp/rcp_client.hpp>
#include <everest/opcp/vra_client.hpp>

#include <evse_security/evse_security.hpp>

#include "test_support.hpp"

using namespace opcp;
using namespace opcp::test;

namespace {
Environment qa() {
    return Environment::preset("hubject-eu-qa").value();
}
} // namespace

class Clients : public ::testing::Test {
protected:
    void SetUp() override {
        ensure_test_pki();
    }
    FakeHttpClient http;
};

TEST_F(Clients, oauth2_token_request_and_response) {
    http.on("POST", "https://auth.eu.plugncharge.hubject.com/oauth/token", 200,
            R"({"access_token":"abc.def.ghi","scope":"pkigateway","expires_in":86400,"token_type":"Bearer"})",
            "application/json");
    const auto token = fetch_access_token(http, qa(), "id", "secret");
    ASSERT_TRUE(token.ok) << token.error;
    EXPECT_EQ(token.access_token, "abc.def.ghi");
    EXPECT_EQ(token.expires_in, 86400);
    EXPECT_EQ(token.bearer().token, "abc.def.ghi");

    ASSERT_EQ(http.requests.size(), 1u);
    const auto body = nlohmann::json::parse(http.requests[0].body);
    EXPECT_EQ(body["client_id"], "id");
    EXPECT_EQ(body["client_secret"], "secret");
    EXPECT_EQ(body["audience"], "https://eu.plugncharge-qa.hubject.com");
    EXPECT_EQ(body["grant_type"], "client_credentials");
    EXPECT_TRUE(std::holds_alternative<NoAuth>(http.requests[0].auth));
}

TEST_F(Clients, oauth2_errors) {
    http.on("POST", "https://auth.", 401, R"({"error":"access_denied","error_description":"Unauthorized"})");
    auto token = fetch_access_token(http, qa(), "id", "wrong");
    EXPECT_FALSE(token.ok);
    EXPECT_EQ(token.http_status, 401);
    EXPECT_NE(token.error.find("Unauthorized"), std::string::npos) << token.error;

    http.scripts.clear();
    http.on("POST", "https://auth.", 200, "<html>");
    token = fetch_access_token(http, qa(), "id", "secret");
    EXPECT_FALSE(token.ok);

    http.scripts.clear();
    http.fail("POST", "https://auth.");
    token = fetch_access_token(http, qa(), "id", "secret");
    EXPECT_FALSE(token.ok);
    EXPECT_NE(token.error.find("transport"), std::string::npos) << token.error;
}

TEST_F(Clients, jwt_payload_and_roles) {
    // {"https://eu.plugncharge-qa.hubject.com/role":["CPO","OEM"],"sub":"x"} base64url encoded
    const std::string payload =
        R"({"https://eu.plugncharge-qa.hubject.com/role":["CPO","OEM"],"sub":"x","permissions":["pkigateway"]})";
    std::string b64 = strip_whitespace(evse_security::EvseSecurity::base64_encode_from_string(payload));
    for (char& c : b64) {
        if (c == '+') {
            c = '-';
        } else if (c == '/') {
            c = '_';
        }
    }
    while (!b64.empty() && b64.back() == '=') {
        b64.pop_back();
    }
    const auto decoded = decode_jwt_payload("eyJhbGciOiJSUzI1NiJ9." + b64 + ".sig");
    EXPECT_EQ(decoded["sub"], "x");
    const auto roles = jwt_roles(decoded);
    ASSERT_EQ(roles.size(), 2u);
    EXPECT_EQ(roles[0], "CPO");

    EXPECT_TRUE(decode_jwt_payload("not-a-jwt").empty());
    EXPECT_TRUE(jwt_roles(decode_jwt_payload("a.b.c")).empty());
}

TEST_F(Clients, est_simple_enroll_request_shape_and_pkcs7_response) {
    http.on("POST", "https://eu.plugncharge-qa.hubject.com/.well-known/cpo/simpleenroll", 200, pki("leaf2.p7b64"),
            "application/pkcs7-mime; smime-type=certs-only");
    EstClient est(http, qa());
    const auto result = est.simple_enroll(IsoVersion::ISO15118_2, pki("leaf2.csr"), BearerToken{"tok"});
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.certificates_pem.size(), 1u);
    EXPECT_EQ(result.http_status, 200);

    const auto* request = http.last("POST", "simpleenroll");
    ASSERT_NE(request, nullptr);
    EXPECT_EQ(request->url, "https://eu.plugncharge-qa.hubject.com/.well-known/cpo/simpleenroll");
    EXPECT_EQ(request->body, pem_csr_to_base64_der(pki("leaf2.csr")));
    bool content_type = false;
    for (const auto& [name, value] : request->headers) {
        if (name == "Content-Type") {
            EXPECT_EQ(value, "application/pkcs10");
            content_type = true;
        }
    }
    EXPECT_TRUE(content_type);
    ASSERT_TRUE(std::holds_alternative<BearerToken>(request->auth));
    EXPECT_EQ(std::get<BearerToken>(request->auth).token, "tok");
}

TEST_F(Clients, est_reenroll_uses_client_certificate_and_iso20_path) {
    http.on("POST", "https://eu.plugncharge-qa.hubject.com/.well-known/cpo/simplereenroll/ISO15118-20", 200,
            pki("leaf20.p7b64"));
    EstClient est(http, qa());
    const ClientCertificate cert{"/certs/leaf.pem", "/certs/leaf.key", std::nullopt};
    const auto result = est.simple_reenroll(IsoVersion::ISO15118_20, pki("leaf20.csr"), cert);
    ASSERT_TRUE(result.ok) << result.error;
    const auto* request = http.last("POST", "simplereenroll");
    ASSERT_NE(request, nullptr);
    ASSERT_TRUE(std::holds_alternative<ClientCertificate>(request->auth));
    EXPECT_EQ(std::get<ClientCertificate>(request->auth).key_path, "/certs/leaf.key");
}

TEST_F(Clients, est_error_mapping) {
    EstClient est(http, qa());
    http.on("POST", "https://eu.plugncharge-qa.hubject.com/.well-known/cpo/simpleenroll", 403, "Forbidden");
    auto result = est.simple_enroll(IsoVersion::ISO15118_2, pki("leaf2.csr"), BearerToken{"tok"});
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.auth_rejected);
    EXPECT_EQ(result.http_status, 403);

    http.scripts.clear();
    http.on("POST", "https://eu.plugncharge-qa.hubject.com/.well-known/cpo/simpleenroll", 200, "not base64 pkcs7!!");
    result = est.simple_enroll(IsoVersion::ISO15118_2, pki("leaf2.csr"), BearerToken{"tok"});
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.auth_rejected);
    EXPECT_NE(result.error.find("PKCS#7"), std::string::npos) << result.error;

    result = est.simple_enroll(IsoVersion::ISO15118_2, "not a csr", BearerToken{"tok"});
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.http_status, 0);

    http.scripts.clear();
    http.fail("GET", "https://eu.plugncharge-qa.hubject.com/cpo/cacerts/");
    result = est.cacerts(IsoVersion::ISO15118_2, BearerToken{"tok"});
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("transport"), std::string::npos);
}

TEST_F(Clients, est_cacerts) {
    http.on("GET", "https://eu.plugncharge-qa.hubject.com/cpo/cacerts/ISO15118-2/secp256r1/", 200,
            pki("cacerts.p7b64"));
    EstClient est(http, qa());
    const auto result = est.cacerts(IsoVersion::ISO15118_2, BearerToken{"tok"});
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.certificates_pem.size(), 2u);
}

TEST_F(Clients, rcp_parse_and_filter) {
    const std::string body = R"({"RootCertificateCollection":{"rootCertificates":[
        {"rootCertificateId":"b1b86334","distinguishedName":"CN=V2G Root CA Test","caCertificate":")" +
                             pki("root.derb64") +
                             R"(","commonName":"V2G Root CA Test","validTo":"2059-01-09T19:00:00Z",
         "organizationName":"EVerest Test","rootType":"V2G","xsdMsgDefNamespace":"urn:iso:15118:2:2013:MsgDef"},
        {"rootCertificateId":"broken","caCertificate":"AAAA","rootType":"MO"},
        {"rootCertificateId":"weird","caCertificate":")" +
                             pki("root.derb64") + R"(","rootType":"PE"}]}})";
    http.on("GET", "https://eu.plugncharge-qa.hubject.com/v1/root/rootCerts?rootType=v2g", 200, body);
    RcpClient rcp(http, qa());
    const auto result = rcp.get_root_certificates(RootType::V2G, BearerToken{"tok"});
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.roots.size(), 1u);
    EXPECT_EQ(result.roots[0].common_name, "V2G Root CA Test");
    EXPECT_EQ(result.roots[0].root_type, RootType::V2G);
    EXPECT_EQ(result.roots[0].xsd_namespace, "urn:iso:15118:2:2013:MsgDef");
    EXPECT_NE(result.roots[0].pem.find("BEGIN CERTIFICATE"), std::string::npos);
    EXPECT_NE(result.error.find("skipped"), std::string::npos) << "broken entries are reported, not fatal";

    http.scripts.clear();
    http.on("GET", "https://eu.plugncharge-qa.hubject.com/v1/root/rootCerts", 401, "");
    const auto denied = rcp.get_root_certificates(std::nullopt, ClientCertificate{"c", "k", std::nullopt});
    EXPECT_FALSE(denied.ok);
    EXPECT_TRUE(denied.auth_rejected);

    std::vector<RootCertificate> out;
    std::string error;
    EXPECT_FALSE(RcpClient::parse_response("[1,2", out, error));
    EXPECT_FALSE(RcpClient::parse_response("{\"foo\":1}", out, error));
}

TEST_F(Clients, vra_registration_body) {
    EndEntity entity;
    entity.common_name = "DE*PNX*E12345*1";
    entity.manufacturer = "Pionix";
    entity.device_name = "Charger";
    entity.device_sw_version = "1.2";
    entity.evse_serial_number = "SN1";
    entity.iso_version = IsoVersion::ISO15118_20;
    entity.ocpp_version = "2.0.1";
    entity.charge_box_serial_number = "CB1";
    const auto json = nlohmann::json::parse(VraClient::to_json(entity));
    EXPECT_EQ(json["commonName"], "DE*PNX*E12345*1");
    EXPECT_EQ(json["manufacture"], "Pionix");
    EXPECT_EQ(json["evseISOversion"], "15118-20");
    ASSERT_EQ(json["evseID"].size(), 1u);
    EXPECT_EQ(json["evseID"][0], "DE*PNX*E12345*1") << "defaults to the common name";

    http.on("PUT", "https://eu.plugncharge-qa.hubject.com/v1/vra/cpo/endEntities", 200, "{}");
    VraClient vra(http, qa());
    const auto result = vra.register_end_entity(entity, BearerToken{"tok"});
    EXPECT_TRUE(result.ok) << result.error;
    ASSERT_EQ(http.requests.size(), 1u);
    EXPECT_EQ(http.requests[0].method, "PUT");

    http.scripts.clear();
    http.on("PUT", "https://eu.plugncharge-qa.hubject.com/v1/vra/cpo/endEntities", 403, "operator not allowed");
    const auto denied = vra.register_end_entity(entity, BearerToken{"tok"});
    EXPECT_FALSE(denied.ok);
    EXPECT_TRUE(denied.auth_rejected);
    EXPECT_NE(denied.error.find("operator not allowed"), std::string::npos);
}

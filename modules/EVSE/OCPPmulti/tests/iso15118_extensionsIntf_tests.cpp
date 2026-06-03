// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// cmds:
//   set_get_certificate_response:
//
// vars:
//   iso15118_certificate_request:
//   charging_needs:
//   ev_info:                          <not used>
//   service_renegotiation_supported:

// TODO(james-ctc):
// var tests need to be done as part of GenericOcpp::init_evse_subscribe()

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <generic_ocpp.hpp>

#include "stubs/generic_ocpp_stub.hpp"

namespace {
using namespace stubs;
using ::testing::_;
using ::testing::Return;

TEST_F(GenericOcppRequiresTester, callSetGetCertificateResponse) {
    // call_set_get_certificate_response() used in
    // cb_iso15118_certificate_request

    using ocpp::v2::Get15118EVCertificateResponse;
    using ocpp::v2::Iso15118EVCertificateStatusEnum;
    using types::iso15118::CertificateActionEnum;

    std::vector<json> received;
    interfaces->subscribe_var("iso15118_extensions", "call_set_get_certificate_response",
                              [&received](const auto&, const auto&, const auto& data) { received.push_back(data); });

    types::iso15118::RequestExiStreamSchema certificate_request;
    certificate_request.exi_request = "1234567890";
    certificate_request.iso15118_schema_version = "v2";
    certificate_request.certificate_action = CertificateActionEnum::Install;

    ocpp::v2::Get15118EVCertificateResponse response{};
    response.status = Iso15118EVCertificateStatusEnum::Failed;
    response.exiResponse = "0987654321";
    // std::optional<StatusInfo> statusInfo;
    // std::optional<std::int32_t> remainingContracts;
    // std::optional<CustomData> customData;

    EXPECT_CALL(chargepoint, on_get_15118_ev_certificate_request(0, _)).Times(1);

    ocpp->cb_iso15118_certificate_request(0, certificate_request);
    ASSERT_EQ(received.size(), 0);
    ocpp->cb_get_15118_ev_certificate_response(0, response, ocpp::v2::CertificateActionEnum::Install);

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(
        received[0],
        R"({"certificate_response":{"certificate_action":"Install","exi_response":"0987654321","status":"Failed"}})"_json);
}

} // namespace

// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <iso15118/detail/received_message_type.hpp>
#include <iso15118/message/certificate_installation.hpp>
#include <iso15118/message/dc_cable_check.hpp>
#include <iso15118/message/decode.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/message/variant.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;

namespace {

template <typename Message> std::vector<uint8_t> encode(const Message& message) {
    std::vector<uint8_t> buffer(16384);
    const io::StreamOutputView out{buffer.data(), buffer.size()};
    buffer.resize(message_20::serialize(message, out));
    return buffer;
}

const message_20::Header header{{1, 2, 3, 4, 5, 6, 7, 8}, 1691411800};

std::vector<uint8_t> session_setup_req() {
    message_20::SessionSetupRequest req;
    req.header = header;
    req.evccid = "WMIV1234567890ABCDEX";
    return encode(req);
}

std::vector<uint8_t> session_setup_res() {
    message_20::SessionSetupResponse res;
    res.header = header;
    res.response_code = dt::ResponseCode::OK_NewSessionEstablished;
    res.evseid = "DE*PNX*E12345*1";
    return encode(res);
}

std::vector<uint8_t> cable_check_res() {
    message_20::DC_CableCheckResponse res;
    res.header = header;
    res.response_code = dt::ResponseCode::OK;
    res.processing = dt::Processing::Finished;
    return encode(res);
}

std::vector<uint8_t> sap_req() {
    message_20::SupportedAppProtocolRequest req;
    req.app_protocol.push_back({"urn:iso:std:iso:15118:-20:DC", 1, 0, 1, 1});
    return encode(req);
}

std::vector<uint8_t> sap_res() {
    message_20::SupportedAppProtocolResponse res;
    res.response_code = message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation;
    res.schema_id = 1;
    return encode(res);
}

std::vector<uint8_t> certificate_installation_res() {
    message_20::CertificateInstallationResponse res;
    res.header = header;
    res.response_code = dt::ResponseCode::OK;
    res.evse_processing = dt::Processing::Finished;
    res.cps_certificate_chain.certificate = std::vector<uint8_t>(64, 0x30);
    res.signed_installation_data.id = "sid";
    res.signed_installation_data.contract_certificate_chain.certificate = std::vector<uint8_t>(64, 0x31);
    res.signed_installation_data.contract_certificate_chain.sub_certificates.emplace_back(
        std::vector<uint8_t>(64, 0x32));
    res.signed_installation_data.dh_public_key = std::vector<uint8_t>(133, 0x04);
    res.signed_installation_data.secp521_encrypted_private_key = std::vector<uint8_t>(94, 0xab);
    res.remaining_contract_certificate_chains = 2;
    return encode(res);
}

// Captured DC, AC, DER IEC and DER SAE frames from the per-message EXI tests.
const std::vector<uint8_t> cable_check_req{0x80, 0x2c, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7,
                                           0x6c, 0x4d, 0x8c, 0x7b, 0xfe, 0x1b, 0x60, 0x62};
const std::vector<uint8_t> ac_charge_loop_req{
    0x80, 0x08, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0xdb, 0xfe, 0x1b, 0x60, 0x62, 0x88, 0x04,
    0x00, 0x5c, 0xb0, 0x00, 0x40, 0x07, 0x02, 0xe8, 0x04, 0x00, 0x00, 0x80, 0x7e, 0x08, 0x01, 0x90, 0x10, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x3f, 0x06, 0x80, 0x78, 0x10, 0x00, 0x00, 0x04, 0x00, 0x00, 0x02, 0x00, 0x38, 0x17, 0x40,
    0x40, 0x00, 0x00, 0x08, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00};
const std::vector<uint8_t> ac_charge_loop_res{
    0x80, 0x0c, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0xeb, 0xfe, 0x1b, 0x60,
    0x62, 0x00, 0x10, 0x0c, 0xc4, 0x69, 0x04, 0xb1, 0x20, 0x00, 0xc8, 0x80, 0x40, 0x07, 0x02, 0xe8,
    0x04, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x08, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x20, 0x00,
    0x00, 0x04, 0x00, 0x70, 0x2e, 0x81, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00};
const std::vector<uint8_t> der_iec_charge_loop_req{
    0x80, 0x08, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0x3b, 0xfe, 0x1b, 0x60, 0x62, 0x86,
    0x90, 0x60, 0x32, 0x08, 0x30, 0x1e, 0x04, 0x18, 0x02, 0x81, 0xf8, 0x20, 0x06, 0x48, 0x7e, 0x0d, 0x00, 0xf2,
    0x1f, 0x83, 0x70, 0x2c, 0x88, 0x00, 0x00, 0x21, 0xf8, 0x20, 0x06, 0x48, 0x7e, 0x0d, 0x00, 0xf2, 0x00, 0x06};
const std::vector<uint8_t> der_iec_charge_loop_res{0x80, 0x0c, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d,
                                                   0x89, 0x1c, 0x0e, 0x1b, 0x60, 0x62, 0x00, 0x78, 0x3f, 0x04, 0x00,
                                                   0xc9, 0x03, 0xf0, 0x40, 0x0c, 0x90, 0xfc, 0x10, 0x03, 0x23, 0x80};
const std::vector<uint8_t> der_sae_charge_loop_req{0x80, 0x08, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c,
                                                   0x4d, 0x8c, 0x3b, 0xfe, 0x1b, 0x60, 0x62, 0x89, 0x24, 0x18,
                                                   0x04, 0x94, 0x80, 0x0e, 0x60, 0x10, 0x40, 0x01, 0xe1, 0x81,
                                                   0x36, 0x1d, 0xff, 0x0d, 0xb0, 0x31, 0x80, 0x00, 0x18};
const std::vector<uint8_t> der_sae_charge_loop_res{0x80, 0x0c, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d,
                                                   0x8c, 0x4b, 0xfe, 0x1b, 0x60, 0x62, 0x00, 0x89, 0x89, 0xe8, 0x00};

const std::vector<uint8_t> garbage{0xde, 0xad, 0xbe, 0xef, 0x00, 0x11};

message_20::Type received(v2gtp::PayloadType payload_type, const std::vector<uint8_t>& bytes) {
    const io::StreamInputView view{bytes.data(), bytes.size()};
    const message_20::RequestVariant request{payload_type, view};
    return message_20::received_message_type(request, payload_type, view);
}

template <typename Message>
std::optional<Message> decode(v2gtp::PayloadType payload_type, const std::vector<uint8_t>& bytes) {
    return message_20::decode<Message>(payload_type, io::StreamInputView{bytes.data(), bytes.size()});
}

message_20::Type peek(v2gtp::PayloadType payload_type, const std::vector<uint8_t>& bytes) {
    return message_20::peek_type(payload_type, io::StreamInputView{bytes.data(), bytes.size()});
}

} // namespace

SCENARIO("decode<CertificateInstallationResponse> takes a Part20Main CertificateInstallationRes only") {
    using Res = message_20::CertificateInstallationResponse;
    using PT = v2gtp::PayloadType;

    const auto res = decode<Res>(PT::Part20Main, certificate_installation_res());
    REQUIRE(res.has_value());
    CHECK(res->header.session_id == header.session_id);
    CHECK(res->header.timestamp == header.timestamp);
    CHECK(res->response_code == dt::ResponseCode::OK);
    CHECK(res->evse_processing == dt::Processing::Finished);
    CHECK(res->signed_installation_data.id == "sid");
    CHECK(res->remaining_contract_certificate_chains == 2);

    CHECK_FALSE(decode<Res>(PT::Part20Main, session_setup_req()).has_value());
    CHECK_FALSE(decode<Res>(PT::Part20Main, session_setup_res()).has_value());
    CHECK_FALSE(decode<Res>(PT::Part20DC, certificate_installation_res()).has_value());
    CHECK_FALSE(decode<Res>(PT::SAP, certificate_installation_res()).has_value());
    CHECK_FALSE(decode<Res>(PT::Part20Main, garbage).has_value());
    CHECK_FALSE(decode<Res>(PT::Part20Main, {}).has_value());
}

SCENARIO("decode reads every response kind in its own payload type") {
    using PT = v2gtp::PayloadType;

    const auto setup = decode<message_20::SessionSetupResponse>(PT::Part20Main, session_setup_res());
    REQUIRE(setup.has_value());
    CHECK(setup->evseid == "DE*PNX*E12345*1");

    const auto cable_check = decode<message_20::DC_CableCheckResponse>(PT::Part20DC, cable_check_res());
    REQUIRE(cable_check.has_value());
    CHECK(cable_check->processing == dt::Processing::Finished);

    const auto sap = decode<message_20::SupportedAppProtocolResponse>(PT::SAP, sap_res());
    REQUIRE(sap.has_value());
    CHECK(sap->schema_id == 1);

    CHECK_FALSE(decode<message_20::SupportedAppProtocolResponse>(PT::SAP, sap_req()).has_value());
}

SCENARIO("received_message_type and peek_type name what arrived") {
    using message_20::Type;
    using PT = v2gtp::PayloadType;

    struct Row {
        PT payload_type;
        std::vector<uint8_t> bytes;
        Type expected;
    };
    const std::vector<Row> rows{
        {PT::Part20Main, session_setup_req(), Type::SessionSetupReq},
        {PT::Part20Main, session_setup_res(), Type::SessionSetupRes},
        {PT::Part20Main, certificate_installation_res(), Type::CertificateInstallationRes},
        {PT::Part20DC, cable_check_req, Type::DC_CableCheckReq},
        {PT::Part20DC, cable_check_res(), Type::DC_CableCheckRes},
        {PT::SAP, sap_req(), Type::SupportedAppProtocolReq},
        {PT::SAP, sap_res(), Type::SupportedAppProtocolRes},
        {PT::Part20AC, ac_charge_loop_req, Type::AC_ChargeLoopReq},
        {PT::Part20AC, ac_charge_loop_res, Type::AC_ChargeLoopRes},
        {PT::Part20DerIec, der_iec_charge_loop_req, Type::DER_AC_ChargeLoopReq},
        {PT::Part20DerIec, der_iec_charge_loop_res, Type::DER_AC_ChargeLoopRes},
        {PT::Part20DerSae, der_sae_charge_loop_req, Type::DER_SAE_AC_ChargeLoopReq},
        {PT::Part20DerSae, der_sae_charge_loop_res, Type::DER_SAE_AC_ChargeLoopRes},
        {PT::Part20Main, garbage, Type::None},
        {PT::SAP, garbage, Type::None},
        {PT::Part20DC, session_setup_res(), Type::None},
        {PT::Part20AC, der_sae_charge_loop_res, Type::None},
        {PT::Part20Main, {}, Type::None},
    };
    for (const auto& row : rows) {
        CHECK(received(row.payload_type, row.bytes) == row.expected);
        CHECK(peek(row.payload_type, row.bytes) == row.expected);
    }
}

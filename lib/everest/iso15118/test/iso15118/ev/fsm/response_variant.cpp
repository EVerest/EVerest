// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <array>
#include <type_traits>
#include <vector>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/engine.hpp>
#include <iso15118/ev/d20/state/supported_app_protocol.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/ev/session/latches.hpp>
#include <iso15118/io/logging.hpp>
#include <iso15118/message/ac_charge_loop.hpp>
#include <iso15118/message/ac_charge_parameter_discovery.hpp>
#include <iso15118/message/ac_der_iec_charge_loop.hpp>
#include <iso15118/message/ac_der_iec_charge_parameter_discovery.hpp>
#include <iso15118/message/ac_der_sae_charge_loop.hpp>
#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/authorization_setup.hpp>
#include <iso15118/message/certificate_installation.hpp>
#include <iso15118/message/dc_cable_check.hpp>
#include <iso15118/message/dc_charge_loop.hpp>
#include <iso15118/message/dc_charge_parameter_discovery.hpp>
#include <iso15118/message/dc_pre_charge.hpp>
#include <iso15118/message/dc_welding_detection.hpp>
#include <iso15118/message/payload_type.hpp>
#include <iso15118/message/power_delivery.hpp>
#include <iso15118/message/response_variant.hpp>
#include <iso15118/message/schedule_exchange.hpp>
#include <iso15118/message/service_detail.hpp>
#include <iso15118/message/service_discovery.hpp>
#include <iso15118/message/service_selection.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/session_stop.hpp>
#include <iso15118/message/supported_app_protocol.hpp>

using namespace iso15118;

TEST_CASE("ISO 15118-20 response decoder preserves SessionSetupRes", "[response-variant]") {
    message_20::SessionSetupResponse response{};
    response.header.session_id = {1, 2, 3, 4, 5, 6, 7, 8};
    response.header.timestamp = 123456;
    response.response_code = message_20::datatypes::ResponseCode::OK_NewSessionEstablished;
    response.evseid = "DE*PNX*E12345";
    std::array<uint8_t, 4096> bytes{};
    const auto size = message_20::serialize(response, io::StreamOutputView{bytes.data(), bytes.size()});
    REQUIRE(size > 0);

    const message_20::ResponseVariant decoded{v2gtp::PayloadType::Part20Main, io::StreamInputView{bytes.data(), size}};
    REQUIRE(decoded.get_error().empty());
    REQUIRE(decoded.get_type() == message_20::Type::SessionSetupRes);
    const auto& result = decoded.get<message_20::SessionSetupResponse>();
    REQUIRE(result.header.session_id == response.header.session_id);
    REQUIRE(result.header.timestamp == response.header.timestamp);
    REQUIRE(result.response_code == response.response_code);
    REQUIRE(result.evseid == response.evseid);
    REQUIRE(decoded.get_if<message_20::SessionSetupResponse>() == &result);
    REQUIRE(decoded.get_if<message_20::SessionSetupRequest>() == nullptr);
    REQUIRE_THROWS_AS(decoded.get<message_20::SessionSetupRequest>(), std::runtime_error);
    REQUIRE(decoded.get_exi_payload().empty());
}

namespace {
template <typename Message> auto encode(const Message& message) {
    std::vector<uint8_t> bytes(8192);
    const auto size = message_20::serialize(message, io::StreamOutputView{bytes.data(), bytes.size()});
    REQUIRE(size > 0);
    bytes.resize(size);
    return bytes;
}

template <typename Response> void check_response(Response response) {
    response.header.session_id = {1, 2, 3, 4, 5, 6, 7, 8};
    response.header.timestamp = 123456;
    response.response_code = message_20::datatypes::ResponseCode::OK;
    const auto bytes = encode(response);
    const message_20::ResponseVariant decoded{message_20::PayloadTypeTrait<Response>::type,
                                              io::StreamInputView{bytes.data(), bytes.size()}};
    INFO(decoded.get_error());
    REQUIRE(decoded.get_error().empty());
    REQUIRE(decoded.get_type() == message_20::TypeTrait<Response>::type);
    const auto& result = decoded.get<Response>();
    REQUIRE(result.header.session_id == response.header.session_id);
    REQUIRE(result.header.timestamp == response.header.timestamp);
    REQUIRE(result.response_code == response.response_code);
    REQUIRE(decoded.get_if<Response>() == &result);
    REQUIRE(decoded.get_exi_payload().empty());

    const message_20::ResponseVariant direct{response};
    REQUIRE(direct.get_type() == decoded.get_type());
    REQUIRE(direct.get<Response>().header.session_id == response.header.session_id);
    REQUIRE(direct.get_exi_payload().empty());
}
} // namespace

TEMPLATE_TEST_CASE("ISO 15118-20 common responses decode", "[response-variant]", message_20::AuthorizationSetupResponse,
                   message_20::AuthorizationResponse, message_20::CertificateInstallationResponse,
                   message_20::ServiceDiscoveryResponse, message_20::ServiceDetailResponse,
                   message_20::ServiceSelectionResponse, message_20::ScheduleExchangeResponse,
                   message_20::PowerDeliveryResponse, message_20::SessionStopResponse) {
    TestType response{};
    if constexpr (std::is_same_v<TestType, message_20::AuthorizationSetupResponse>) {
        response.authorization_services = {message_20::datatypes::Authorization::EIM};
        response.authorization_mode = message_20::datatypes::EIM_ASResAuthorizationMode{};
    } else if constexpr (std::is_same_v<TestType, message_20::CertificateInstallationResponse>) {
        response.signed_installation_data.id = "id";
        response.signed_installation_data.contract_certificate_chain.sub_certificates.emplace_back();
    }
    check_response(response);
}

TEMPLATE_TEST_CASE("ISO 15118-20 energy transfer responses decode", "[response-variant]",
                   message_20::DC_ChargeParameterDiscoveryResponse, message_20::DC_CableCheckResponse,
                   message_20::DC_PreChargeResponse, message_20::DC_ChargeLoopResponse,
                   message_20::DC_WeldingDetectionResponse, message_20::AC_ChargeParameterDiscoveryResponse,
                   message_20::AC_ChargeLoopResponse, message_20::DER_AC_ChargeParameterDiscoveryResponse,
                   message_20::DER_AC_ChargeLoopResponse, message_20::DER_SAE_AC_ChargeLoopResponse) {
    check_response(TestType{});
}

TEST_CASE("ISO 15118-20 SAP response decodes", "[response-variant]") {
    const message_20::SupportedAppProtocolResponse response{
        message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation, 7};
    const auto bytes = encode(response);
    const message_20::ResponseVariant decoded{v2gtp::PayloadType::SAP, io::StreamInputView{bytes.data(), bytes.size()}};
    REQUIRE(decoded.get_type() == message_20::Type::SupportedAppProtocolRes);
    REQUIRE(decoded.get_error().empty());
    REQUIRE(decoded.get<message_20::SupportedAppProtocolResponse>().schema_id == response.schema_id);
    REQUIRE(decoded.get<message_20::SupportedAppProtocolResponse>().response_code == response.response_code);
    REQUIRE(decoded.get_exi_payload().empty());
}

TEST_CASE("ISO 15118-20 SAE DER charge parameter response decodes", "[response-variant]") {
    const std::vector<uint8_t> cpd_res_bytes = {
        0x80, 0x14, 0x04, 0x1e, 0xa6, 0x5f, 0xc9, 0x9b, 0xa7, 0x6c, 0x4d, 0x8c, 0x4b, 0xfe, 0x1b, 0x60, 0x62, 0x00,
        0x84, 0x18, 0x0b, 0x11, 0x00, 0x0c, 0x84, 0x40, 0x01, 0xe1, 0x41, 0x09, 0x18, 0x00, 0x1f, 0xc0, 0x08, 0x10,
        0x01, 0x40, 0x04, 0x02, 0x00, 0x00, 0x41, 0x00, 0x11, 0x00, 0x41, 0x84, 0x8c, 0x00, 0x0f, 0xe0, 0x04, 0x08,
        0x00, 0xa0, 0x01, 0x01, 0x00, 0x00, 0x40, 0x80, 0x0c, 0x40, 0x10, 0xd0, 0x24, 0x60, 0x40, 0x7f, 0x00, 0x50,
        0x40, 0x01, 0xf8, 0x08, 0x00, 0x01, 0x04, 0x00, 0x1f, 0x06, 0x12, 0x30, 0x20, 0x3f, 0x80, 0x28, 0x20, 0x00,
        0xe4, 0x04, 0x00, 0x00, 0x82, 0x00, 0x0e, 0x83, 0x42, 0x10, 0x01, 0xfa, 0x02, 0x08, 0x00, 0xcf, 0x01, 0x04,
        0x00, 0x1e, 0x82, 0x00, 0x0e, 0xc6, 0x12, 0x3f, 0x02, 0xf9, 0x08, 0x48, 0x02, 0x01, 0x00, 0x19, 0xe0, 0x20,
        0x83, 0x00, 0x30, 0x10, 0x01, 0xfa, 0x02, 0x08, 0x31, 0x02, 0x0c, 0x40, 0x00, 0x28, 0x90, 0x01, 0xcc, 0x02,
        0x01, 0x58, 0x04, 0x09, 0x10, 0x40, 0x20, 0xc0, 0x14, 0x10, 0x60, 0x02, 0x02, 0x0c, 0x02, 0xc1, 0x06, 0x00,
        0x41, 0x82, 0x48, 0x30, 0x02, 0x22, 0x00, 0x98, 0x90, 0x02, 0x02, 0x00, 0x3f, 0x40, 0x41, 0x06, 0x01, 0x60,
        0x20, 0x02, 0x20, 0x08, 0x10, 0x60, 0x0a, 0x18, 0x80, 0x00, 0x51, 0x12, 0x41, 0x80, 0x29, 0x08, 0x12, 0x50,
        0x33, 0x10, 0x60, 0x1e, 0x42, 0x0c, 0x01, 0x48, 0x83, 0x00, 0x52, 0x20, 0xc0, 0x14, 0x88, 0x30, 0x05, 0x20,
        0x80, 0x03, 0xc0, 0x40, 0x07, 0x30, 0x08, 0x20, 0x00, 0x00, 0x88, 0x00, 0x88, 0x02, 0x04, 0x00, 0x62, 0x00,
        0x80, 0x03, 0x12, 0xff, 0x86, 0xd8, 0x18, 0x00};
    const message_20::ResponseVariant decoded{v2gtp::PayloadType::Part20DerSae,
                                              io::StreamInputView{cpd_res_bytes.data(), cpd_res_bytes.size()}};
    REQUIRE(decoded.get_type() == message_20::Type::DER_SAE_AC_ChargeParameterDiscoveryRes);
    REQUIRE(decoded.get_error().empty());
    const auto& response = decoded.get<message_20::DER_SAE_AC_ChargeParameterDiscoveryResponse>();
    REQUIRE(response.header.session_id == std::array<uint8_t, 8>{0x3D, 0x4C, 0xBF, 0x93, 0x37, 0x4E, 0xD8, 0x9B});
    REQUIRE(response.header.timestamp == 1725456324);
    REQUIRE(message_20::datatypes::from_RationalNumber(response.transfer_mode.max_charge_power) == 22000);
}

TEMPLATE_TEST_CASE("ISO 15118-20 response decoder rejects requests", "[response-variant]",
                   message_20::SessionSetupRequest, message_20::DC_CableCheckRequest, message_20::AC_ChargeLoopRequest,
                   message_20::DER_AC_ChargeLoopRequest, message_20::DER_SAE_AC_ChargeLoopRequest,
                   message_20::SupportedAppProtocolRequest) {
    TestType request{};
    if constexpr (std::is_same_v<TestType, message_20::SupportedAppProtocolRequest>) {
        request.app_protocol = {{"urn:iso:std:iso:15118:-20:DC", 1, 0, 1, 1}};
    }
    const auto bytes = encode(request);
    const message_20::ResponseVariant decoded{message_20::PayloadTypeTrait<TestType>::type,
                                              io::StreamInputView{bytes.data(), bytes.size()}};
    REQUIRE(decoded.get_type() == message_20::Type::None);
    REQUIRE(decoded.get_error() == "chosen message type unhandled");
    REQUIRE(decoded.get_if<TestType>() == nullptr);
    REQUIRE_THROWS_WITH(decoded.get<TestType>(), "Illegal message type access");
    REQUIRE(decoded.get_exi_payload().empty());
}

TEST_CASE("ISO 15118-20 response decoder reports empty and malformed payloads", "[response-variant]") {
    using v2gtp::PayloadType;
    for (auto family : {PayloadType::SAP, PayloadType::Part20Main, PayloadType::Part20DC, PayloadType::Part20AC,
                        PayloadType::Part20DerIec, PayloadType::Part20DerSae}) {
        CAPTURE(family);
        const std::array<uint8_t, 4> bytes{0xff, 0xff, 0xff, 0xff};
        for (const auto view :
             {io::StreamInputView{nullptr, 0}, io::StreamInputView{nullptr, 4}, io::StreamInputView{bytes.data(), 0}}) {
            const message_20::ResponseVariant decoded{family, view};
            REQUIRE(decoded.get_type() == message_20::Type::None);
            REQUIRE(decoded.get_error() == "empty EXI payload");
        }
        const message_20::ResponseVariant decoded{family, io::StreamInputView{bytes.data(), bytes.size()}};
        REQUIRE(decoded.get_type() == message_20::Type::None);
        REQUIRE(decoded.get_error().find("failed with") != std::string::npos);
        if (family == PayloadType::Part20AC) {
            REQUIRE(decoded.get_error().find("decode_iso20_ac_exiDocument failed with ") == 0);
        }
        REQUIRE(decoded.get_if<message_20::SessionSetupResponse>() == nullptr);
        REQUIRE_THROWS_WITH(decoded.get<message_20::SessionSetupResponse>(), "Illegal message type access");
    }
}

TEST_CASE("ISO 15118-20 EV engine stages only response bodies", "[response-variant]") {
    ev::session::Latches latches;
    everest::lib::util::monitor<ev::params::DcChargeParams> dc_params{ev::params::DcChargeParams{}};
    everest::lib::util::monitor<ev::params::AcChargeParams> ac_params{ev::params::AcChargeParams{}};
    ev::d20::Engine engine{{},        "EVTESTID01", {{"urn:iso:std:iso:15118:-20:DC", 1, 0, 1, 1}}, latches,
                           dc_params, ac_params,    message_20::datatypes::ServiceCategory::DC,     {}};
    engine.start();
    const auto request = engine.take_request();
    REQUIRE(request.has_value());
    const io::StreamInputView view{request->first.data(), request->first.size()};

    REQUIRE_FALSE(engine.stage_response(static_cast<v2gtp::PayloadType>(0xffff), view));
    REQUIRE_FALSE(engine.is_stopped());
    // [V2G20-440]: a request body is not a valid response, so it is ignored rather than staged.
    REQUIRE_FALSE(engine.stage_response(request->second, view));
    REQUIRE(std::get<message_20::Type>(engine.peek_response_type()) == message_20::Type::None);
    REQUIRE_FALSE(engine.is_stopped());
}

TEST_CASE("ISO 15118-20 EV reports an undecodable response once", "[response-variant]") {
    const LogCapture logs{};
    const ev::feedback::Callbacks callbacks{};
    FsmStateHelper helper{callbacks};
    auto& ctx = helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    const std::array<uint8_t, 4> bytes{0xff, 0xff, 0xff, 0xff};
    const message_20::ResponseVariant variant{v2gtp::PayloadType::Part20Main,
                                              io::StreamInputView{bytes.data(), bytes.size()}};
    const auto* res = ev::d20::expect_response<message_20::SessionSetupResponse>(ctx, variant);
    const auto errors = logs.messages(LogLevel::Error);

    REQUIRE(res == nullptr);
    REQUIRE(ctx.is_session_stopped());
    REQUIRE(errors.size() == 1);
    REQUIRE(errors.front().find(variant.get_error()) != std::string::npos);
}

TEST_CASE("ISO 15118-20 EV reports an undecodable SupportedAppProtocolRes once", "[response-variant]") {
    const LogCapture logs{};
    const ev::feedback::Callbacks callbacks{};
    FsmStateHelper helper{callbacks};
    auto& ctx = helper.get_context();
    ev::d20::state::SupportedAppProtocol state{ctx};

    const std::array<uint8_t, 4> bytes{0xff, 0xff, 0xff, 0xff};
    auto variant = std::make_unique<message_20::ResponseVariant>(v2gtp::PayloadType::SAP,
                                                                 io::StreamInputView{bytes.data(), bytes.size()});
    const auto error = variant->get_error();
    helper.get_message_exchange().set_response(std::move(variant));
    const auto result = state.feed(ev::d20::Event::V2GTP_MESSAGE);
    const auto errors = logs.messages(LogLevel::Error);

    REQUIRE(result.new_state == nullptr);
    REQUIRE(ctx.is_session_stopped());
    REQUIRE(errors.size() == 1);
    REQUIRE(errors.front().find(error) != std::string::npos);
}

// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <array>
#include <cbv2g/din/din_msgDefDecoder.h>
#include <cbv2g/iso_2/iso2_msgDefDecoder.h>
#include <chrono>
#include <iso15118/detail/message_2/decode_response.hpp>
#include <iso15118/detail/message_din/decode_response.hpp>
#include <memory>
#include <optional>
#include <type_traits>

#include <sys/syscall.h>
#include <unistd.h>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <iso15118/message/authorization.hpp>
#include <iso15118/message/authorization_setup.hpp>
#include <iso15118/message/service_discovery.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/session_stop.hpp>
#include <iso15118/message/variant.hpp>
#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/message_2/payment_service_selection.hpp>
#include <iso15118/message_2/power_delivery.hpp>
#include <iso15118/message_2/service_discovery.hpp>
#include <iso15118/message_2/session_setup.hpp>
#include <iso15118/message_2/session_stop.hpp>
#include <iso15118/message_2/variant.hpp>
#include <iso15118/message_din/charge_parameter_discovery.hpp>
#include <iso15118/message_din/contract_authentication.hpp>
#include <iso15118/message_din/service_discovery.hpp>
#include <iso15118/message_din/service_payment_selection.hpp>
#include <iso15118/message_din/session_setup.hpp>
#include <iso15118/message_din/session_stop.hpp>
#include <iso15118/message_din/variant.hpp>
#include <iso15118/session/iso.hpp>
#include <iso15118/tbd_controller.hpp>

#include "mock_connection.hpp"

namespace {
auto now = iso15118::TimePoint{} + std::chrono::seconds(1);
}

// Interpose the monotonic clock only in this executable; certificate wall time stays real.
extern "C" int clock_gettime(clockid_t clock, timespec* value) noexcept {
    if (clock != CLOCK_MONOTONIC) {
        return static_cast<int>(syscall(SYS_clock_gettime, clock, value));
    }
    const auto elapsed = now.time_since_epoch();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed);
    value->tv_sec = seconds.count();
    value->tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed - seconds).count();
    return 0;
}

namespace {
using namespace std::chrono_literals;
namespace dt = iso15118::message_20::datatypes;
using iso15118::test::MockConnection;

iso15118::session::SessionConfig make_eim_session_config() {
    iso15118::session::EvseSetupConfig setup{};
    setup.authorization_services = {dt::Authorization::EIM};
    setup.control_mobility_modes = {{dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc}};
    return iso15118::session::SessionConfig{setup};
}

struct SessionFixture {
    iso15118::session::feedback::Callbacks callbacks;
    std::optional<iso15118::d20::PauseContext> pause_ctx;
    std::optional<iso15118::d2::PauseContext> d2_pause_ctx;
    std::unique_ptr<MockConnection> connection = std::make_unique<MockConnection>();
    MockConnection* conn = connection.get();

    SessionFixture() {
        callbacks.signal = [](auto) {};
    }

    template <typename... Options>
    std::unique_ptr<iso15118::Session> make_session(iso15118::session::SessionConfig config, Options... options) {
        return std::make_unique<iso15118::Session>(std::move(connection), std::move(config), callbacks, pause_ctx,
                                                   d2_pause_ctx, options...);
    }
};

template <typename Request>
void queue_request(MockConnection& conn, const Request& req,
                   iso15118::io::v2gtp::PayloadType payload = iso15118::io::v2gtp::PayloadType::Part20Main) {
    std::array<uint8_t, 256> buffer{};
    const auto size = serialize(req, iso15118::io::StreamOutputView{buffer.data(), buffer.size()});
    conn.written.clear();
    conn.queue_v2gtp_packet(payload, buffer.data(), size);
    conn.fire(iso15118::io::ConnectionEvent::NEW_DATA);
}

void queue_sap(MockConnection& conn, iso15118::ProtocolId protocol) {
    iso15118::message_20::SupportedAppProtocolRequest sap;
    const auto protocol_namespace = protocol == iso15118::ProtocolId::ISO15118_20
                                        ? iso15118::ISO20_AC_PROTOCOL_NAMESPACE
                                    : protocol == iso15118::ProtocolId::ISO15118_2 ? iso15118::ISO2_NAMESPACE
                                                                                   : iso15118::DIN70121_NAMESPACE;
    sap.app_protocol.push_back({protocol_namespace, protocol == iso15118::ProtocolId::ISO15118_20 ? 1u : 2u, 0, 1, 1});
    queue_request(conn, sap, iso15118::io::v2gtp::PayloadType::SAP);
}

void queue_setup(MockConnection& conn, iso15118::ProtocolId protocol) {
    if (protocol == iso15118::ProtocolId::ISO15118_2) {
        queue_request(conn, iso15118::message_2::SessionSetupRequest{}, iso15118::io::v2gtp::PayloadType::SAP);
    } else if (protocol == iso15118::ProtocolId::DIN70121) {
        queue_request(conn, iso15118::message_din::SessionSetupRequest{}, iso15118::io::v2gtp::PayloadType::SAP);
    } else {
        queue_request(conn, iso15118::message_20::SessionSetupRequest{});
    }
}

template <typename Response>
Response
response_from_wire(const MockConnection& conn,
                   iso15118::io::v2gtp::PayloadType payload_type = iso15118::io::v2gtp::PayloadType::Part20Main) {
    const auto header_size = iso15118::io::SdpPacket::V2GTP_HEADER_SIZE;
    REQUIRE(conn.written.size() > header_size);
    const iso15118::message_20::Variant response{
        payload_type,
        iso15118::io::StreamInputView{conn.written.data() + header_size, conn.written.size() - header_size}};
    return response.get<Response>();
}

dt::SessionId start_session(iso15118::Session& session, MockConnection& conn) {
    conn.fire(iso15118::io::ConnectionEvent::ACCEPTED);
    iso15118::message_20::SessionSetupRequest setup{};
    setup.evccid = "EVCC";
    queue_request(conn, setup);
    session.poll();
    const auto session_id = response_from_wire<iso15118::message_20::SessionSetupResponse>(conn).header.session_id;
    now += 100ms;
    queue_request(conn, iso15118::message_20::AuthorizationSetupRequest{{session_id, 0}});
    session.poll();
    REQUIRE(response_from_wire<iso15118::message_20::AuthorizationSetupResponse>(conn).response_code ==
            dt::ResponseCode::OK);
    return session_id;
}

void queue_authorization(MockConnection& conn, const dt::SessionId& session_id) {
    iso15118::message_20::AuthorizationRequest req{};
    req.header = {session_id, 0};
    req.selected_authorization_service = dt::Authorization::EIM;
    req.authorization_mode = dt::EIM_ASReqAuthorizationMode{};
    queue_request(conn, req);
}

struct Iso2Messages {
    static constexpr auto protocol = iso15118::ProtocolId::ISO15118_2;
    using Document = iso2_exiDocument;
    static constexpr auto decode = decode_iso2_exiDocument;
    using SetupRequest = iso15118::message_2::SessionSetupRequest;
    using SetupResponse = iso15118::message_2::SessionSetupResponse;
    using DiscoveryRequest = iso15118::message_2::ServiceDiscoveryRequest;
    using DiscoveryResponse = iso15118::message_2::ServiceDiscoveryResponse;
    using SelectionRequest = iso15118::message_2::PaymentServiceSelectionRequest;
    using SelectionResponse = iso15118::message_2::PaymentServiceSelectionResponse;
    using AuthRequest = iso15118::message_2::AuthorizationRequest;
    using AuthResponse = iso15118::message_2::AuthorizationResponse;
    using ParametersRequest = iso15118::message_2::ChargeParameterDiscoveryRequest;
    using ParametersResponse = iso15118::message_2::ChargeParameterDiscoveryResponse;
    using StopRequest = iso15118::message_2::SessionStopRequest;
    using StopResponse = iso15118::message_2::SessionStopResponse;
};

struct DinMessages {
    static constexpr auto protocol = iso15118::ProtocolId::DIN70121;
    using Document = din_exiDocument;
    static constexpr auto decode = decode_din_exiDocument;
    using SetupRequest = iso15118::message_din::SessionSetupRequest;
    using SetupResponse = iso15118::message_din::SessionSetupResponse;
    using DiscoveryRequest = iso15118::message_din::ServiceDiscoveryRequest;
    using DiscoveryResponse = iso15118::message_din::ServiceDiscoveryResponse;
    using SelectionRequest = iso15118::message_din::ServicePaymentSelectionRequest;
    using SelectionResponse = iso15118::message_din::ServicePaymentSelectionResponse;
    using AuthRequest = iso15118::message_din::ContractAuthenticationRequest;
    using AuthResponse = iso15118::message_din::ContractAuthenticationResponse;
    using ParametersRequest = iso15118::message_din::ChargeParameterDiscoveryRequest;
    using ParametersResponse = iso15118::message_din::ChargeParameterDiscoveryResponse;
    using StopRequest = iso15118::message_din::SessionStopRequest;
    using StopResponse = iso15118::message_din::SessionStopResponse;
};

template <typename Messages> struct Pre20Session : SessionFixture {
    std::unique_ptr<iso15118::Session> session;
    iso15118::message_2::datatypes::SessionId session_id{};
    std::vector<iso15118::session::feedback::Signal> signals;
    std::vector<iso15118::session::feedback::SessionStopAction> stop_actions;
    const bool same_type_only;

    explicit Pre20Session(bool enabled) : same_type_only(enabled) {
        callbacks.signal = [this](auto signal) { signals.push_back(signal); };
        callbacks.session_stop_res_sent = [this](auto action) { stop_actions.push_back(action); };
        auto config = make_eim_session_config();
        config.evse_id = "DE*EVR*E00000*1";
        config.evse_id_din = "49A00001";
        config.supported_energy_transfer_services = {
            Messages::protocol == iso15118::ProtocolId::ISO15118_2 ? dt::ServiceCategory::AC : dt::ServiceCategory::DC};
        config.ac_limits.charge_power.max = dt::from_float(11000);
        config.dc_limits.voltage.max = dt::from_float(500);
        config.dc_limits.charge_limits.current.max = dt::from_float(200);
        config.dc_limits.charge_limits.power.max = dt::from_float(50000);
        config.supported_protocols = {Messages::protocol};
        session = make_session(config, false, enabled);
        conn->fire(iso15118::io::ConnectionEvent::ACCEPTED);
        queue_sap(*conn, Messages::protocol);
        poll_response();
        const auto sap_response = response_from_wire<iso15118::message_20::SupportedAppProtocolResponse>(
            *conn, iso15118::io::v2gtp::PayloadType::SAP);
        REQUIRE(sap_response.response_code ==
                iso15118::message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation);
        queue(typename Messages::SetupRequest{});
        poll_response();
        session_id = response<typename Messages::SetupResponse>().header.session_id;
    }

    template <typename Request> void queue(const Request& req) {
        queue_request(*conn, req, iso15118::io::v2gtp::PayloadType::SAP);
    }

    template <typename Response> Response response() {
        const auto header_size = iso15118::io::SdpPacket::V2GTP_HEADER_SIZE;
        REQUIRE(conn->written.size() > header_size);
        exi_bitstream_t stream;
        exi_bitstream_init(&stream, conn->written.data() + header_size, conn->written.size() - header_size, 0, nullptr);
        typename Messages::Document doc{};
        REQUIRE(Messages::decode(&stream, &doc) == 0);
        Response result{};
        convert(doc.V2G_Message.Header, result.header);
        const auto& body = doc.V2G_Message.Body;
        if constexpr (std::is_same_v<Response, typename Messages::SetupResponse>) {
            REQUIRE(body.SessionSetupRes_isUsed);
            convert(body.SessionSetupRes, result);
        } else if constexpr (std::is_same_v<Response, typename Messages::DiscoveryResponse>) {
            REQUIRE(body.ServiceDiscoveryRes_isUsed);
            convert(body.ServiceDiscoveryRes, result);
        } else if constexpr (std::is_same_v<Response, typename Messages::ParametersResponse>) {
            REQUIRE(body.ChargeParameterDiscoveryRes_isUsed);
            convert(body.ChargeParameterDiscoveryRes, result);
        } else if constexpr (std::is_same_v<Response, iso15118::message_2::PowerDeliveryResponse>) {
            REQUIRE(body.PowerDeliveryRes_isUsed);
            convert(body.PowerDeliveryRes, result);
        } else if constexpr (std::is_same_v<Response, typename Messages::StopResponse>) {
            REQUIRE(body.SessionStopRes_isUsed);
            convert(body.SessionStopRes, result);
        } else if constexpr (std::is_same_v<Response, typename Messages::SelectionResponse>) {
            if constexpr (Messages::protocol == iso15118::ProtocolId::ISO15118_2) {
                REQUIRE(body.PaymentServiceSelectionRes_isUsed);
                convert(body.PaymentServiceSelectionRes, result);
            } else {
                REQUIRE(body.ServicePaymentSelectionRes_isUsed);
                convert(body.ServicePaymentSelectionRes, result);
            }
        } else if constexpr (std::is_same_v<Response, typename Messages::AuthResponse>) {
            if constexpr (Messages::protocol == iso15118::ProtocolId::ISO15118_2) {
                REQUIRE(body.AuthorizationRes_isUsed);
                convert(body.AuthorizationRes, result);
            } else {
                REQUIRE(body.ContractAuthenticationRes_isUsed);
                convert(body.ContractAuthenticationRes, result);
            }
        }
        return result;
    }

    void poll_response() {
        session->poll();
        if (not same_type_only) {
            REQUIRE(conn->written.empty());
            now += 100ms;
            session->poll();
        }
    }

    void start_authorization() {
        typename Messages::DiscoveryRequest discovery{};
        discovery.header.session_id = session_id;
        queue(discovery);
        poll_response();
        REQUIRE(response<typename Messages::DiscoveryResponse>().response_code ==
                decltype(typename Messages::DiscoveryResponse{}.response_code)::OK);
        typename Messages::SelectionRequest selection{};
        selection.header.session_id = session_id;
        selection.selected_payment_option = decltype(selection.selected_payment_option)::ExternalPayment;
        selection.selected_service_list.push_back({1, std::nullopt});
        queue(selection);
        poll_response();
        REQUIRE(response<typename Messages::SelectionResponse>().response_code ==
                decltype(typename Messages::SelectionResponse{}.response_code)::OK);
        queue_auth();
        poll_response();
        REQUIRE(response<typename Messages::AuthResponse>().response_code ==
                decltype(typename Messages::AuthResponse{}.response_code)::OK);
    }

    void queue_parameters() {
        typename Messages::ParametersRequest req{};
        req.header.session_id = session_id;
        if constexpr (Messages::protocol == iso15118::ProtocolId::ISO15118_2) {
            namespace mdt = iso15118::message_2::datatypes;
            req.requested_energy_transfer_mode = mdt::EnergyTransferMode::AC_three_phase_core;
            auto& params = req.ac_ev_charge_parameter.emplace();
            params.ev_max_voltage = mdt::to_physical_value(230, mdt::Unit::V);
            params.ev_max_current = mdt::to_physical_value(32, mdt::Unit::A);
            params.ev_min_current = mdt::to_physical_value(6, mdt::Unit::A);
        } else {
            req.ev_requested_energy_transfer_type = iso15118::message_din::datatypes::EnergyTransferMode::DC_extended;
            auto& params = req.dc_ev_charge_parameter.emplace();
            params.ev_maximum_current_limit = 200;
            params.ev_maximum_voltage_limit = 400;
        }
        queue(req);
    }

    void queue_auth() {
        typename Messages::AuthRequest auth{};
        auth.header.session_id = session_id;
        queue(auth);
    }
};
} // namespace

TEST_CASE_METHOD(SessionFixture, "ISO 15118-20 opt-in paces only consecutive responses of the same type") {
    auto session = make_session(make_eim_session_config(), true, true);
    const auto session_id = start_session(*session, *conn);

    now += 10ms;
    queue_authorization(*conn, session_id);
    session->poll();
    REQUIRE(response_from_wire<iso15118::message_20::AuthorizationResponse>(*conn).evse_processing ==
            dt::Processing::Ongoing);
    const auto sent = now;

    now += 10ms;
    queue_authorization(*conn, session_id);
    REQUIRE(session->poll() == sent + 100ms);
    REQUIRE(conn->written.empty());
    SECTION("a session with a held response can be closed") {
        session->close();
        REQUIRE(session->is_finished());
        REQUIRE(conn->closed);
        return;
    }
    SECTION("a changed response type releases the held deadline") {
        session->push_control_event(iso15118::d20::AuthorizationResponse{true});
        now += 10ms;
        queue_authorization(*conn, session_id);
        session->poll();
        REQUIRE(conn->written.empty());
        iso15118::message_20::ServiceDiscoveryRequest discovery{};
        discovery.header = {session_id, 0};
        queue_request(*conn, discovery);
        session->poll();
        const auto response = response_from_wire<iso15118::message_20::ServiceDiscoveryResponse>(*conn);
        REQUIRE(response.response_code == dt::ResponseCode::OK);
    }
    SECTION("a terminal response releases the held deadline") {
        now += 10ms;
        iso15118::message_20::SessionStopRequest stop{};
        stop.header = {session_id, 0};
        stop.charging_session = dt::ChargingSession::Terminate;
        queue_request(*conn, stop);
        session->poll();
        const auto response = response_from_wire<iso15118::message_20::SessionStopResponse>(*conn);
        REQUIRE(response.response_code == dt::ResponseCode::OK);
    }
    SECTION("the response is sent at its original deadline") {
        now = sent + 99ms;
        REQUIRE(session->poll() == sent + 100ms);
        REQUIRE(conn->written.empty());
        now = sent + 100ms;
        session->poll();
        REQUIRE(response_from_wire<iso15118::message_20::AuthorizationResponse>(*conn).evse_processing ==
                dt::Processing::Ongoing);
    }
}

TEST_CASE_METHOD(SessionFixture, "ISO 15118-20 defaults to pacing different response types") {
    REQUIRE_FALSE(iso15118::TbdConfig{}.throttle_same_type_only);
    auto session = make_session(make_eim_session_config(), true);
    const auto session_id = start_session(*session, *conn);
    const auto sent = now;

    now += 10ms;
    queue_authorization(*conn, session_id);
    REQUIRE(session->poll() == sent + 100ms);
    REQUIRE(conn->written.empty());
    now = sent + 99ms;
    REQUIRE(session->poll() == sent + 100ms);
    REQUIRE(conn->written.empty());
    now = sent + 100ms;
    session->poll();
    REQUIRE(response_from_wire<iso15118::message_20::AuthorizationResponse>(*conn).evse_processing ==
            dt::Processing::Ongoing);
}

TEST_CASE_METHOD(SessionFixture, "ISO 15118-20 terminal responses bypass pacing only with the opt-in") {
    const bool same_type_only = GENERATE(false, true);
    CAPTURE(same_type_only);
    auto session = make_session(make_eim_session_config(), true, same_type_only);
    const auto session_id = start_session(*session, *conn);
    now += 100ms;
    queue_authorization(*conn, session_id);
    session->poll();
    REQUIRE(response_from_wire<iso15118::message_20::AuthorizationResponse>(*conn).evse_processing ==
            dt::Processing::Ongoing);
    auto sent = now;

    SECTION("an ongoing authorization times out just after its last response") {
        now += std::chrono::milliseconds(iso15118::d20::TIMEOUT_ONGOING) - 50ms;
        queue_authorization(*conn, session_id);
        session->poll();
        REQUIRE(response_from_wire<iso15118::message_20::AuthorizationResponse>(*conn).evse_processing ==
                dt::Processing::Ongoing);
        sent = now;

        now += 10ms;
        queue_authorization(*conn, session_id);
        session->poll();
        REQUIRE(conn->written.empty());
        now += 40ms;
        queue_authorization(*conn, session_id);
        session->poll();
        if (not same_type_only) {
            REQUIRE(conn->written.empty());
            now = sent + 100ms;
            session->poll();
        }
        REQUIRE(response_from_wire<iso15118::message_20::AuthorizationResponse>(*conn).response_code ==
                dt::ResponseCode::FAILED);
    }

    SECTION("the EV terminates while waiting for authorization") {
        now += 10ms;
        iso15118::message_20::SessionStopRequest stop{};
        stop.header = {session_id, 0};
        stop.charging_session = dt::ChargingSession::Terminate;
        queue_request(*conn, stop);
        session->poll();
        if (not same_type_only) {
            REQUIRE(conn->written.empty());
            now = sent + 100ms;
            session->poll();
        }
        REQUIRE(response_from_wire<iso15118::message_20::SessionStopResponse>(*conn).response_code ==
                dt::ResponseCode::OK);
    }
}

TEST_CASE_METHOD(SessionFixture, "SAP and pre-20 default to request pacing") {
    const bool same_type_only = false;
    const auto protocol =
        GENERATE(iso15118::ProtocolId::ISO15118_20, iso15118::ProtocolId::ISO15118_2, iso15118::ProtocolId::DIN70121);
    CAPTURE(same_type_only, protocol);
    auto config = make_eim_session_config();
    config.supported_protocols = {protocol};
    auto session = make_session(config, false);
    conn->fire(iso15118::io::ConnectionEvent::ACCEPTED);

    queue_sap(*conn, protocol);
    const auto received = now;
    REQUIRE(session->poll() == received + 100ms);
    REQUIRE(conn->written.empty());
    now = received + 99ms;
    session->poll();
    REQUIRE(conn->written.empty());
    now = received + 100ms;
    session->poll();
    REQUIRE(response_from_wire<iso15118::message_20::SupportedAppProtocolResponse>(
                *conn, iso15118::io::v2gtp::PayloadType::SAP)
                .response_code ==
            iso15118::message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation);

    // Let the last response's gap expire: pre-20 must still wait after the next request.
    now += 200ms;
    if (protocol == iso15118::ProtocolId::ISO15118_20) {
        return;
    }
    queue_setup(*conn, protocol);
    const auto setup_received = now;
    REQUIRE(session->poll() == setup_received + 100ms);
    REQUIRE(conn->written.empty());
    now = setup_received + 99ms;
    session->poll();
    REQUIRE(conn->written.empty());
    now = setup_received + 100ms;
    session->poll();
    REQUIRE_FALSE(conn->written.empty());
}

TEST_CASE_METHOD(SessionFixture, "Opt-in sends SAP and protocol handover responses immediately") {
    const bool same_type_only = true;
    const auto protocol =
        GENERATE(iso15118::ProtocolId::ISO15118_20, iso15118::ProtocolId::ISO15118_2, iso15118::ProtocolId::DIN70121);
    CAPTURE(same_type_only, protocol);
    auto config = make_eim_session_config();
    config.supported_protocols = {protocol};
    auto session = make_session(config, false, same_type_only);
    conn->fire(iso15118::io::ConnectionEvent::ACCEPTED);

    queue_sap(*conn, protocol);
    session->poll();
    const auto sap_response = response_from_wire<iso15118::message_20::SupportedAppProtocolResponse>(
        *conn, iso15118::io::v2gtp::PayloadType::SAP);
    REQUIRE(sap_response.response_code ==
            iso15118::message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation);

    now += 10ms;
    queue_setup(*conn, protocol);
    session->poll();
    REQUIRE_FALSE(conn->written.empty());
}

TEMPLATE_TEST_CASE("Pre-20 opt-in repeats wait from the last wire response", "", Iso2Messages, DinMessages) {
    Pre20Session<TestType> fixture{true};
    fixture.start_authorization();
    const auto sent = now;
    now += 10ms;
    fixture.queue_auth();
    REQUIRE(fixture.session->poll() == sent + 100ms);
    REQUIRE(fixture.conn->written.empty());
    now = sent + 99ms;
    REQUIRE(fixture.session->poll() == sent + 100ms);
    REQUIRE(fixture.conn->written.empty());
    now = sent + 100ms;
    fixture.session->poll();
    const auto response = fixture.template response<typename TestType::AuthResponse>();
    REQUIRE(response.response_code == decltype(response.response_code)::OK);
}

TEMPLATE_TEST_CASE("Pre-20 terminal responses release a held deadline only with the opt-in", "", Iso2Messages,
                   DinMessages) {
    const bool enabled = GENERATE(false, true);
    Pre20Session<TestType> fixture{enabled};
    fixture.start_authorization();
    now += 10ms;
    fixture.queue_auth();
    fixture.session->poll();
    REQUIRE(fixture.conn->written.empty());
    const auto due = now + 100ms;
    now += 10ms;
    fixture.session->push_control_event(iso15118::d20::AuthorizationResponse{false});
    fixture.queue_auth();
    fixture.session->poll();
    if (not enabled) {
        REQUIRE(fixture.conn->written.empty());
        now = due;
        fixture.session->poll();
    }
    const auto response = fixture.template response<typename TestType::AuthResponse>();
    REQUIRE(response.response_code == decltype(response.response_code)::FAILED);
    REQUIRE(fixture.stop_actions == std::vector{iso15118::session::feedback::SessionStopAction::FailedTermination});
    REQUIRE(fixture.conn->closed);
    now += 300ms;
    fixture.session->poll();
    REQUIRE(fixture.session->is_finished());
}

TEST_CASE_METHOD(SessionFixture, "Failed SAP responses bypass pacing only with the opt-in") {
    const bool enabled = GENERATE(false, true);
    auto session = make_session(make_eim_session_config(), false, enabled);
    conn->fire(iso15118::io::ConnectionEvent::ACCEPTED);
    iso15118::message_20::SupportedAppProtocolRequest sap;
    sap.app_protocol.push_back({"unsupported", 1, 0, 1, 1});
    queue_request(*conn, sap, iso15118::io::v2gtp::PayloadType::SAP);
    session->poll();
    if (not enabled) {
        REQUIRE(conn->written.empty());
        now += 100ms;
        session->poll();
    }
    const auto response = response_from_wire<iso15118::message_20::SupportedAppProtocolResponse>(
        *conn, iso15118::io::v2gtp::PayloadType::SAP);
    REQUIRE(response.response_code ==
            iso15118::message_20::SupportedAppProtocolResponse::ResponseCode::Failed_NoNegotiation);
    REQUIRE(conn->closed);
    REQUIRE(session->is_finished());
}

TEMPLATE_TEST_CASE("Pre-20 changed responses release a held deadline", "", Iso2Messages, DinMessages) {
    Pre20Session<TestType> fixture{true};
    fixture.start_authorization();
    now += 10ms;
    fixture.queue_auth();
    fixture.session->poll();
    REQUIRE(fixture.conn->written.empty());
    fixture.session->push_control_event(iso15118::d20::AuthorizationResponse{true});
    now += 10ms;
    fixture.queue_auth();
    fixture.session->poll();
    REQUIRE(fixture.conn->written.empty());
    fixture.queue_parameters();
    fixture.session->poll();
    const auto response = fixture.template response<typename TestType::ParametersResponse>();
    REQUIRE(response.response_code == decltype(response.response_code)::OK);
    REQUIRE_FALSE(fixture.conn->closed);
}

TEST_CASE("ISO 15118-2 pause and terminate responses after power delivery stops") {
    namespace mdt = iso15118::message_2::datatypes;
    const bool enabled = GENERATE(false, true);
    const auto action = GENERATE(mdt::ChargingSession::Pause, mdt::ChargingSession::Terminate);
    Pre20Session<Iso2Messages> fixture{enabled};
    fixture.start_authorization();
    fixture.session->push_control_event(iso15118::d20::AuthorizationResponse{true});
    now += 100ms;
    fixture.queue_auth();
    fixture.poll_response();
    REQUIRE(fixture.response<iso15118::message_2::AuthorizationResponse>().evse_processing ==
            mdt::EVSEProcessing::Finished);
    fixture.queue_parameters();
    fixture.poll_response();
    const auto parameters = fixture.response<iso15118::message_2::ChargeParameterDiscoveryResponse>();
    REQUIRE(parameters.response_code == mdt::ResponseCode::OK);
    REQUIRE(parameters.sa_schedule_list.has_value());
    iso15118::message_2::PowerDeliveryRequest power{};
    power.header.session_id = fixture.session_id;
    power.sa_schedule_tuple_id = parameters.sa_schedule_list->front().sa_schedule_tuple_id;
    power.charge_progress = mdt::ChargeProgress::Stop;
    fixture.queue(power);
    fixture.poll_response();
    REQUIRE(fixture.response<iso15118::message_2::PowerDeliveryResponse>().response_code == mdt::ResponseCode::OK);
    now += 10ms;
    iso15118::message_2::SessionStopRequest stop{};
    stop.header.session_id = fixture.session_id;
    stop.charging_session = action;
    fixture.queue(stop);
    fixture.poll_response();
    REQUIRE(fixture.response<iso15118::message_2::SessionStopResponse>().response_code == mdt::ResponseCode::OK);
    const bool paused = action == mdt::ChargingSession::Pause;
    REQUIRE(fixture.d2_pause_ctx.has_value() == paused);
    REQUIRE(fixture.stop_actions == std::vector{paused ? iso15118::session::feedback::SessionStopAction::Pause
                                                       : iso15118::session::feedback::SessionStopAction::Terminate});
    REQUIRE_FALSE(fixture.conn->closed);
    REQUIRE_FALSE(fixture.session->is_finished());
    now += 4000ms;
    fixture.session->poll();
    REQUIRE(fixture.conn->closed);
    REQUIRE(fixture.session->is_finished());
    REQUIRE(fixture.signals.back() == (paused ? iso15118::session::feedback::Signal::DLINK_PAUSE
                                              : iso15118::session::feedback::Signal::DLINK_TERMINATE));
}

TEST_CASE("DIN terminates with an authorization response held") {
    const bool enabled = GENERATE(false, true);
    Pre20Session<DinMessages> fixture{enabled};
    fixture.start_authorization();
    now += 10ms;
    fixture.queue_auth();
    fixture.session->poll();
    REQUIRE(fixture.conn->written.empty());
    now += 10ms;
    iso15118::message_din::SessionStopRequest stop{};
    stop.header.session_id = fixture.session_id;
    fixture.queue(stop);
    fixture.poll_response();
    const auto response = fixture.response<iso15118::message_din::SessionStopResponse>();
    REQUIRE(response.response_code == decltype(response.response_code)::OK);
    REQUIRE(fixture.stop_actions == std::vector{iso15118::session::feedback::SessionStopAction::Terminate});
    REQUIRE_FALSE(fixture.conn->closed);
    now += 4000ms;
    fixture.session->poll();
    REQUIRE(fixture.session->is_finished());
    REQUIRE(fixture.conn->closed);
    REQUIRE(fixture.signals.back() == iso15118::session::feedback::Signal::DLINK_TERMINATE);
}

TEST_CASE_METHOD(SessionFixture, "SAP handover waits for its paced wire response") {
    const auto protocol =
        GENERATE(iso15118::ProtocolId::ISO15118_20, iso15118::ProtocolId::ISO15118_2, iso15118::ProtocolId::DIN70121);
    auto config = make_eim_session_config();
    config.supported_protocols = {protocol};
    auto session = make_session(config);
    conn->fire(iso15118::io::ConnectionEvent::ACCEPTED);
    queue_sap(*conn, protocol);
    const auto received = now;
    REQUIRE(session->poll() == received + 100ms);
    REQUIRE(conn->written.empty());
    now += 10ms;
    queue_setup(*conn, protocol);
    REQUIRE(session->poll() == received + 100ms);
    REQUIRE_FALSE(session->is_v2g_session_established());
    REQUIRE(conn->written.empty());
    now = received + 99ms;
    session->poll();
    REQUIRE(conn->written.empty());
    now = received + 100ms;
    REQUIRE(session->poll() == now);
    const auto response = response_from_wire<iso15118::message_20::SupportedAppProtocolResponse>(
        *conn, iso15118::io::v2gtp::PayloadType::SAP);
    REQUIRE(response.response_code ==
            iso15118::message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation);
    REQUIRE_FALSE(session->is_v2g_session_established());
    conn->written.clear();
    REQUIRE(session->poll() == now + 100ms);
    REQUIRE(session->is_v2g_session_established());
    REQUIRE(conn->written.empty());
    now += 100ms;
    session->poll();
    REQUIRE_FALSE(conn->written.empty());
    REQUIRE_FALSE(conn->closed);
}
